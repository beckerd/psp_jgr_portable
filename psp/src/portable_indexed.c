/* Bounded on-device indexed texture conversion. The resampling arithmetic is
 * compatible with Pillow 11.3; octree/sort adaptations retain their notices in
 * the included source and tools/portable/notices. No asset pixels are embedded. */
#include "portable_indexed.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef union {struct {unsigned char r,g,b,a;} c;uint32_t v;} Pixel;
#define CLIP8(n) ((n)<0?0:(n)>255?255:(n))
#include "portable_indexed_sort.inc"
#include "portable_quant_octree.inc"
#undef MAX
#undef CLIP8

typedef struct {int first,count,*tap;} IndexedFilter;
#define FILTER_CACHE_BYTES (128u*1024u)
#define FILTER_CACHE_ENTRIES 16u
typedef struct {
    IndexedFilter *filters;
    size_t bytes;
    uint64_t age;
    unsigned source,target;
} FilterEntry;
static struct {
    FilterEntry entries[FILTER_CACHE_ENTRIES];
    size_t bytes;
    uint64_t clock;
    int enabled;
#ifdef JGR_INDEXED_TESTING
    unsigned hits,builds,evictions;
#endif
} filter_cache;
static void free_filters(IndexedFilter *f){if(f){free(f[0].tap);free(f);}}
void jgr_indexed_cache_end(void)
{
    for(unsigned i=0;i<FILTER_CACHE_ENTRIES;i++)free_filters(filter_cache.entries[i].filters);
    memset(filter_cache.entries,0,sizeof filter_cache.entries);
    filter_cache.bytes=0;filter_cache.clock=0;filter_cache.enabled=0;
}
void jgr_indexed_cache_begin(void)
{jgr_indexed_cache_end();memset(&filter_cache,0,sizeof filter_cache);filter_cache.enabled=1;}
/* No filter is borrowed while these image buffers are allocated. A retained
 * optional cache must not turn an otherwise viable resize into an OOM. */
