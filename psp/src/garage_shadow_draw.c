#include "ge_submit.h"
#include "garage_shadow.h"
#include <pspgu.h>
#include <string.h>
/* PSP adapter for the original finite, closed modifier volumes. Accumulated
   signed depth-fail counts produce their union without darkening overlaps
   repeatedly. Alpha stores stencil in the 8888 render buffer. Run after
   the receiving room and BEFORE residents: original mode-0 PCW 808c000a
   has Shadow=0, so the dancers must not receive this darkening. */
void garage_shadow_begin(void)
{
    const float values[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};ScePspFMatrix4 identity;memcpy(&identity,values,64);
    sceGuSetMatrix(GU_MODEL,&identity);
    sceGuSetMatrix(GU_VIEW,&identity);
    sceGuClearStencil(128);sceGuClear(GU_STENCIL_BUFFER_BIT);
    sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_DEPTH_TEST);sceGuDepthMask(GU_TRUE);sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_STENCIL_TEST);sceGuStencilFunc(GU_ALWAYS,128,255);
    sceGuPixelMask(0x00ffffff);sceGuEnable(GU_CULL_FACE);
}
void garage_shadow_submit(void *context,const float (*vertices)[3],unsigned nv,const uint16_t *indices,unsigned ni)
{
    (void)context;
    float (*v)[3]=sceGuGetMemory(nv*12);unsigned short *ix=sceGuGetMemory(ni*2);
    memcpy(v,vertices,nv*12);memcpy(ix,indices,ni*2);
    const int format=GU_VERTEX_32BITF|GU_INDEX_16BIT|GU_TRANSFORM_3D;
    sceGuFrontFace(GU_CW);sceGuStencilOp(GU_KEEP,GU_INCR,GU_KEEP);
    ge_submit_aux(GU_TRIANGLES,format,ni,ix,v);
    sceGuFrontFace(GU_CCW);sceGuStencilOp(GU_KEEP,GU_DECR,GU_KEEP);
    ge_submit_aux(GU_TRIANGLES,format,ni,ix,v);
}
void garage_shadow_end(const float view[16])
{
    typedef struct {float x,y,z;} Point;
    Point *quad=sceGuGetMemory(2*sizeof *quad);quad[0]=(Point){0,0,0};quad[1]=(Point){480,272,0};
    sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_CULL_FACE);
    sceGuPixelMask(0xff000000);sceGuStencilFunc(GU_NOTEQUAL,128,255);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);
    /* Original FPU_SHAD_SCALE = 0x180: resident surface RGB times 128/256.
       The PSP's 8-bit blend factor is its hardware quantization adapter. */
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,0,0x808080);sceGuColor(0);
    ge_submit_aux(GU_SPRITES,GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,0,quad);
    sceGuPixelMask(0);sceGuDisable(GU_STENCIL_TEST);sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);sceGuDepthMask(GU_FALSE);sceGuFrontFace(GU_CW);sceGuEnable(GU_CULL_FACE);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    ScePspFMatrix4 matrix;float *m=(float*)&matrix;
    for(int r=0;r<4;r++)for(int c=0;c<4;c++)m[c*4+r]=view[r*4+c];
    sceGuSetMatrix(GU_VIEW,&matrix);
}
