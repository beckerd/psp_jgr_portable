/* On-device equivalents of the reviewed TEX1/L2D1 host format converters. */
#include "portable_convert.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t word(const unsigned char *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static unsigned half(const unsigned char *p){return p[0]|((unsigned)p[1]<<8);}
static int putword(FILE *f,uint32_t n)
{unsigned char b[4]={(unsigned char)n,(unsigned char)(n>>8),(unsigned char)(n>>16),(unsigned char)(n>>24)};return fwrite(b,1,4,f)==4?0:-1;}
static int range(const JgrDiscFile *f,uint32_t base,uint32_t address,uint32_t bytes)
{return address>=base && address-base<=f->size && bytes<=f->size-(address-base);}
static int copy(JgrDisc *d,const JgrDiscFile *f,uint32_t off,uint32_t bytes,FILE *out)
{
    unsigned char b[4096];
    while(bytes){unsigned n=bytes<sizeof b?bytes:sizeof b;
        if(jgr_disc_read(d,f,off,b,n)<0 || fwrite(b,1,n,out)!=n)return -1;
        off+=n;bytes-=n;}
    return 0;
}
int jgr_convert_list2d(JgrDisc *d,const JgrDiscFile *f,uint32_t base,
                      uint32_t table,unsigned count,FILE *out)
{
    if(!count || count>1024 || table>f->size || count>(f->size-table)/4)return -1;
    if(fwrite("L2D1",1,4,out)!=4 || putword(out,count))return -1;
    for(unsigned i=0;i<count;i++) {
        unsigned char p[4],header[32];uint32_t address,entries=0,n=0;int valid=0;
        if(jgr_disc_read(d,f,table+4*i,p,4)<0)return -1;
        address=word(p);
        if(range(f,base,address,33)) {
            if(jgr_disc_read(d,f,address-base,header,32)<0)return -1;
            entries=word(header);n=word(header+4);
            valid=n>0 && n<=256 && range(f,base,entries,n*64);
        }
        if(valid) {
            if(fwrite(header+4,1,28,out)!=28 || copy(d,f,entries-base,n*64,out))return -1;
        } else {
            for(unsigned k=0;k<7;k++)if(putword(out,k>=5?0x3f800000u:0))return -1;
        }
    }
    return 0;
}

/* Spread the ten supported coordinate bits into even positions. These masks
 * are the bit-interleaving operation, with no texture-dependent lookup data. */