static void *image_memory(size_t bytes)
{
    void *p=malloc(bytes);
    if(!p && filter_cache.bytes) {
        int enabled=filter_cache.enabled;jgr_indexed_cache_end();filter_cache.enabled=enabled;
        p=malloc(bytes);
    }
    return p;
}
#ifdef JGR_INDEXED_TESTING
void jgr_indexed_cache_stats(JgrIndexedCacheStats *stats)
{
    if(!stats)return;
    stats->bytes=filter_cache.bytes;
    stats->entries=0;
    for(unsigned i=0;i<FILTER_CACHE_ENTRIES;i++)if(filter_cache.entries[i].filters)stats->entries++;
    stats->hits=filter_cache.hits;stats->builds=filter_cache.builds;stats->evictions=filter_cache.evictions;
}
#endif
static unsigned char clip_channel(int n)
{return n<0?0:n>255?255:(unsigned char)n;}
static double lanczos(double x)
{
    const double pi=3.1415926535897932384626433832795;
    if(x==0)return 1;
    if(x<=-3 || x>=3)return 0;
    x*=pi;return sin(x)/x*(sin(x/3)/(x/3));
}
static IndexedFilter *make_filters(unsigned source,unsigned target,size_t *bytes)
{
#ifdef JGR_INDEXED_TESTING
    filter_cache.builds++;
#endif
    double scale=(double)source/target,stretch=scale>1?scale:1,support=3*stretch;
    unsigned stride=(unsigned)ceil(support)*2+1;
    IndexedFilter *filters=calloc(target,sizeof *filters);
    int *taps=malloc((size_t)target*stride*sizeof *taps);
    double *weights=malloc(stride*sizeof *weights);
    if(!filters || !taps || !weights){free(filters);free(taps);free(weights);return NULL;}
    for(unsigned i=0;i<target;i++) {
        double center=(i+0.5)*scale,sum=0;
        int first=(int)(center-support+0.5),end=(int)(center+support+0.5);
        if(first<0)first=0;
        if(end>(int)source)end=source;
        IndexedFilter *f=filters+i;f->first=first;f->count=end-first;f->tap=taps+i*stride;
        for(int k=0;k<f->count;k++){weights[k]=lanczos((k+first-center+0.5)/stretch);sum+=weights[k];}
        for(int k=0;k<f->count;k++){double v=(sum?weights[k]/sum:weights[k])*4194304.0;f->tap[k]=(int)(v+(v<0?-0.5:0.5));}
    }
    free(weights);*bytes=(size_t)target*(sizeof *filters+stride*sizeof *taps);return filters;
}
static IndexedFilter *get_filters(unsigned source,unsigned target,int *cached)
{
    *cached=0;
    if(filter_cache.enabled)for(unsigned i=0;i<FILTER_CACHE_ENTRIES;i++) {
        FilterEntry *e=filter_cache.entries+i;
        if(e->filters && e->source==source && e->target==target) {
            e->age=++filter_cache.clock;*cached=1;
#ifdef JGR_INDEXED_TESTING
            filter_cache.hits++;
#endif
            return e->filters;
        }
    }
    size_t bytes=0;IndexedFilter *filters=make_filters(source,target,&bytes);
    /* The cache is optional. If it crowds out a new working table, release
     * retained entries and retry the ordinary allocation once. No table is
     * borrowed here: the horizontal pass releases its lease before vertical. */
    if(!filters && filter_cache.bytes) {
        jgr_indexed_cache_end();filter_cache.enabled=1;
        filters=make_filters(source,target,&bytes);
    }
    if(!filters || !filter_cache.enabled || bytes>FILTER_CACHE_BYTES-sizeof filter_cache)return filters;
    unsigned slot;
    for(;;) {
        slot=0;while(slot<FILTER_CACHE_ENTRIES && filter_cache.entries[slot].filters)slot++;
        if(slot<FILTER_CACHE_ENTRIES && filter_cache.bytes<=FILTER_CACHE_BYTES-sizeof filter_cache-bytes)break;
        unsigned oldest=FILTER_CACHE_ENTRIES;
        for(unsigned i=0;i<FILTER_CACHE_ENTRIES;i++)if(filter_cache.entries[i].filters &&
            (oldest==FILTER_CACHE_ENTRIES || filter_cache.entries[i].age<filter_cache.entries[oldest].age))oldest=i;
        if(oldest==FILTER_CACHE_ENTRIES)return filters;
        FilterEntry *e=filter_cache.entries+oldest;
        filter_cache.bytes-=e->bytes;free_filters(e->filters);memset(e,0,sizeof *e);
#ifdef JGR_INDEXED_TESTING
        filter_cache.evictions++;
#endif
    }
    filter_cache.entries[slot]=(FilterEntry){filters,bytes,++filter_cache.clock,source,target};
    filter_cache.bytes+=bytes;*cached=1;return filters;
}
unsigned char *jgr_rgba_lanczos(unsigned char *src,unsigned sw,unsigned sh,unsigned w,unsigned h)
{
    if(!src || !sw || !sh || !w || !h || sw>1024 || sh>1024 || w>1024 || h>1024)return NULL;
    unsigned char *out=image_memory((size_t)w*h*4),*middle=NULL;
    IndexedFilter *fx=NULL,*fy=NULL;int cx=0,cy=0;
    if(!out)return NULL;
    if(sw==w && sh==h){memcpy(out,src,(size_t)w*h*4);return out;}
    /* Pillow converts RGBA to premultiplied RGBa before resizing. */
    for(unsigned i=0;i<sw*sh;i++)for(unsigned c=0;c<3;c++) {
        unsigned v=src[4*i+c]*src[4*i+3]+128;src[4*i+c]=(v+(v>>8))>>8;
    }
    if(sw!=w) {
        middle=image_memory((size_t)w*sh*4);fx=get_filters(sw,w,&cx);
        if(!middle || !fx)goto fail;
        for(unsigned y=0;y<sh;y++)for(unsigned x=0;x<w;x++) {
            const IndexedFilter *f=fx+x;int r=2097152,g=r,b=r,a=r;
            const unsigned char *p=src+4*(y*sw+f->first);
            for(int k=0;k<f->count;k++,p+=4) {
                int tap=f->tap[k];r+=p[0]*tap;g+=p[1]*tap;b+=p[2]*tap;a+=p[3]*tap;
            }
            unsigned char *q=middle+4*(y*w+x);
            q[0]=clip_channel(r>>22);q[1]=clip_channel(g>>22);q[2]=clip_channel(b>>22);q[3]=clip_channel(a>>22);
        }
        if(!cx)free_filters(fx);
        fx=NULL;
    }
    if(sh!=h) {
        const unsigned char *input=middle?middle:src;fy=get_filters(sh,h,&cy);if(!fy)goto fail;
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
            const IndexedFilter *f=fy+y;int r=2097152,g=r,b=r,a=r;
            unsigned at=4*(f->first*w+x);
            for(int k=0;k<f->count;k++,at+=4*w) {
                const unsigned char *p=input+at;
                int tap=f->tap[k];r+=p[0]*tap;g+=p[1]*tap;b+=p[2]*tap;a+=p[3]*tap;
            }
            unsigned char *q=out+4*(y*w+x);
            q[0]=clip_channel(r>>22);q[1]=clip_channel(g>>22);q[2]=clip_channel(b>>22);q[3]=clip_channel(a>>22);
        }
    } else memcpy(out,middle?middle:src,(size_t)w*h*4);
    for(unsigned i=0;i<w*h;i++) {
        unsigned a=out[4*i+3];
        if(a && a<255)for(unsigned c=0;c<3;c++)out[4*i+c]=clip_channel(out[4*i+c]*255/a);
    }
    if(!cx)free_filters(fx);
    if(!cy)free_filters(fy);
    free(middle);return out;
