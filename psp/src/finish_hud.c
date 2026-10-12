#include "finish_hud.h"
#include "charpose.h"
#include "hud_layout.h"
#include <pspgu.h>
static void matrix(int mode,const float v[16])
{ScePspFMatrix4 m;float *p=(float *)&m;for(int r=0;r<4;r++)for(int c=0;c<4;c++)p[c*4+r]=v[r*4+c];sceGuSetMatrix(mode,&m);}
void finish_hud_draw(Chars *b,const FinishHud *h,const float projection[16],const float view[16])
{
    if(!h->life || !b->h)return;
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const float root[16]={.025f,0,0,0,0,.025f,0,-.3f,0,0,.025f,-1.2f,0,0,0,1};
    const float near=.5f,far=1000;
    float hp[16]={500.f/320.f,0,0,0,0,500.f/240.f,0,0,0,0,(far+near)/(near-far),2*far*near/(near-far),0,0,-1,0};
    const CharLight light={{1,1,1},1,1,{0xffffffff,0xffffffff},1};
    hud_projection(hp,HUD_CENTRE);matrix(GU_VIEW,identity);matrix(GU_PROJECTION,hp);
    sceGuClear(GU_DEPTH_BUFFER_BIT);sceGuDepthFunc(GU_GEQUAL);
    chars_scene_lighting(&light,NULL);chars_draw_material(b,0,root,NULL,0,h->frame,1,NULL);
    chars_scene_lighting(NULL,NULL);sceGuDepthMask(GU_FALSE);sceGuDepthFunc(GU_GEQUAL);
    matrix(GU_PROJECTION,projection);matrix(GU_VIEW,view);matrix(GU_MODEL,identity);
}
