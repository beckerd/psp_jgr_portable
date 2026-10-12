/* Generated ABI/storage only: no original mesh, index or material bytes. */
#include "source_data_bubble.h"
#include "source_data_io.h"
#if JGR_PORTABLE_ASSETS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef __PSP__
#include <pspkernel.h>
#endif
typedef struct {float u,v;unsigned colour;float x,y,z;} BubbleVertex;
typedef struct {unsigned vertex,index,count,texture,flags;} BubbleBatch;
typedef char vertex_layout_must_be_24[sizeof(BubbleVertex)==24 ? 1:-1];
typedef char batch_layout_must_be_20[sizeof(BubbleBatch)==20 ? 1:-1];
typedef char ushort_layout_must_be_2[sizeof(unsigned short)==2 ? 1:-1];
BubbleVertex jgr_bubble_v0[28] __attribute__((aligned(16)));
unsigned short jgr_bubble_i0[72] __attribute__((aligned(16)));
BubbleBatch jgr_bubble_b0[2] __attribute__((aligned(16)));
BubbleVertex jgr_bubble_v1[28] __attribute__((aligned(16)));
unsigned short jgr_bubble_i1[72] __attribute__((aligned(16)));
BubbleBatch jgr_bubble_b1[2] __attribute__((aligned(16)));
BubbleVertex jgr_bubble_v2[28] __attribute__((aligned(16)));
unsigned short jgr_bubble_i2[72] __attribute__((aligned(16)));
BubbleBatch jgr_bubble_b2[2] __attribute__((aligned(16)));
typedef struct {unsigned kind,count,offset,bytes;void *destination;} BlobSection;
static const BlobSection sections[]={
    {1,28,176,672,jgr_bubble_v0},
    {2,72,848,144,jgr_bubble_i0},
    {3,2,992,40,jgr_bubble_b0},
    {1,28,1040,672,jgr_bubble_v1},
    {2,72,1712,144,jgr_bubble_i1},
    {3,2,1856,40,jgr_bubble_b1},
    {1,28,1904,672,jgr_bubble_v2},
    {2,72,2576,144,jgr_bubble_i2},
    {3,2,2720,40,jgr_bubble_b2},
};
static int ready;
static unsigned word(const unsigned char *p)
{return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static unsigned crc32(const unsigned char *p,size_t bytes)
{
    return source_data_io_checksum(p,bytes);
}
int source_data_bubble_ready(void){return ready;}
int source_data_bubble_load(const void *image,size_t bytes)
{
    const unsigned char *p=image;const unsigned endian=1;
    if(!image || bytes!=2760u || *(const unsigned char *)&endian!=1)return -1;
    if(memcmp(p,"JGBL",4) || word(p+4)!=1 || word(p+8)!=9 || word(p+12)!=bytes ||
       word(p+16)!=0x5efc5e97u || word(p+24)!=176 || word(p+28))return -2;
    if(crc32(p+32,144)!=0x5efc5e97u || crc32(p+176,bytes-176)!=word(p+20))return -3;
    for(unsigned i=0;i<9;i++) {
        const BlobSection *s=sections+i;const unsigned char *entry=p+32+16*i;
        if(word(entry)!=s->kind || word(entry+4)!=s->count || word(entry+8)!=s->offset ||
           word(entry+12)!=s->bytes || s->offset>bytes || s->bytes>bytes-s->offset)return -4;
    }
    /* A checksummed but malformed locally generated mesh must still never
       let the renderer index outside these fixed storage objects. */
    for(unsigned mesh=0;mesh<3;mesh++) {
        const BlobSection *v=sections+mesh*3,*indices=v+1,*b=v+2;
        for(unsigned n=0;n<v->count;n++)for(unsigned field=0;field<6;field++) {
            if(field==2)continue; /* Packed colour is not a float. */
            if((word(p+v->offset+n*24+field*4)&0x7f800000u)==0x7f800000u)return -8;
        }
        for(unsigned n=0;n<b->count;n++) {
            const unsigned char *batch=p+b->offset+n*20;
            unsigned base=word(batch),first=word(batch+4),count=word(batch+8);
            if(base>=v->count || first>=indices->count || !count || count>indices->count-first)return -8;
            for(unsigned k=0;k<count;k++) {
                const unsigned char *index=p+indices->offset+(first+k)*2;
                unsigned vertex=index[0]|((unsigned)index[1]<<8);
                if(vertex>=v->count-base)return -8;
            }
        }
    }
    /* Validation completes before the first write: failed replacement retains
       the previous fully loaded asset. Called only before drawing begins. */
    for(unsigned i=0;i<9;i++)memcpy(sections[i].destination,p+sections[i].offset,sections[i].bytes);
#ifdef __PSP__
    /* Publish once during setup before any GE/ME observer can use the data. */
    for(unsigned i=0;i<9;i++)sceKernelDcacheWritebackRange(sections[i].destination,sections[i].bytes);
#endif
    ready=1;return 0;
}
int source_data_bubble_load_file(const char *path)
{
    const void *memory=NULL;size_t memory_bytes=0;
    int view=source_data_io_view(path,&memory,&memory_bytes);
    if(view)return view<0 ? -5:source_data_bubble_load(memory,memory_bytes);
    FILE *f=source_data_io_open(path);if(!f)return -5;
    unsigned char *data=malloc(2760u);if(!data){fclose(f);return -6;}
    size_t n=fread(data,1,2760u,f);
    int failed=n!=2760u || fgetc(f)!=EOF || ferror(f);
    if(fclose(f))failed=1;
    int result=failed ? -7:source_data_bubble_load(data,n);free(data);return result;
}
#endif
