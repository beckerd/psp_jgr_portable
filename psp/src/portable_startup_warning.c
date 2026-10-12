/* Prepare the original startup warning using only a user's validated disc.
 * The constants below describe source formats and display coordinates; no
 * image pixels, palette, text, or other original payload is embedded here. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#include "portable_startup_warning.h"
#include "portable_convert.h"
#include "portable_crc32.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef JGR_STARTUP_WARNING_RENAME
#define JGR_STARTUP_WARNING_RENAME rename
#endif

enum { PATH_BYTES=1024, SOURCE_WIDTH=1024, SOURCE_HEIGHT=1024,
       CROP_WIDTH=640, CROP_HEIGHT=480, VQ_BYTES=256*8+1024*1024/4,
       DISPLAY_WIDTH=360, DISPLAY_HEIGHT=270, DISPLAY_LEFT=60, DISPLAY_TOP=1,
       IMAGE_BYTES=JGR_STARTUP_WARNING_HEADER+JGR_STARTUP_WARNING_PIXELS };
static unsigned word(const unsigned char *p)
{return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static unsigned half(const unsigned char *p){return p[0]|((unsigned)p[1]<<8);}
static void put(unsigned char *p,unsigned n)
{p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);p[2]=(unsigned char)(n>>16);p[3]=(unsigned char)(n>>24);}
static int path(char out[PATH_BYTES],const char *root,const char *relative)
{
    if(!root || !*root || strlen(root)>=PATH_BYTES)return -1;
    int n=snprintf(out,PATH_BYTES,"%s%s%s",root,root[strlen(root)-1]=='/'?"":"/",relative);
    return n<0 || n>=PATH_BYTES ? -1:0;
}
static int valid_path(const char *name)
{
    FILE *file=fopen(name,"rb");if(!file)return 0;
    unsigned char header[JGR_STARTUP_WARNING_HEADER],buffer[4096];int valid=0;
    if(fread(header,1,sizeof header,file)!=sizeof header ||
       memcmp(header,"JGRBG01\0",8) || word(header+8)!=JGR_STARTUP_WARNING_WIDTH ||
       word(header+12)!=JGR_STARTUP_WARNING_HEIGHT ||
       word(header+16)!=JGR_STARTUP_WARNING_PIXELS)goto done;
    unsigned left=JGR_STARTUP_WARNING_PIXELS,crc=0xffffffffu;
    while(left){unsigned n=left<sizeof buffer?left:sizeof buffer;
        if(fread(buffer,1,n,file)!=n)goto done;
        crc=jgr_crc32_add(crc,buffer,n);left-=n;
    }
    valid=fgetc(file)==EOF && !ferror(file) && (crc^0xffffffffu)==word(header+20);
done:
    if(fclose(file))valid=0;
    return valid;
}
int portable_startup_warning_valid(const char *root)
{char name[PATH_BYTES];return !path(name,root,JGR_STARTUP_WARNING_FILE) && valid_path(name);}

static unsigned char *source_pixels(JgrDisc *disc)
{
    JgrDiscFile source;unsigned at=0;
    if(!disc || jgr_disc_source(disc,"decompile/afs/U_SEGALOGO/003_start_titletxr.bin",&source)<0)return NULL;
    /* Walk the two actual GBIX/PVRT records, not incidental magic in pixels.
     * The first, uncompressed image has an accurate physical record length. */
    for(unsigned index=0;index<2;index++){
        unsigned char gbix[8],header[16];
        if(at>source.size || source.size-at<8 || jgr_disc_read(disc,&source,at,gbix,sizeof gbix)<0 ||
           memcmp(gbix,"GBIX",4))return NULL;
        unsigned extension=word(gbix+4);
        if(extension<8 || extension>64 || extension>source.size-at-8)return NULL;
        at+=8+extension;
        if(source.size-at<sizeof header || jgr_disc_read(disc,&source,at,header,sizeof header)<0 ||
           memcmp(header,"PVRT",4) || header[8]!=1 || header[10] || header[11])return NULL;
        unsigned declared=word(header+4),w=half(header+12),h=half(header+14);
        if(!index){
            if(w!=512 || h!=512 || header[9]!=1 || declared!=8+512*512*2 ||
               declared>source.size-at-8)return NULL;
            at+=8+declared;continue;
        }
        /* The VQ source retains its uncompressed declared size. Bound reads
         * to the complete physical codebook/index payload instead. */
        if(w!=SOURCE_WIDTH || h!=SOURCE_HEIGHT || header[9]!=3 ||
           (declared!=8+VQ_BYTES && declared!=8+SOURCE_WIDTH*SOURCE_HEIGHT+256*8) ||
           source.size-at-sizeof header!=VQ_BYTES)return NULL;
        unsigned char *packed=malloc(VQ_BYTES);if(!packed)return NULL;
        int bad=jgr_disc_read(disc,&source,at+sizeof header,packed,VQ_BYTES)<0;
        unsigned char *pixels=bad?NULL:jgr_pvr_decode_payload(packed,VQ_BYTES,1,3,w,h);
        free(packed);return pixels;
    }
    return NULL;
}
static unsigned char *image(JgrDisc *disc)
{
    unsigned char *pixels=source_pixels(disc);if(!pixels)return NULL;
    unsigned char *out=malloc(IMAGE_BYTES);if(!out){free(pixels);return NULL;}
    memset(out,0,IMAGE_BYTES);
    /* Original 024d20 draws texture 1 over a 640x480 canvas with UVs
     * (0,0)..(160/256,120/256): the upper-left 640x480 of the 1024 image.
     * Preserve its 4:3 aspect at exactly 360x270, with 60 black columns on
     * either side and one black row above and below. Exact rational sample
     * centers and one rounded integer bilinear sum keep native PSP and
     * browser output identical, without float modes or stretched artwork. */
    const unsigned dx=2*DISPLAY_WIDTH,dy=2*DISPLAY_HEIGHT,denom=dx*dy;
    for(unsigned y=0;y<DISPLAY_HEIGHT;y++){
        unsigned yn=(2*y+1)*CROP_HEIGHT-DISPLAY_HEIGHT;
        unsigned y0=yn/dy,fy=yn%dy,y1=y0+1<CROP_HEIGHT?y0+1:y0;
        for(unsigned x=0;x<DISPLAY_WIDTH;x++){
            unsigned xn=(2*x+1)*CROP_WIDTH-DISPLAY_WIDTH;
            unsigned x0=xn/dx,fx=xn%dx,x1=x0+1<CROP_WIDTH?x0+1:x0,channel[3];
            const unsigned char *a=pixels+4*(y0*SOURCE_WIDTH+x0),*b=pixels+4*(y0*SOURCE_WIDTH+x1),
                                *c=pixels+4*(y1*SOURCE_WIDTH+x0),*d=pixels+4*(y1*SOURCE_WIDTH+x1);
            for(unsigned k=0;k<3;k++)
                channel[k]=((a[k]*(dx-fx)+b[k]*fx)*(dy-fy)+(c[k]*(dx-fx)+d[k]*fx)*fy+denom/2)/denom;
            unsigned v=(channel[0]>>3)|((channel[1]>>2)<<5)|((channel[2]>>3)<<11);
            unsigned offset=JGR_STARTUP_WARNING_HEADER+2*((y+DISPLAY_TOP)*JGR_STARTUP_WARNING_WIDTH+x+DISPLAY_LEFT);
            out[offset]=(unsigned char)v;out[offset+1]=(unsigned char)(v>>8);
        }
    }
    free(pixels);memcpy(out,"JGRBG01\0",8);
    put(out+8,JGR_STARTUP_WARNING_WIDTH);put(out+12,JGR_STARTUP_WARNING_HEIGHT);
    put(out+16,JGR_STARTUP_WARNING_PIXELS);
    put(out+20,jgr_crc32_add(0xffffffffu,out+JGR_STARTUP_WARNING_HEADER,JGR_STARTUP_WARNING_PIXELS)^0xffffffffu);
    return out;
}
static int directory(const char *name)
{
    if(!mkdir(name,0777))return 0;
    struct stat st;return errno==EEXIST && !stat(name,&st) && S_ISDIR(st.st_mode)?0:-1;
}
int portable_startup_warning_prepare(JgrDisc *disc,const char *root)
{
    char target[PATH_BYTES],temporary[PATH_BYTES],folder[PATH_BYTES];
    if(path(target,root,JGR_STARTUP_WARNING_FILE) || path(temporary,root,JGR_STARTUP_WARNING_FILE ".tmp"))return -1;
    if(valid_path(target))return 0;
    unsigned char *data=image(disc);if(!data)return -1;
    int bad=path(folder,root,"data") || directory(folder) || path(folder,root,"data/setup") || directory(folder);
    FILE *file=NULL;struct stat st;
    /* Do not truncate a directory/device masquerading as our temporary. */
    if(!bad && !stat(temporary,&st) && !S_ISREG(st.st_mode))bad=1;
    if(!bad){file=fopen(temporary,"wb");if(!file)bad=1;}
    if(file){
        if(fwrite(data,1,IMAGE_BYTES,file)!=IMAGE_BYTES)bad=1;
        if(fflush(file))bad=1;
        if(fclose(file))bad=1;
    }
    free(data);
    if(!bad && JGR_STARTUP_WARNING_RENAME(temporary,target)){
        /* FAT may refuse to replace a file. Never remove a valid previous
         * image, including one another preparer published while we worked.
         * An invalid old cache is optional and may be replaced. */
        if(valid_path(target)){remove(temporary);return 0;}
        if(!stat(target,&st) && !S_ISREG(st.st_mode))bad=1;
        if(!bad && ((remove(target) && errno!=ENOENT) || JGR_STARTUP_WARNING_RENAME(temporary,target)))bad=1;
    }
    if(bad){if(file)remove(temporary);return -1;}
    return 0;
}