fail:if(!cx)free_filters(fx);if(!cy)free_filters(fy);free(middle);free(out);return NULL;
}
static int align_pixels(FILE *out,uint32_t *offset)
{
    long at=ftell(out);if(at<0 || (unsigned long)at>0x7fffff00UL)return -1;
    while(at&15){if(fputc(0,out)==EOF)return -1;at++;}
    *offset=(uint32_t)at;return 0;
}
unsigned char *jgr_indexed_prepare(unsigned char *rgba,unsigned sw,unsigned sh,
                                  unsigned w,unsigned h,unsigned *flags)
{
    if(!flags || w<16 || h<8 || w>1024 || h>1024 || w%16 || h%8)return NULL;
    unsigned char *image=jgr_rgba_lanczos(rgba,sw,sh,w,h);
    if(!image)return NULL;
    unsigned transparent=0,alpha=0;unsigned char transparent_rgb[3]={0};
    for(unsigned i=0;i<w*h;i++) {
        unsigned char *p=image+4*i;if(p[3]<255)alpha=1;
        if(!p[3]){if(!transparent){memcpy(transparent_rgb,p,3);transparent=1;}else memcpy(p,transparent_rgb,3);}
    }
    *flags=alpha;return image;
}
static int indexed_prepared(const unsigned char *image,unsigned w,unsigned h,unsigned alpha,
                            FILE *out,JgrJgcTexture *record,unsigned char *memory,size_t capacity)
{
    if(!image || w<16 || h<8 || w>1024 || h>1024 || w%16 || h%8 || alpha>1)return -1;
    if(memory) {if(capacity<1024u+w*h)return -1;}
    else if(!record || !out)return -1;
    Pixel *palette=NULL;uint32_t count=0,*indices=NULL;int result=-1;
    if(!quantize_octree((Pixel*)image,w*h,256,&palette,&count,&indices,1) || count!=256)goto done;
    if(memory)memcpy(memory,palette,1024);
    else {
        record->width=w;record->height=h;record->flags=alpha;
        if(align_pixels(out,&record->clut) || fwrite(palette,4,256,out)!=256 || align_pixels(out,&record->pixels))goto done;
    }
    unsigned written=1024;
    unsigned char tile[128];
    for(unsigned y=0;y<h;y+=8)for(unsigned x=0;x<w;x+=16) {
        for(unsigned dy=0;dy<8;dy++)for(unsigned dx=0;dx<16;dx++)tile[dy*16+dx]=(unsigned char)indices[(y+dy)*w+x+dx];
        if(memory){memcpy(memory+written,tile,sizeof tile);written+=sizeof tile;}
        else if(fwrite(tile,1,sizeof tile,out)!=sizeof tile)goto done;
    }
    result=0;
done:free(palette);free(indices);return result;
}
int jgr_indexed_prepared_memory(const unsigned char *image,unsigned w,unsigned h,unsigned flags,
                                unsigned char *output,size_t capacity)
{if(!output)return -1;return indexed_prepared(image,w,h,flags,NULL,NULL,output,capacity);}
static int indexed_encode(unsigned char *rgba,unsigned sw,unsigned sh,
                        unsigned w,unsigned h,FILE *out,JgrJgcTexture *record,
                        unsigned char *memory,size_t capacity,unsigned *flags)
{
    if(memory){if(!flags || capacity<1024u+(size_t)w*h)return -1;}
    else if(!record || !out)return -1;
    unsigned alpha=0;unsigned char *image=jgr_indexed_prepare(rgba,sw,sh,w,h,&alpha);
    if(!image)return -1;
    int result=indexed_prepared(image,w,h,alpha,out,record,memory,capacity);
    if(!result && flags)*flags=alpha;
    free(image);return result;
}

int jgr_indexed_texture(unsigned char *rgba,unsigned sw,unsigned sh,
                        unsigned w,unsigned h,FILE *out,JgrJgcTexture *record)
{return indexed_encode(rgba,sw,sh,w,h,out,record,NULL,0,NULL);}
int jgr_indexed_memory(unsigned char *rgba,unsigned sw,unsigned sh,
                       unsigned w,unsigned h,unsigned char *memory,size_t capacity,unsigned *flags)
{if(!memory)return -1;return indexed_encode(rgba,sw,sh,w,h,NULL,NULL,memory,capacity,flags);}
