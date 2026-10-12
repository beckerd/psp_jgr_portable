#include "portable_resources.h"
#include "portable_convert.h"
#include <stdlib.h>
#include <string.h>

static unsigned u32(const unsigned char *p)
{return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static int get(JgrDisc *d,const JgrDiscFile *f,unsigned at,unsigned *out)
{unsigned char b[4];if(jgr_disc_read(d,f,at,b,4)<0)return -1;*out=u32(b);return 0;}
static int put(FILE *f,unsigned n)
{unsigned char b[4]={(unsigned char)n,(unsigned char)(n>>8),(unsigned char)(n>>16),(unsigned char)(n>>24)};return fwrite(b,1,4,f)==4?0:-1;}
static int copy(JgrDisc *d,const JgrDiscFile *f,unsigned at,unsigned size,FILE *out)
{
    unsigned char b[4096];
    if(at>f->size || size>f->size-at)return -1;
    while(size){unsigned n=size<sizeof b?size:sizeof b;
        if(jgr_disc_read(d,f,at,b,n)<0 || fwrite(b,1,n,out)!=n)return -1;
        at+=n;size-=n;}
    return 0;
}
static int magic(FILE *f,const char *value,unsigned count)
{return fwrite(value,1,4,f)!=4 || put(f,count)?-1:0;}
static int find(JgrDisc *d,const JgrDiscFile *f,unsigned start,const char *tag,unsigned *out)
{
    unsigned char b[4096];
    while(start<f->size && f->size-start>=4) {
        unsigned n=f->size-start<sizeof b?f->size-start:sizeof b;
        if(jgr_disc_read(d,f,start,b,n)<0)return -1;
        for(unsigned i=0;i+4<=n;i++)if(!memcmp(b+i,tag,4)){*out=start+i;return 0;}
        start+=n-3;
    }
    return -1;
}
static int font(JgrDisc *d,const JgrDiscFile *f,unsigned index,FILE *out)
{
    if(index>1)return -1;
    unsigned gbix=0,pvr=0,size=0;unsigned char header[16];
    for(unsigned i=0;i<=index;i++) {
        if(find(d,f,i?pvr+8+size:0,"GBIX",&gbix) || get(d,f,gbix+4,&size) || size>64)return -1;
        pvr=gbix+8+size;
        if(jgr_disc_read(d,f,pvr,header,16)<0 || memcmp(header,"PVRT",4))return -1;
        size=u32(header+4);
    }
    unsigned w=header[12]|((unsigned)header[13]<<8),h=header[14]|((unsigned)header[15]<<8);
    if(w!=512 || h!=512 || size<8 || size>4*1024*1024u || pvr>f->size || f->size-pvr<16)return -1;
    size-=8;if(size>f->size-pvr-16)size=f->size-pvr-16;
    unsigned char *packed=malloc(size?size:1);if(!packed)return -1;
    if(jgr_disc_read(d,f,pvr+16,packed,size)<0){free(packed);return -1;}
    unsigned char *pixels=jgr_pvr_decode_payload(packed,size,header[8],header[9],w,h);free(packed);
    if(!pixels)return -1;
    unsigned char palette[64]={0};unsigned ncolors=0;int bad=0;
    for(unsigned i=0;i<w*h;i++) {
        unsigned k;for(k=0;k<ncolors;k++)if(!memcmp(palette+4*k,pixels+4*i,4))break;
        if(k==ncolors){if(ncolors==16){bad=1;break;}memcpy(palette+4*ncolors++,pixels+4*i,4);}
        /* Original RGBA pixels are no longer needed once their palette index
           is known. Storing indices at the start cannot overwrite later RGBA. */
        pixels[i]=(unsigned char)k;
    }
    if(!bad && (magic(out,"FNT2",w) || put(out,h) || put(out,index?24:12) || put(out,24) || put(out,index?21:42) || fwrite(palette,1,64,out)!=64))bad=1;
    unsigned char row[256];
    for(unsigned y=0;!bad && y<h;y++) {
        for(unsigned x=0;x<w;x+=2)row[x/2]=pixels[y*w+x]|(pixels[y*w+x+1]<<4);
        if(fwrite(row,1,sizeof row,out)!=sizeof row)bad=1;
    }
    free(pixels);return bad?-1:0;
}
static int result_camera(JgrDisc *d,const JgrDiscFile *f,unsigned character,FILE *out)
{
    unsigned root,actual,table,record,initial,track,md,format;
    unsigned char tag[4];
    if(jgr_disc_read(d,f,0,tag,4)<0 || memcmp(tag,"PLAY",4) || get(d,f,8,&root) || get(d,f,root,&actual) || actual!=character ||
       get(d,f,root+0x88,&table) || get(d,f,table,&record) || get(d,f,record,&initial) || get(d,f,record+4,&track) ||
       get(d,f,track,&md) || get(d,f,track+8,&format) || format!=0x401c1)return -1;
    unsigned starts[8]={initial,track,md,record},lengths[8]={64,12,32,8},low=f->size,high=0;
    for(unsigned c=0;c<4;c++) {
        unsigned n;
        if(get(d,f,md+4*c,&starts[4+c]) || get(d,f,md+16+4*c,&n) || n>f->size/(c<2?16:8))return -1;
        lengths[4+c]=n*(c<2?16:8);
    }
    for(unsigned i=0;i<8;i++) {
        if(starts[i]>f->size || lengths[i]>f->size-starts[i])return -1;
        if(starts[i]<low)low=starts[i];
        if(starts[i]+lengths[i]>high)high=starts[i]+lengths[i];
    }
    if(!low || high%4 || magic(out,"SPC1",low) || put(out,high) || put(out,1) || copy(d,f,low,high-low,out) || put(out,record))return -1;
    return 0;
}
static int spray_camera(JgrDisc *d,const JgrDiscFile *f,FILE *out)
{
    /* Source coordinates from the reviewed PLAYER camera schema. */
    const unsigned base=0x8c8e1000,first=0x8c8f6704,table=0x8c8fa0c0,end=table+17*4;
    return magic(out,"SPC1",first) || put(out,table) || put(out,17) || copy(d,f,first-base,end-first,out)?-1:0;
}
static int spots(JgrDisc *d,const JgrDiscFile *f,unsigned table,FILE *out)
{
    const unsigned base=0x8cb00000;unsigned total=0;
    for(unsigned pass=0;pass<2;pass++) {
        if(pass && magic(out,"SPT6",total))return -1;
        for(unsigned area=0;area<6;area++) {
            unsigned list;if(table<base || get(d,f,table-base+4*area,&list))return -1;
            for(unsigned index=0;list;index++,list+=4) {
                unsigned a;if(index>=1024 || list<base || get(d,f,list-base,&a))return -1;
                if(!a)break;
                if(a<base || a-base>f->size || f->size-(a-base)<0x104)return -1;
                if(!pass){total++;continue;}
                unsigned kind,mask,points,count,camera,ncamera;
                if(get(d,f,a-base,&kind) || kind<1 || kind>3 || get(d,f,a-base+0xe8,&mask) ||
                   get(d,f,a-base+0xec,&points) || get(d,f,a-base+0xf0,&count) || count>7 ||
                   get(d,f,a-base+0xf4,&camera) || get(d,f,a-base+0xf8,&ncamera) || ncamera>5)return -1;
                float type=(float)kind;unsigned bits;memcpy(&bits,&type,4);
                if(put(out,area)||put(out,index)||put(out,bits)||copy(d,f,a-base+4,12,out)||copy(d,f,a-base+0x58,144,out)||
                   put(out,mask)||put(out,count) || (count && (points<base || copy(d,f,points-base,32*count,out))) ||
                   copy(d,f,a-base+4,84,out)||put(out,ncamera) || (ncamera && (camera<base || copy(d,f,camera-base,176*ncamera,out))))return -1;
            }
        }
    }
    return 0;
}
static int voices(JgrDisc *d,const JgrDiscFile *exe,FILE *out)
{
    const unsigned base=0x8c010000;unsigned resources,banks[17]={5};JgrDiscFile files[17],archive;
    if(get(d,exe,0x8c05bf94-base,&resources) || jgr_disc_find(d,"JETRADIO/E_SND_SE.AFS",&archive)<0)return -1;
    for(unsigned i=0;i<16;i++) {
        unsigned script=0x8c1cded0+i*40,reference,record;
        if(get(d,exe,script-base+12,&reference) || reference>65535 || resources<base ||
           get(d,exe,resources-base+reference*4,&record) || record<base || get(d,exe,record-base+4,&banks[i+1]))return -1;
    }
    unsigned offsets[17],cursor=8+17*8;
    for(unsigned i=0;i<17;i++) {
        unsigned char head[4];
        if(jgr_disc_afs(d,&archive,banks[i],&files[i])<0 || jgr_disc_read(d,&files[i],0,head,4)<0 || memcmp(head,"SOSB",4))return -1;
        unsigned j;for(j=0;j<i;j++)if(banks[j]==banks[i])break;
        if(j<i)offsets[i]=offsets[j];
        else {offsets[i]=cursor;if(files[i].size>0x7fffffffu-cursor)return -1;cursor+=files[i].size;}
    }
    if(magic(out,"OSV1",17))return -1;
    for(unsigned i=0;i<17;i++)if(put(out,offsets[i])||put(out,files[i].size))return -1;
    for(unsigned i=0;i<17;i++) {
        unsigned j;for(j=0;j<i;j++)if(banks[j]==banks[i])break;
        if(j==i && copy(d,&files[i],0,files[i].size,out))return -1;
    }
    return 0;
}
static int practice(JgrDisc *d,const JgrDiscFile *f,FILE *out)
{
    unsigned header,entry,count;unsigned char tail[12];
    if(get(d,f,0xe8,&header) || header<0x8c9f0000 || get(d,f,header-0x8c9f0000,&entry) ||
       get(d,f,header-0x8c9f0000+4,&count) || count!=1 || entry<0x8c9f0000 ||
       jgr_disc_read(d,f,header-0x8c9f0000+20,tail,12)<0)return -1;
    float z=-1.78f;unsigned bits;memcpy(&bits,&z,4);
    return magic(out,"L2D1",1)||put(out,1)||put(out,0)||put(out,0)||put(out,bits)||
        fwrite(tail,1,12,out)!=12 || copy(d,f,entry-0x8c9f0000,64,out)?-1:0;
}
static int strings(JgrDisc *d,const JgrDiscFile *f,const unsigned args[4],FILE *out)
{
    unsigned base=args[0],count=args[2];if(!count || count>512)return -1;
    for(unsigned pass=0;pass<(args[3]?2u:1u);pass++) {
        unsigned table;if(get(d,f,pass?args[3]:args[1],&table) || table<base)return -1;
        for(unsigned i=0;i<count;i++) {
            unsigned p;if(get(d,f,table-base+4*i,&p) || p<base)return -1;p-=base;
            for(unsigned n=0;;n++) {
                unsigned char c;if(n>=8192 || jgr_disc_read(d,f,p+n,&c,1)<0)return -1;
                if(!c)break;
                if(fputc(c,out)==EOF)return -1;
            }
            if(fputc('\n',out)==EOF)return -1;
        }
    }
    return 0;
}
static int grind_font(JgrDisc *d,const unsigned char used[256],FILE *out)
{
    JgrDiscFile file;unsigned at,size;unsigned char header[16],chars[48]={0};
    unsigned colors[16]={0},count=0,ncolors=1;
    for(unsigned i=0;i<256;i++)if(used[i]){if(count==42)return -1;chars[count++]=(unsigned char)i;}
    if(jgr_disc_source(d,"decompile/afs/E_FONTBASIC/000_e_fontbasictxr.bin",&file) ||
       find(d,&file,0,"PVRT",&at) || jgr_disc_read(d,&file,at,header,16))return -1;
    size=u32(header+4);
    if(size<8 || size>1024*1024 || u32(header+12)!=0x02000200)return -1;
    unsigned char *source=malloc(size-8);if(!source)return -1;
    if(jgr_disc_read(d,&file,at+16,source,size-8)){free(source);return -1;}
    unsigned char *pixels=jgr_pvr_decode_payload(source,size-8,header[8],header[9],512,512);
    free(source);if(!pixels)return -1;
    unsigned char packed[8192]={0};int result=-1;
    for(unsigned y=0;y<64;y++)for(unsigned x=0;x<256;x++) {
        unsigned tile=y/24*21+x/12,color=0,k;
        if(x<252 && tile<count) {
            unsigned c=chars[tile],sx=c%42*12+x%12,sy=c/42*24+y%24;
            color=u32(pixels+(sy*512+sx)*4);
        }
        for(k=0;k<ncolors;k++)if(colors[k]==color)break;
        if(k==ncolors){if(ncolors==16)goto done;colors[ncolors++]=color;}
        packed[(y*256+x)/2]|=(unsigned char)(k<<((x&1)*4));
    }
    if(fwrite(chars,1,sizeof chars,out)!=sizeof chars)goto done;
    for(unsigned i=0;i<16;i++)if(put(out,colors[i]))goto done;
    if(fwrite(packed,1,sizeof packed,out)!=sizeof packed)goto done;
    result=0;
done:free(pixels);return result;
}
static int grind_map(JgrDisc *d,const JgrDiscFile *garage,FILE *out)
{
    /* Task 0f6d04 uses its own nine placements, strings and title list.
       Read them from the user's disc; only source addresses belong here. */
    JgrDiscFile exe;unsigned strings_at,title,quad,count;unsigned char used[256]={0};
    const unsigned base=0x8c800000,code=0x8c010000;
    if(jgr_disc_source(d,"decompile/extracted/1ST_READ.BIN",&exe) ||
       get(d,garage,0x34c,&strings_at) || strings_at<base ||
       magic(out,"GMP1",9))return -1;
    for(unsigned i=0;i<9;i++) {
        unsigned cell,object;unsigned char point[8];float y;
        if(get(d,&exe,0x8c21aff8-code+4*i,&cell) || cell<base ||
           get(d,garage,cell-base,&object) || object<base ||
           jgr_disc_read(d,garage,object-base+8,point,8))return -1;
        memcpy(&y,point+4,4);y=-y;memcpy(point+4,&y,4);
        if(fwrite(point,1,8,out)!=8)return -1;
        unsigned name=i<2 ? 63+i:i==8 ? 66:22+2*((i-2)%3);
        unsigned prompt=i<2 ? 7:i==8 ? 68:23+2*((i-2)%3);
        for(unsigned field=0;field<2;field++) {
            unsigned address,capacity=field ? 64:32;unsigned char text[64]={0};
            if(get(d,garage,strings_at-base+4*(field ? prompt:name),&address) || address<base)return -1;
            unsigned n;
            for(n=0;n<capacity;n++) {
                if(jgr_disc_read(d,garage,address-base+n,text+n,1))return -1;
                if(!text[n])break;
                if(!field)used[text[n]]=1;
            }
            if(n==capacity || fwrite(text,1,capacity,out)!=capacity)return -1;
        }
    }
    if(get(d,garage,0x4a4,&title) || title<base ||
       get(d,garage,title-base,&quad) || quad<base ||
       get(d,garage,title-base+4,&count) || count!=1)return -1;
    return copy(d,garage,quad-base,64,out) || grind_font(d,used,out) ? -1:0;
}
int jgr_resource_convert(JgrDisc *d,const JgrDiscFile *f,unsigned op,const unsigned a[4],FILE *out)
{
    switch(op) {
    case JGR_RESOURCE_FONT:return font(d,f,a[0],out);
    case JGR_RESOURCE_RESULT_CAMERA:return result_camera(d,f,a[0],out);
    case JGR_RESOURCE_SPRAY_CAMERA:return spray_camera(d,f,out);
    case JGR_RESOURCE_SPOTS:return spots(d,f,a[0],out);
    case JGR_RESOURCE_VOICES:return voices(d,f,out);
    case JGR_RESOURCE_PRACTICE_LIST:return practice(d,f,out);
    case JGR_RESOURCE_STRINGS:return strings(d,f,a,out);
    case JGR_RESOURCE_GRIND_MAP:return grind_map(d,f,out);
    default:return -1;
    }
}
