#include "ge_submit.h"
#include <pspgu.h>
#include "pause_wipe.h"
/* 04c75c/04c7f4 -> 2727fc: 19x19 coloured strips, TL/BL/TR/BR.
   Keep floating screen coordinates and the same BL--TR diagonal. */
void pause_wipe_draw(const PauseWipe *w)
{
    typedef struct {unsigned color;float x,y,z;} Vertex;
    Vertex *v=sceGuGetMemory(400*sizeof(*v));
    unsigned short *ix=sceGuGetMemory(19*19*6*sizeof(*ix));int n=0;
    for(int k=0;k<400;k++)v[k]=(Vertex){w->grid[k].color,w->grid[k].x*.75f,w->grid[k].y*(272.f/480.f),0};
    for(int y=0;y<19;y++)for(int x=0;x<19;x++){
        int k=y*20+x;ix[n++]=k;ix[n++]=k+20;ix[n++]=k+1;
        ix[n++]=k+1;ix[n++]=k+20;ix[n++]=k+21;
    }
    sceGuDisable(GU_DEPTH_TEST);sceGuDepthMask(GU_TRUE);sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_TEXTURE_2D);sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    ge_submit_aux(GU_TRIANGLES,GU_COLOR_8888|GU_VERTEX_32BITF|GU_INDEX_16BIT|GU_TRANSFORM_2D,n,ix,v);
    /* Leaves the caller's 2-D overlay scope selected, like gfx_mode2d. */
}
