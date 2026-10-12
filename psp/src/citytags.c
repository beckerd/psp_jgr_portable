#include "gfx.h"
#include "citytags.h"
#include "asset_progress.h"
#include <pspkernel.h>
#include <math.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if JGR_PORTABLE_ASSETS
#include "source_data_rival_graffiti.inc"
#else
#include "generated/source_data/rival_graffiti.inc"
#endif
static unsigned cw_word(const unsigned char *p){unsigned v;memcpy(&v,p,4);return v;}
static int cw_finite(const void *p,unsigned n)
{const unsigned char *b=p;for(unsigned i=0;i<n;i++){float f;memcpy(&f,b+i*4,4);if(!isfinite(f))return 0;}return 1;}
void citytags_free(CityTags *t)
{gfx_wait_idle();free(t->mesh);free(t->image);for(unsigned i=0;i<3;i++)free(t->art[i]);free(t->mask);memset(t,0,sizeof *t);}
int citytags_bind(CityTags *out,const void *image,unsigned size,const Graffiti *g,unsigned stage)
{
    const unsigned char *b=image;
    if(!b || ((uintptr_t)b&3) || size<8 || memcmp(b,"CWT1",4) ||
       !g || !g->spots || g->n<=0 || g->n>4096 || cw_word(b+4)!=(unsigned)g->n ||
       stage<1 || stage>6 || stage==4)return -1;
    const GfoTemplate **mesh=calloc(g->n,sizeof *mesh);if(!mesh)return -1;
    unsigned at=8;
    for(int i=0;i<g->n;i++) {
        if(size-at<4+sizeof(GfoTemplate))goto bad;
        unsigned bytes=cw_word(b+at);const GfoTemplate *t=(const GfoTemplate *)(b+at+4);const Spot *s=g->spots+i;
        if(bytes>size-at || t->bank!=stage || t->chain!=(unsigned)s->area || t->index!=(unsigned)s->idx ||
           t->kind!=(unsigned)s->size || t->kind<1 || t->kind>3 || t->source<0x8cb00000 || t->source>=0x8cde6000 ||
           !t->nbones || t->nbones>64 || !t->nvertex || t->nvertex%3 || t->nvertex>4096)goto bad;
        unsigned expected=4+sizeof *t+t->nbones*sizeof(JgcBone)+t->nvertex*(sizeof(GfoVertex)+4);
        if(bytes!=expected || !cw_finite(t->descriptor,3) || !cw_finite(t->descriptor+24,51))goto bad;
        /* Source TRS must be the same descriptor as the interaction target,
           not a merely equal-size mesh from another area or stage. */
        if(!s->source || memcmp(t->descriptor,s->source,36))goto bad;
        const JgcBone *bones=gfo_bones(t);
        for(unsigned j=0;j<t->nbones;j++)if(bones[j].parent < -1 || bones[j].parent >= (int)j ||
            bones[j].batch0 || bones[j].nbatch || !cw_finite(bones[j].pos,3) || !cw_finite(bones[j].scl,3))goto bad;
        const GfoVertex *v=gfo_vertices(t);
        for(unsigned j=0;j<t->nvertex;j++)if(v[j].bone>=t->nbones || !isfinite(v[j].u) || !isfinite(v[j].v) || !cw_finite(v[j].pos,3))goto bad;
        const uint32_t *m=(const uint32_t *)(v+t->nvertex);
        for(unsigned j=0;j<t->nvertex/3;j++)if(m[j*3]>1 || m[j*3+2]>>24!=255)goto bad;
        mesh[i]=t;at+=bytes;
    }
    if(at!=size)goto bad;
    free(out->mesh);out->mesh=mesh;out->count=g->n;out->worldspace=0;return 0;
bad:free(mesh);return -1;
}
static void *cw_read(const char *name,unsigned *size)
{
    FILE *f=fopen(name,"rb");
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);
    if(n<=0 || n>0x1000000){fclose(f);return NULL;}rewind(f);
    unsigned char *b=memalign(16,n);if(!b){fclose(f);return NULL;}
    unsigned done=0;
    while(done<(unsigned)n) {
        unsigned chunk=(unsigned)n-done;if(chunk>ASSET_READ_CHUNK)chunk=ASSET_READ_CHUNK;
        unsigned got=fread(b+done,1,chunk,f);
        if(!got){fclose(f);free(b);return NULL;}
        done+=got;asset_progress();
    }
    fclose(f);*size=n;return b;
}
int citytags_select_mask(CityTags *tags,unsigned mission_art)
{
    if(mission_art>=sizeof rival_graffiti_mask)return -1;
    unsigned index=rival_graffiti_mask[mission_art];
    if(tags->mask && tags->mask_index==index)return 0;
    char path[32];unsigned size=0;snprintf(path,sizeof path,"rivaltag%u.bin",index);
    unsigned short *p=cw_read(path,&size);
    if(!p || size!=256*256*2){free(p);return -1;}
    for(unsigned j=0;j<256*256;j++){unsigned c=p[j];p[j]=(c&0xf0f0)|((c>>8)&15)|((c&15)<<8);if(!(j&4095))asset_progress();}
    sceKernelDcacheWritebackRange(p,size);gfx_wait_idle();free(tags->mask);
    tags->mask=p;tags->mask_index=index;return 0;
}
int citytags_load(CityTags *out,const Graffiti *g,unsigned stage,const unsigned char selected[3])
{
    CityTags tags={0};char path[32];unsigned size=0;
    snprintf(path,sizeof path,"walltags%u.bin",stage);tags.image=cw_read(path,&size);
    if(citytags_bind(&tags,tags.image,size,g,stage))goto bad;
    for(unsigned i=0;i<3;i++) {
        unsigned count=(128u<<i)*128;
        if(selected[i]>=34)goto bad;snprintf(path,sizeof path,"grf/%c%02u.bin","sml"[i],selected[i]);
        unsigned short *p=cw_read(path,&size);
        if(!p || size!=count*2){free(p);goto bad;}
        tags.art[i]=p;
        for(unsigned j=0;j<count;j++){unsigned c=p[j];p[j]=(c&0xf0f0)|((c>>8)&15)|((c&15)<<8);if(!(j&4095))asset_progress();}
        sceKernelDcacheWritebackRange(p,count*2);
    }
    if(citytags_select_mask(&tags,0))goto bad;
    /* Test builds can keep the previous per-frame path for exact A/B runs. */
#ifndef JGR_NO_STATIC_TAG_CACHE
    citytags_prepare(&tags,g);
#endif
    citytags_free(out);*out=tags;return 0;
bad:citytags_free(&tags);return -1;
}
