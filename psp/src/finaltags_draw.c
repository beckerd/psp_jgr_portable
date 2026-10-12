#include "ge_submit.h"
#include "ge_projection.h"
#include "gfx.h"
#include "finaltags.h"
#include <pspkernel.h>
#include <pspgu.h>
#include <pspgum.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned short *pixels(const char *name,unsigned count)
{
    FILE *f=fopen(name,"rb");
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END) || ftell(f)!=(long)count*2){fclose(f);return NULL;}rewind(f);
    unsigned short *p=memalign(16,count*2);
    if(!p || fread(p,2,count,f)!=count){fclose(f);free(p);return NULL;}fclose(f);
    for(unsigned i=0;i<count;i++){unsigned c=p[i];p[i]=(c&0xf0f0)|((c>>8)&15)|((c&15)<<8);}
    sceKernelDcacheWritebackRange(p,count*2);return p;
}
void finaltags_art_free(FinalTagArt *a)
{gfx_wait_idle();free(a->small);free(a->large);free(a->mask);memset(a,0,sizeof *a);}
int finaltags_art_load(FinalTagArt *a,unsigned small,unsigned large)
{
    if(small>=34 || large>=34)return -1;
    FinalTagArt art={0};char path[32];
    snprintf(path,sizeof path,"grf/s%02u.bin",small);art.small=pixels(path,128*128);
    snprintf(path,sizeof path,"grf/l%02u.bin",large);art.large=pixels(path,512*128);
    art.mask=pixels("finaltagmask.bin",256*256);
    if(!art.small || !art.large || !art.mask){finaltags_art_free(&art);return -1;}
    finaltags_art_free(a);*a=art;return 0;
}
typedef struct {float u,v;unsigned colour;float x,y,z;} TagVertex;
void finaltags_draw(const FinalTags *tags,const Graffiti *g,const FinalTagArt *art)
{
    if(!g || g->n!=5 || !art->small || !art->large || !art->mask)return;
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGuEnable(GU_DEPTH_TEST);sceGuDisable(GU_ALPHA_TEST);
    sceGuDepthOffset(GE_DECAL_DEPTH_BIAS);
    for(unsigned i=0;i<5;i++) {
        const Spot *s=g->spots+i;const GfoObject *o=tags->objects+i;if(!s->active)continue;
        GfoProgress progress={s->stroke,s->reveal,s->complete,s->started,0};
        float root[16],matrices[64][16];
        for(int r=0;r<4;r++)for(int c=0;c<4;c++)root[r*4+c]=o->matrix[c*4+r];
        Chars bank={0};JgcChar ch={0};bank.bones=(JgcBone *)gfo_bones(o->tpl);ch.nbones=o->tpl->nbones;
        chars_mesh_matrices(&bank,&ch,-1,0,root,matrices);
        const GfoVertex *src=gfo_vertices(o->tpl);const uint32_t *material=tags->materials[i];
        /* Same translucent underpaint/selected-art ordering as city tags.
           Preserve reveal colours and depth testing against the arena. */
        for(unsigned layer=0;layer<2;layer++)for(unsigned first=0;first<o->tpl->nvertex/3;) {
            unsigned end=first+1;while(end<o->tpl->nvertex/3 && !memcmp(material+first*3,material+end*3,12))end++;
            unsigned tex=material[first*3],flags=material[first*3+1];
            if(tex!=1-layer){first=end;continue;}
            unsigned width=tex ? 256:i<4 ? 512:128,height=tex ? 256:128;
            const unsigned short *image=tex ? art->mask:i<4 ? art->large:art->small;
            sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_4444,0,0,0);sceGuTexImage(0,width,height,width,image);
            sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_REPEAT,GU_REPEAT);
            static const int factor[8]={GU_FIX,GU_FIX,GU_OTHER_COLOR,GU_ONE_MINUS_OTHER_COLOR,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,GU_DST_ALPHA,GU_ONE_MINUS_DST_ALPHA};
            unsigned blend=flags>>8,sf=(blend>>5)&7,df=(blend>>8)&7;
            if(flags&8){sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,factor[sf],factor[df],sf==1?0xffffff:0,df==1?0xffffff:0);}
            else sceGuDisable(GU_BLEND);
            sceGuDepthMask(flags&8 ? GU_TRUE:GU_FALSE);
            if(flags&16)sceGuDisable(GU_CULL_FACE);else{sceGuEnable(GU_CULL_FACE);sceGuFrontFace(GU_CCW);}
            TagVertex *v=sceGuGetMemory((end-first)*3*sizeof *v);
            for(unsigned j=first*3;j<end*3;j++) {
                TagVertex *d=v+j-first*3;const float *m=matrices[src[j].bone],*p=src[j].pos;
                d->u=src[j].u;d->v=src[j].v;d->colour=gfo_colour(&progress,src[j].reveal,1);
                d->x=m[0]*p[0]+m[1]*p[1]+m[2]*p[2]+m[3];
                d->y=m[4]*p[0]+m[5]*p[1]+m[6]*p[2]+m[7];
                d->z=m[8]*p[0]+m[9]*p[1]+m[10]*p[2]+m[11];
            }
            ge_submit_aux(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,(end-first)*3,0,v);first=end;
        }
    }
    sceGuDepthOffset(0);
    sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);sceGuEnable(GU_CULL_FACE);sceGuDisable(GU_TEXTURE_2D);
}
