#include "hud_layout.h"
#include "countdown_hud.h"
#include <pspgu.h>
static void countdown_matrix(int mode,const float v[16])
{
    ScePspFMatrix4 m;float *out=(float *)&m;
    for(int r=0;r<4;r++)for(int c=0;c<4;c++)out[c*4+r]=v[r*4+c];
    sceGuSetMatrix(mode,&m);
}
void countdown_hud_draw(Chars *models,int model,const float projection[16],const float view[16])
{
    if(!models || !models->h || models->h->n_chars!=4 || (unsigned)model>=4)return;
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const float root[16]={.035f,0,0,0,0,.035f,0,0,0,0,.035f,-2,0,0,0,1};
    const float near=.5f,far=1000;
    float hp[16]={415.f/320.f,0,0,0,0,415.f/240.f,0,0,
        0,0,(far+near)/(near-far),2.f*far*near/(near-far),0,0,-1,0};
    hud_projection(hp,HUD_CENTRE);
    const CharLight light={{1,1,1},1,.5f,{0xffffffff,0xffffffff},1};
    models->drawn_tris=0;
    countdown_matrix(GU_VIEW,identity);countdown_matrix(GU_PROJECTION,hp);
    sceGuClear(GU_DEPTH_BUFFER_BIT);sceGuDepthFunc(GU_GEQUAL);
    chars_scene_lighting(&light,NULL);chars_draw_material(models,model,root,NULL,-1,0,1,NULL);
    chars_scene_lighting(NULL,NULL);sceGuDepthMask(GU_FALSE);sceGuDepthFunc(GU_GEQUAL);
    countdown_matrix(GU_PROJECTION,projection);countdown_matrix(GU_VIEW,view);countdown_matrix(GU_MODEL,identity);
}
