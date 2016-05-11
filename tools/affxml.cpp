/*
 * affxml.cpp:
 *
 * print AFF information as an XML
 */

/*
 * Copyright (c) 2005-2006
 *	Simson L. Garfinkel and Basis Technology, Inc. 
 *      All rights reserved.
 *
 * This code is derrived from software contributed by
 * Simson L. Garfinkel
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. [omitted]
 * 4. Neither the name of Simson Garfinkel, Basis Technology, or other
 *    contributors to this program may be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SIMSON GARFINKEL, BASIS TECHNOLOGY,
 * AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL SIMSON GARFINKEL, BAIS TECHNOLOGy,
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.  
 */


#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"
#include "base64.h"

#ifdef WIN32
#include "unix4win32.h"
#endif

#include <vector>
#include <string>

#ifdef HAVE_CSTRING
#include <cstring>
#endif

using namespace std;

#if HAVE_CTYPE_H
#include <ctype.h>
#endif

#if !defined(HAVE_ISALPHANUM) && defined(HAVE_ISALNUM)
#define isalphanum(c) isalnum(c)
#endif

#if !defined(HAVE_ISALPHANUM) && !defined(HAVE_ISALNUM)
#define isalphanum(c) (isalpha(c)||isdigit(c))
#endif

const char *progname = "affxml";

int opt_x = 0;
char **opt_j = 0;
int opt_j_count = 0;
int opt_stats = 0;

struct page_stat_block {
    uint64_t zsectors;	// number of sectors that are all blank
    uint64_t badsectors;	// number of bad sectors
    uint64_t zpages;		// number of pages that are all blank
    uint64_t pages;		// total number of pages
    uint64_t sectors;		// total number of sectors
};


void usage()
{
    printf("%s version %s\n",progname,PACKAGE_VERSION);
    printf("usage: %s [options] infile... \n",progname);
    printf("   -V         =   Just print the version number and exit\n");
    printf("   -x         =   Don't include the infile filename in output.\n");
    printf("   -j segname =   Just print information about segname \n");
    printf("                  (may be repeated)\n");
    printf("   -s         =   output 'stats' for the file data (may a long time)\n");
    exit(0);
}



/* Return true if segname is in the optj list */
bool in_opt_j_list(char *segname)
{
    for(int i=0;i<opt_j_count;i++){
	if(strcmp(segname,opt_j[i])==0) return true;
    }
    return false;
}

/* It's okay to print if there are no funky characters in it...
 * (avoids problems with invalid UTF-8
 */
bool okay_to_print(const char *data,int datalen)
{
    for(const char *cc=data;cc<data+datalen;cc++){
	if (*cc <= 0) return false;
	if (*cc == 10 || *cc==13) continue;
	if (*cc<32) return false;
	if (*cc>=127) return false;
    }
    return true;
}

bool is_blank(const u_char *buf,size_t len)
{
    for(size_t i=0;i<len;i++){
	if(buf[i]) return false;
    }
    return true;
}

void print_xml64(const char *name,int64_t val)
{
    printf("   <%s coding='base10'>%" I64d"</%s>\n\n",name,val,name);
}