static unsigned spread10(unsigned n)
{
    n&=0x3ffu;
    n=(n|(n<<8))&0x00ff00ffu;
    n=(n|(n<<4))&0x0f0f0f0fu;
    n=(n|(n<<2))&0x33333333u;
    return (n|(n<<1))&0x55555555u;
}
static unsigned twiddle(unsigned x,unsigned y)
{return spread10(y)|(spread10(x)<<1);}
static void rgba(unsigned pf,unsigned v,unsigned char *p)
{
    if(pf==1){p[0]=((v>>11)&31)*255/31;p[1]=((v>>5)&63)*255/63;p[2]=(v&31)*255/31;p[3]=255;}
    else if(pf==0){p[0]=((v>>10)&31)*255/31;p[1]=((v>>5)&31)*255/31;p[2]=(v&31)*255/31;p[3]=(v&0x8000)?255:0;}
    else{p[0]=((v>>8)&15)*17;p[1]=((v>>4)&15)*17;p[2]=(v&15)*17;p[3]=((v>>12)&15)*17;}
}
static unsigned mip(unsigned width,int vq)
{unsigned n=0;for(unsigned s=1;s<width;s*=2){unsigned b=vq?s*s/4:s*s*2;n+=b?b:1;}return n;}
unsigned char *jgr_pvr_decode_payload(const unsigned char *b,unsigned size,unsigned pf,unsigned df,unsigned w,unsigned h)
{
    if(!b || pf>2 || !w || !h || w>1024 || h>1024 || (w&(w-1)) || (h&(h-1)))return NULL;
    unsigned char *pixels=malloc((size_t)w*h*4);if(!pixels)return NULL;
    if(df==3 || df==4 || df==0x10 || df==0x11){
        unsigned entries=(df==3 || df==4)?256:
            df==0x10?(w<=16?16:w<=32?32:w<=64?128:256):(w<=16?16:w<=32?64:256);
        unsigned base=entries*8+((df==4 || df==0x11)?mip(w,1):0);
        /* Morton addresses increase in each coordinate. The final block is
         * the largest referenced byte even for the supported narrow images.
         * This also proves the entire codebook precedes a readable index. */
        if(base+twiddle((w-1)/2,(h-1)/2)>=size)goto fail;
        uint32_t colors[256][4],ready[8]={0};unsigned row=0;
        for(unsigned y=0;y<h;y+=2){
            unsigned column=0;
            for(unsigned x=0;x<w;x+=2){
                unsigned entry=b[base+(row|column)];if(entry>=entries)goto fail;
                unsigned bit=1u<<(entry&31);
                if(!(ready[entry>>5]&bit)){
                    for(unsigned i=0;i<4;i++)rgba(pf,half(b+entry*8+i*2),(unsigned char *)&colors[entry][i]);
                    ready[entry>>5]|=bit;
                }
                unsigned char *out=pixels+4*(y*w+x);
                memcpy(out,colors[entry],4);
                if(x+1<w)memcpy(out+4,colors[entry]+2,4);
                if(y+1<h){
                    memcpy(out+4*w,colors[entry]+1,4);
                    if(x+1<w)memcpy(out+4*w+4,colors[entry]+3,4);
                }
                /* Increment only the interleaved X bits, carrying across the
                 * Y positions. Avoid expanding both coordinates per pixel. */
                column=(column-0xaaaaaaaau)&0xaaaaaaaau;
            }
            row=(row-0x55555555u)&0x55555555u;
        }
    }else if(df==1 || df==2 || df==0x12){
        unsigned base=df==1?0:mip(w,0)+2,row=0;
        if(base+2*twiddle(w-1,h-1)+2>size)goto fail;
        for(unsigned y=0;y<h;y++){
            unsigned column=0;
            for(unsigned x=0;x<w;x++){
                rgba(pf,half(b+base+2*(row|column)),pixels+4*(y*w+x));
                column=(column-0xaaaaaaaau)&0xaaaaaaaau;
            }
            row=(row-0x55555555u)&0x55555555u;
        }
    }else if(df==9 || df==0xb){
        if(2*w*h>size)goto fail;
        for(unsigned i=0;i<w*h;i++)rgba(pf,half(b+2*i),pixels+4*i);
    }else if(df==0xd){
        unsigned side=w<h?w:h,base=0;if(2*w*h>size)goto fail;
        for(unsigned ty=0;ty<h;ty+=side)for(unsigned tx=0;tx<w;tx+=side){
            unsigned row=0;
            for(unsigned y=0;y<side;y++){
                unsigned column=0;
                for(unsigned x=0;x<side;x++){
                    rgba(pf,half(b+base+2*(row|column)),pixels+4*((ty+y)*w+tx+x));
                    column=(column-0xaaaaaaaau)&0xaaaaaaaau;
                }
                row=(row-0x55555555u)&0x55555555u;
            }
            base+=2*side*side;
        }
    }else goto fail;
    return pixels;
fail:free(pixels);return NULL;
}

/* Catmull-Rom reduction with the established host converter's quantization:
   normalized 22-bit taps, clipping after each axis, premultiplied alpha.
   Compatibility reference: Pillow 12 libImaging/Resample.c and Convert.c.
   This bounded two-to-one implementation stores at most one intermediate
   image; it does not link Pillow or require it on the device. */
