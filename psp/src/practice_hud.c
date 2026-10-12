#include "hud_layout.h"
#include "practice_hud.h"
#include "charpose.h"
#include <pspgu.h>
typedef struct {int model,selected,intro;float x,y,z,scale;} PracticeEntry;
#if JGR_PORTABLE_ASSETS
#include "source_data_practice_hud_entries.inc"
#else
#include "generated/source_data/practice_hud_entries.inc"
#endif
void practice_hud_free(PracticeHud *p)
{chars_free(&p->models);tex2d_free(&p->textures);list2d_free(&p->lists);}
int practice_hud_load(PracticeHud *p)
{
    if(p->models.h)return 0;
    if(chars_load(&p->models,"practicehud.jgc") || p->models.h->n_chars!=3 ||
       tex2d_load(&p->textures,"practicehud_tex.bin") || list2d_load(&p->lists,"practicehud_list.bin")){
        practice_hud_free(p);return -1;
    }
    return 0;
}
static void matrix(int mode,const float v[16])
{ScePspFMatrix4 m;float *p=(float*)&m;for(int r=0;r<4;r++)for(int c=0;c<4;c++)p[c*4+r]=v[r*4+c];sceGuSetMatrix(mode,&m);}
void practice_hud_draw(PracticeHud *p,const PauseHud *h,const float projection[16],const float view[16])
{
    if(!p->models.h || !h->active)return;
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const float near=.5f,far=1000;
    float hp[16]={500.f/320.f,0,1.f-700.f/640.f,0,0,500.f/240.f,-(1.f-460.f/480.f),0,
        0,0,(far+near)/(near-far),2.f*far*near/(near-far),0,0,-1,0};
    hud_projection(hp,HUD_CENTRE);
    matrix(GU_VIEW,identity);matrix(GU_PROJECTION,hp);sceGuClear(GU_DEPTH_BUFFER_BIT);sceGuDepthFunc(GU_GEQUAL);
    p->models.drawn_tris=0;
    /* 0358ac..c8 uses raw light (no reciprocal model-scale adjustment).
       This is 234fda/0b8b1c, NOT the common map menu's reciprocal-depth path. */
    const CharLight light={{1,1,1},1,.5f,{0xffffffff,0xffffffff},1};
    chars_scene_lighting(&light,NULL);
    for(int i=0;i<3;i++){
        const PracticeEntry *e=PRACTICE_ENTRIES+i;float s=e->scale;
        const float root[16]={s,0,0,e->x,0,s,0,e->y,0,0,s,e->z,0,0,0,1};
        chars_draw_material(&p->models,i,root,NULL,h->pose_clip[i],h->pose_frame[i],1,NULL);
    }
    chars_scene_lighting(NULL,NULL);sceGuDepthMask(GU_FALSE);sceGuDepthFunc(GU_GEQUAL);
    matrix(GU_PROJECTION,projection);matrix(GU_VIEW,view);matrix(GU_MODEL,identity);
}
