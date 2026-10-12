#include "ge_submit.h"
#include "spray_bubble.h"
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
typedef struct {float u,v;unsigned colour;float x,y,z;} BubbleVertex;
typedef struct {unsigned vertex,index,count,texture,flags;} BubbleBatch;
#if JGR_PORTABLE_ASSETS
#include "source_data_bubble.inc"
#else
#include "generated/source_data/spray_bubble_mesh.inc"
#endif
static void bubble_view(const float *v)
{
    ScePspFMatrix4 m;float *p=(float *)&m;
    for(int r=0;r<4;r++)for(int c=0;c<4;c++)p[c*4+r]=v[r*4+c];sceGuSetMatrix(GU_VIEW,&m);
}
void spray_bubble_draw(const SprayBubble *b,const unsigned short *const art[3],const float view[16])
{
#if JGR_PORTABLE_ASSETS
    if(!source_data_bubble_ready())return;
#endif
    if(!b || b->scale<=0 || (unsigned)b->kind>3)return;
    unsigned kind=b->kind ? b->kind-1:0;if(!art[kind])return;
    float head[3];
    for(int r=0;r<3;r++){double n=(double)view[r*4]*b->head[0];n+=(double)view[r*4+1]*b->head[1];n+=(double)view[r*4+2]*b->head[2];n+=view[r*4+3];head[r]=(float)n;}
    if(head[2]>=-5 || head[0]*head[0]+head[1]*head[1]+head[2]*head[2]>500.f*500.f)return;
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};bubble_view(identity);
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGuEnable(GU_DEPTH_TEST);sceGuDisable(GU_ALPHA_TEST);
    /* 02f314 leaves projected XY alone but replaces reciprocal Z with
       -1/(head_source_z-2). PSP view uses the opposite Z convention. */
    float depth=head[2]+2;
    for(int pass=0;pass<2;pass++)for(unsigned j=0;j<2;j++) {
        const BubbleBatch *batch=bubble_batches[kind]+j;int alpha=!!(batch->flags&1);if(alpha!=pass)continue;
        if(alpha){sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);}else sceGuDisable(GU_BLEND);
        sceGuDepthMask(alpha ? GU_TRUE:GU_FALSE);
        /* Original outline strips face the camera; avoid a reflection-
           dependent stage cull setting leaking into this billboard. */
        sceGuDisable(GU_CULL_FACE);
        if(batch->texture==65535)sceGuDisable(GU_TEXTURE_2D);
        else {
            unsigned width=128u<<kind;sceGuEnable(GU_TEXTURE_2D);
            sceGuTexMode(GU_PSM_4444,0,0,0);sceGuTexImage(0,width,128,width,art[kind]);
            sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_REPEAT,GU_REPEAT);
        }
        BubbleVertex *out=sceGuGetMemory(batch->count*sizeof *out);
        for(unsigned k=0;k<batch->count;k++) {
            const BubbleVertex *v=bubble_vertices[kind]+batch->vertex+bubble_indices[kind][batch->index+k];
            float z=head[2]+v->z,ratio=depth/z;out[k]=*v;
            out[k].x=(head[0]+v->x*b->scale)*ratio;out[k].y=(head[1]+v->y*b->scale)*ratio;out[k].z=depth;
        }
        ge_submit_aux(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,batch->count,0,out);
    }
    sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);sceGuDisable(GU_TEXTURE_2D);sceGuEnable(GU_CULL_FACE);bubble_view(view);
}