typedef struct {int start,count,tap[9];} Filter;
static double cubic(double x)
{x=fabs(x);return x<1?((1.5*x-2.5)*x*x+1):x<2?(((-0.5*x+2.5)*x-4)*x+2):0;}
static void filter(Filter *f,unsigned target,unsigned source)
{
    double weights[9],sum=0,center=2.0*target+1.0;
    int first=(int)(center-3.5),last=(int)(center+4.5);
    if(first<0)first=0;
    if(last>(int)source)last=(int)source;
    f->start=first;f->count=last-first;
    for(int i=0;i<f->count;i++){weights[i]=cubic((first+i-center+0.5)*0.5);sum+=weights[i];}
    for(int i=0;i<f->count;i++){double v=weights[i]/sum*4194304.0;f->tap[i]=(int)(v+(v<0?-0.5:0.5));}
}
static unsigned char clip(int n){return n<0?0:n>255?255:(unsigned char)n;}
unsigned char *jgr_rgba_half_bicubic(unsigned char *src,unsigned w,unsigned h)
{
    if(w<2 || h<2)return NULL;
    unsigned ow=w/2,oh=h/2;
    unsigned char *middle=malloc((size_t)ow*h*4),*out=malloc((size_t)ow*oh*4);
    Filter *horizontal=malloc(ow*sizeof *horizontal);
    if(!middle || !out || !horizontal){free(middle);free(out);free(horizontal);return NULL;}
    for(unsigned x=0;x<ow;x++)filter(&horizontal[x],x,w);
    for(unsigned i=0;i<w*h;i++)for(unsigned c=0;c<3;c++) {
        unsigned v=src[4*i+c]*src[4*i+3]+128;src[4*i+c]=(v+(v>>8))>>8;
    }
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<ow;x++)for(unsigned c=0;c<4;c++) {
        const Filter *f=horizontal+x;int v=2097152;
        for(int k=0;k<f->count;k++)v+=src[4*(y*w+f->start+k)+c]*f->tap[k];
        middle[4*(y*ow+x)+c]=clip(v>>22);
    }
    for(unsigned y=0;y<oh;y++) {
        Filter f;filter(&f,y,h);
        for(unsigned x=0;x<ow;x++)for(unsigned c=0;c<4;c++) {
            int v=2097152;
            for(int k=0;k<f.count;k++)v+=middle[4*((f.start+k)*ow+x)+c]*f.tap[k];
            out[4*(y*ow+x)+c]=clip(v>>22);
        }
    }
    for(unsigned i=0;i<ow*oh;i++) {
        unsigned a=out[4*i+3];
        if(a && a<255)for(unsigned c=0;c<3;c++)out[4*i+c]=clip(out[4*i+c]*255/a);
    }
    free(horizontal);free(middle);return out;
}
static int find_tag(JgrDisc *d,const JgrDiscFile *f,uint32_t start,uint32_t end,const char *tag,uint32_t *found)
{
    unsigned char b[4096];uint32_t pos=start;
    if(end>f->size)end=f->size;
    while(pos<end && end-pos>=4) {
        unsigned n=end-pos<sizeof b?end-pos:sizeof b;
        if(jgr_disc_read(d,f,pos,b,n)<0)return -1;
        for(unsigned i=0;i+4<=n;i++)if(!memcmp(b+i,tag,4)){*found=pos+i;return 1;}
        pos+=n-3;
    }
    return 0;
}
int jgr_convert_tex2d(JgrDisc *d,const JgrDiscFile *f,unsigned maximum,FILE *out)
{
    if(!maximum || maximum>1024)return -1;
    uint32_t at=0,count=0,gbix;int found;
    while((found=find_tag(d,f,at,f->size,"GBIX",&gbix))>0){if(++count>4096)return -1;at=gbix+8;}
    if(found<0 || fwrite("TEX1",1,4,out)!=4 || putword(out,count))return -1;
    at=0;
    for(unsigned i=0;i<count;i++) {
        uint32_t pvr;unsigned char header[16];
        if(find_tag(d,f,at,f->size,"GBIX",&gbix)!=1 ||
           find_tag(d,f,gbix+8,gbix+64,"PVRT",&pvr)!=1 ||
           jgr_disc_read(d,f,pvr,header,16)<0)return -1;
        at=gbix+8;
        unsigned size=word(header+4),pf=header[8],df=header[9],w=half(header+12),h=half(header+14);
        if(size<8 || size>4*1024*1024 || pvr>f->size || f->size-pvr<16)return -1;
        size-=8;
        /* Some original VQ chunks retain an uncompressed declared length.
           Match the host's bounded slice, then validate every actual index
           and codebook access in decode(). Never read beyond the AFS member. */
        if(size>f->size-pvr-16)size=f->size-pvr-16;
        unsigned char *payload=malloc(size?size:1);if(!payload)return -1;
        if(jgr_disc_read(d,f,pvr+16,payload,size)<0){free(payload);return -1;}
        unsigned char *pixels=jgr_pvr_decode_payload(payload,size,pf,df,w,h);free(payload);
        if(!pixels)return -1;
        while(w>maximum || h>maximum) {
            unsigned char *smaller=jgr_rgba_half_bicubic(pixels,w,h);free(pixels);pixels=smaller;
            if(!pixels)return -1;
            w/=2;h/=2;
        }
        unsigned char row[2048];row[0]=w;row[1]=w>>8;row[2]=h;row[3]=h>>8;
        if(fwrite(row,1,4,out)!=4){free(pixels);return -1;}
        for(unsigned y=0;y<h;y++) {
            for(unsigned x=0;x<w;x++) {
                const unsigned char *p=pixels+4*(y*w+x);
                unsigned v=((p[3]>>4)<<12)|((p[2]>>4)<<8)|((p[1]>>4)<<4)|(p[0]>>4);
                row[2*x]=v;row[2*x+1]=v>>8;
            }
            if(fwrite(row,1,w*2,out)!=w*2){free(pixels);return -1;}
        }
        free(pixels);
    }
    return 0;
}
