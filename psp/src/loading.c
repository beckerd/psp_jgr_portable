#include "loading.h"
#include "hud_layout.h"
#include <pspgu.h>
static void matrix(int mode,const float v[16])
{ScePspFMatrix4 m;float *p=(float*)&m;for(int r=0;r<4;r++)for(int c=0;c<4;c++)p[c*4+r]=v[r*4+c];sceGuSetMatrix(mode,&m);}
int loading_load(Chars *b)
{
    if(chars_load(b,"loading.jgc")){chars_free(b);return -1;}
    if(b->h->n_chars!=2 || b->h->n_tex || b->h->n_motions!=2)goto invalid;
    for(int i=0;i<2;i++)if(b->chars[i].nmotions!=1 || b->motions[b->chars[i].motion0].nframes!=101)goto invalid;
    return 0;
invalid:chars_free(b);return -1;
}
void loading_draw(Chars *b,const LoadingHud *h,float focal)
{
    if(!b->h)return;
    /* 01733c: identity camera, angle 3c71, root translation (0,0,-80),
       normal RGB/direction (1,1,1), diffuse 1, ambient .5.
       0ba244 installs 0ba260, which adds screen-space (0,32) after
       projection. This is not a world-space root translation. */
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const float near=1,far=60000;
    float projection[16]={focal/320.f,0,0,0,0,focal/240.f,32.f/240.f,0,
        0,0,(far+near)/(near-far),2.f*far*near/(near-far),0,0,-1,0};
    const float root[16]={1,0,0,0,0,1,0,0,0,0,1,-80,0,0,0,1};
    const CharLight light={{1,1,1},1,.5f,{0xffffffff,0xffffffff},1};
    hud_projection(projection,HUD_CENTRE);
    matrix(GU_VIEW,identity);matrix(GU_PROJECTION,projection);chars_environment_view(identity);
    chars_scene_lighting(&light,NULL);b->drawn_tris=0;
    for(int i=0;i<2;i++)chars_draw_material(b,i,root,NULL,0,h->frame[i],1,NULL);
    chars_scene_lighting(NULL,NULL);matrix(GU_MODEL,identity);
}
