/* Whole-image setup kernel. Palette selection and sorting deliberately use the
 * same implementation as portable_indexed.c; no authored data is present. */
#include "portable_texture_job.h"
#include "portable_indexed.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __PSP__
#include <malloc.h>
#endif
#define JOB_MAGIC 0x54584a31u
#define ARENA_BASE (704u*1024u)
#define ROUND64(n) (((n)+63u)&~(size_t)63u)
struct JgrTextureJob {
    uint32_t magic,width,height,flags;
    uint32_t input_at,output_at,arena_at,arena_bytes,bytes;
    int32_t status;
    unsigned char reserved[24];
};
typedef struct {uint32_t bytes,used,reserved[2];} ArenaBlock;
typedef struct {unsigned char *data;size_t bytes,small_bytes;} TextureArena;
/* The transport admits one setup worker. This pointer is private to this
 * quantizer copy, never read by the CPU reference encoder or preparation. */
static struct {
    TextureArena *arena;unsigned char padding[64-sizeof(TextureArena*)];
} texture_worker_context __attribute__((aligned(64)));
#define worker_arena texture_worker_context.arena
static void *texture_alloc(size_t bytes)
{
    if(!worker_arena || !bytes || bytes>SIZE_MAX-15)return NULL;
    bytes=(bytes+15)&~(size_t)15;
    unsigned char *at=worker_arena->data,*end=at+worker_arena->bytes;
    if(worker_arena->small_bytes){
        if(bytes<=16384)end=at+worker_arena->small_bytes;
        else at+=worker_arena->small_bytes;
    }
    ArenaBlock *best=NULL;
    while(at<end){
        if((size_t)(end-at)<sizeof(ArenaBlock))return NULL;
        ArenaBlock *b=(ArenaBlock*)at;
        if(b->bytes>(size_t)(end-at)-sizeof *b)return NULL;
        if(!b->used){
            unsigned char *next=at+sizeof *b+b->bytes;
            while(next<end){
                if((size_t)(end-next)<sizeof(ArenaBlock))return NULL;
                ArenaBlock *n=(ArenaBlock*)next;if(n->used)break;
                if(n->bytes>(size_t)(end-next)-sizeof *n)return NULL;
                b->bytes+=sizeof *n+n->bytes;next=at+sizeof *b+b->bytes;
            }
            if(b->bytes>=bytes && (!best || b->bytes<best->bytes))best=b;
        }
        at+=sizeof *b+b->bytes;
    }
    if(!best)return NULL;
    if(best->bytes-bytes>=sizeof *best+16){ArenaBlock *n=(ArenaBlock*)((unsigned char*)best+sizeof *best+bytes);
        n->bytes=best->bytes-bytes-sizeof *best;n->used=0;n->reserved[0]=n->reserved[1]=0;best->bytes=bytes;}
    best->used=1;return (unsigned char*)best+sizeof *best;
}
static void *texture_calloc(size_t count,size_t bytes)
{
    if(bytes && count>SIZE_MAX/bytes)return NULL;
    size_t n=count*bytes;void *p=texture_alloc(n);if(p)memset(p,0,n);return p;
}
static void texture_free(void *p)
{if(p){ArenaBlock *b=(ArenaBlock*)((unsigned char*)p-sizeof(ArenaBlock));b->used=0;}}
typedef union {struct {unsigned char r,g,b,a;} c;uint32_t v;} Pixel;
#define CLIP8(n) ((n)<0?0:(n)>255?255:(n))
#include "portable_indexed_sort.inc"
#define malloc texture_alloc
#define calloc texture_calloc
#define free texture_free
#include "portable_quant_octree.inc"
#undef malloc
#undef calloc
#undef free
#undef MAX
#undef CLIP8
size_t jgr_texture_job_bytes(unsigned w,unsigned h)
{
    if(w<16 || h<8 || w>1024 || h>1024 || w%16 || h%8)return 0;
    size_t pixels=(size_t)w*h;
    return ROUND64(sizeof(JgrTextureJob))+ROUND64(pixels*4)+ROUND64(pixels+1024)+ROUND64(ARENA_BASE+pixels*4);
}
static int valid(const JgrTextureJob *j)
{
    if(!j || j->magic!=JOB_MAGIC || j->flags>1 || !jgr_texture_job_bytes(j->width,j->height))return 0;
    size_t pixels=(size_t)j->width*j->height;
    return j->input_at==ROUND64(sizeof *j) && j->output_at==j->input_at+ROUND64(pixels*4) &&
        j->arena_at==j->output_at+ROUND64(pixels+1024) && j->arena_bytes==ROUND64(ARENA_BASE+pixels*4) &&
        j->bytes==j->arena_at+j->arena_bytes && !((uintptr_t)j&63);
}
JgrTextureJob *jgr_texture_job_create(const unsigned char *prepared,unsigned w,unsigned h,unsigned flags)
{
    size_t bytes=jgr_texture_job_bytes(w,h);if(!prepared || !bytes || flags>1)return NULL;
    void *memory=NULL;
#ifdef __PSP__
    memory=memalign(64,bytes);
#else
    if(posix_memalign(&memory,64,bytes))return NULL;
#endif
    if(!memory)return NULL;
    JgrTextureJob *j=memory;memset(j,0,ROUND64(sizeof *j));j->magic=JOB_MAGIC;j->width=w;j->height=h;j->flags=flags;
    j->input_at=ROUND64(sizeof *j);j->output_at=j->input_at+ROUND64((size_t)w*h*4);
    j->arena_at=j->output_at+ROUND64((size_t)w*h+1024);j->arena_bytes=ROUND64(ARENA_BASE+(size_t)w*h*4);
    j->bytes=bytes;j->status=-1;memcpy((unsigned char*)j+j->input_at,prepared,(size_t)w*h*4);return j;
}
void *jgr_texture_job_range(JgrTextureJob *j,size_t *bytes)
{if(!bytes || !valid(j))return NULL;*bytes=j->bytes;return j;}
int jgr_texture_job_worker(intptr_t argument)
{
    JgrTextureJob *j=(JgrTextureJob*)argument;if(!valid(j))return -1;
    TextureArena arena={(unsigned char*)j+j->arena_at,j->arena_bytes,48u*1024u};
    /* Small cube records cannot fragment the two full fine-cube blocks or
     * the image-sized index array. Both pools retain exact allocation order. */
    ArenaBlock *first=(ArenaBlock*)arena.data;first->bytes=arena.small_bytes-sizeof *first;first->used=0;first->reserved[0]=first->reserved[1]=0;
    first=(ArenaBlock*)(arena.data+arena.small_bytes);first->bytes=arena.bytes-arena.small_bytes-sizeof *first;first->used=0;first->reserved[0]=first->reserved[1]=0;
    worker_arena=&arena;j->status=-1;
    Pixel *palette=NULL;uint32_t count=0,*indices=NULL;
    if(quantize_octree((Pixel*)((unsigned char*)j+j->input_at),j->width*j->height,256,&palette,&count,&indices,1) && count==256){
        unsigned char *out=(unsigned char*)j+j->output_at;memcpy(out,palette,1024);out+=1024;
        for(unsigned y=0;y<j->height;y+=8)for(unsigned x=0;x<j->width;x+=16)
            for(unsigned dy=0;dy<8;dy++)for(unsigned dx=0;dx<16;dx++)*out++=(unsigned char)indices[(y+dy)*j->width+x+dx];
        j->status=0;
    }
    worker_arena=NULL;return j->status;
}
int jgr_texture_job_result(const JgrTextureJob *j,const unsigned char **out,unsigned *flags)
{if(!valid(j) || j->status || !out || !flags)return -1;*out=(const unsigned char*)j+j->output_at;*flags=j->flags;return 0;}
int jgr_texture_job_retry(JgrTextureJob *j)
{
    if(!valid(j))return -1;
    j->status=jgr_indexed_prepared_memory((const unsigned char*)j+j->input_at,j->width,j->height,j->flags,
        (unsigned char*)j+j->output_at,1024+(size_t)j->width*j->height);return j->status;
}
int jgr_texture_job_verify(const JgrTextureJob *j)
{
    if(!valid(j) || j->status)return -1;
    size_t bytes=1024+(size_t)j->width*j->height;unsigned char *reference=malloc(bytes);if(!reference)return -1;
    int result=jgr_indexed_prepared_memory((const unsigned char*)j+j->input_at,j->width,j->height,j->flags,reference,bytes);
    if(!result && memcmp(reference,(const unsigned char*)j+j->output_at,bytes))result=-1;
    free(reference);return result;
}
void jgr_texture_job_destroy(JgrTextureJob *j){free(j);}