int xml_info(const char *infile)
{
    AFFILE *af = af_open(infile,O_RDONLY,0);
    if(!af){
	warn("%s",infile);
	return -1;
    }

    struct page_stat_block psb;
    memset(&psb,0,sizeof(psb));

    printf("<!-- XML generated by affxml version %s -->\n",PACKAGE_VERSION);
    printf("<affinfo");
    if(!opt_x) printf(" image_filename='%s'",infile);
    printf(">\n");

    af_rewind_seg(af);			// start at the beginning

    char segname[AF_MAX_NAME_LEN];
    int  pages = 0;
    vector<string> seglist;		// list of segments we will get
    vector<int64_t> pagelist;		// list of segments we will get

    while(af_get_next_seg(af,segname,sizeof(segname),0,0,0)==0){
	if(segname[0]==0) continue;	// segment to ignore
	if(strcmp(segname,AF_DIRECTORY)==0) continue; // don't output the directories
	if(strstr(segname,AF_AES256_SUFFIX)) continue; // don't output encrypted segments that won't decrypt

	/* check optj */
	if(opt_j_count > 0 && in_opt_j_list(segname)==false){
	    continue;
	}

	int64_t page_num = af_segname_page_number(segname);
	if(page_num>=0){
	    pages += 1;
	    pagelist.push_back(page_num);
	}
	else {
	    seglist.push_back(segname);
	}
    }

    printf("    <pages coding='base10'>%d</pages>\n",pages); // tell how many pages we have

    /* If we have been asked to create stats, create the states */
    if(opt_stats){
	unsigned char *data= (unsigned char *)malloc(af_page_size(af));
	if(!data) err(1,"Can't allocate page with %d bytes.",af_page_size(af));
	for(vector<int64_t>::const_iterator it = pagelist.begin(); it != pagelist.end(); it++){
	    size_t pagesize = af_page_size(af);
	    size_t sectorsize = af_get_sectorsize(af);
	    if(af_get_page(af,*it,data,&pagesize)){
		err(1,"Can't read page %" PRId64,*it);
	    }
	    psb.pages++;
	    bool allblank = true;
	    for(const unsigned char *s = data; s < data+pagesize; s+=sectorsize){
		psb.sectors ++;
		if(is_blank(s,sectorsize)){
		    psb.zsectors++;
		    continue;
		}
		allblank = false;
		if(af_is_badsector(af,s)){
		    psb.badsectors++;
		    continue;
		}
	    }
	    if(allblank) psb.zpages++;
	}
	free(data);
	printf("  <calculated>\n");
	print_xml64("pages",psb.pages);
	print_xml64("zpages",psb.zpages);
	print_xml64("sectors",psb.sectors);
	print_xml64("zsectors",psb.zsectors);
	print_xml64("badsectors",psb.badsectors);
	printf("  </calculated>\n");
    }

    /* Now that we have a list of segments, print them */
    for(vector<string>::const_iterator it = seglist.begin();
	it != seglist.end();
	it++){
	
	/* See how long the data is */
	size_t datalen = 0;
	uint32_t arg=0;
	
	strcpy(segname,it->c_str());

	if(af_get_seg(af,segname,&arg,0,&datalen)){
	    err(1,"Can't read info for segment '%s'",segname);
	}

	unsigned char *data= (unsigned char *)malloc(datalen);
	if(data==0) err(1,"Can't allocate %zd bytes for data",datalen);
	if(af_get_seg(af,segname,&arg,data,&datalen)!=0){
	    err(1,"Can't read data for segment '%s'",segname);
	}

	/* Change non-XML characters in segname to _ */
	for(char *cc=segname;*cc;cc++){
	    if(!isalphanum(*cc)) *cc = '_';
	}

	if(datalen==8 && (arg & AF_SEG_QUADWORD || af_display_as_quad(segname))){
	    /* Print it as a 64-bit value.
	     * The strcmp is there because early AF_IMAGESIZE segs didn't set
	     * AF_SEG_QUADWORD...
	     */
	    printf("    <%s coding='base10'>%" I64d"</%s>\n",segname,af_decode_q(data),segname);
	    free(data);
	    continue;
	}

	/* If datalen==0, just print the arg as an unsigned number */
	if(datalen==0){
	    printf("    <%s coding='base10'>%" PRIu32 "</%s>\n",segname,arg,segname);
	    free(data);
	    continue;
	}

	/* Just handle it as binhex ... */
	printf("    <%s",segname);
	if(datalen==0){
	    printf(" arg='%" PRIu32 "' />\n",arg);
	    free(data);
	    continue;
	}

	/* If segname ends 'md5', code in hex */
	if(strlen(segname)>=3 && strcmp(segname+strlen(segname)-3,"md5")==0){
	    char hex_buf[40];
	    printf(" coding='base16'>%s</%s>\n",
		   af_hexbuf(hex_buf,sizeof(hex_buf),data,datalen,0),
		   segname);
	    free(data);
	    continue;
	}

	/* If all segment contents are printable ascii with no CRs, LFs, or brackets, 
	 * just print as-is...
	 */
	if(okay_to_print((const char *)data,datalen)){
	    putchar('>');
	    for(const char *cc=(const char *)data;cc<(const char *)data+datalen;cc++){
		switch(*cc){
		case '>': fputs("&lt;",stdout);break;
		case '<': fputs("&gt;",stdout);break;
		case '&': fputs("&amp;",stdout);break;
		case '\'': fputs("&apos;",stdout);break;
		case '"': fputs("&quot;",stdout);break;
		default: putchar(*cc);
		}
	    }
	    printf("</%s>\n",segname);
	    free(data);
	    continue;
	}

	/* Default coding: base64 */
	int  b64size = datalen*2+2;
	char *b64buf = (char *)calloc(b64size,1);
	int  b64size_real = b64_ntop(data,datalen,b64buf,b64size);
	b64buf[b64size_real] = 0;		// be sure it is null terminated

	printf(" coding='base64'>");
	fputs(b64buf,stdout);
	printf("</%s>\n",segname);
	free(b64buf);
	free(data);
    }
    af_close(af);

    printf("</affinfo>\n");
    return 0;
}
	 

int main(int argc,char **argv)
{
    int ch;
    const char *infile;

    /* Figure out how many cols the screen has... */

    while ((ch = getopt(argc, argv, "xj:h?Vs")) != -1) {
	switch (ch) {
	case 'j':
	    if(opt_j==0) opt_j = (char **)malloc(0);
	    opt_j_count++;
	    opt_j = (char **)realloc(opt_j,sizeof(char *)*opt_j_count);
	    opt_j[opt_j_count-1] = strdup(optarg); // make a copy
	case 'x': opt_x++;     break;
	case 's': opt_stats++; break;
	case 'h':
	case '?':
	default:
	    usage();
	    break;
	case 'V':
	    printf("%s version %s\n",progname,PACKAGE_VERSION);
	    exit(0);
	}
    }
    argc -= optind;
    argv += optind;

    if(argc<1){
	usage();
    }


    /* Loop through all of the files */
    printf("<?xml version='1.0' encoding='UTF-8'?>\n");
    printf("<affobjects>\n");
    while(*argv){
	infile = *argv++;		// get the file
	argc--;				// decrement argument counter
	xml_info(infile);
    }
    printf("</affobjects>\n");
    exit(0);
}


