#include "hud_layout.h"
#include "pause_hud.h"
#include "charpose.h"
#include <pspgu.h>
typedef struct {int model,type,selected,intro;float x,y,z,scale;} PauseEntry;
#if JGR_PORTABLE_ASSETS
#include "source_data_pause_hud_entries.inc"
#else
#include "generated/source_data/pause_hud_entries.inc"
#endif
static void matrix(int mode,const float v[16])
{ScePspFMatrix4 m;float *p=(float*)&m;for(int r=0;r<4;r++)for(int c=0;c<4;c++)p[c*4+r]=v[r*4+c];sceGuSetMatrix(mode,&m);}
void pause_hud_draw(Chars *bank,const PauseHud *h,int stage,int allow_quit,const float projection[16],const float view[16],int map_depth)
{
    if(!bank->h || !h->active || (unsigned)stage>=6)return;
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    /* Share the map's tight 16-bit depth interval; neither its original
       thin outlines nor the nearer animated icons may collapse/clip. */
    const float near=.3f,far=1.f;
    /* 034760 source screen centre (350,230), focal 500. */
    float hp[16]={500.f/320.f,0,1.f-700.f/640.f,0,0,500.f/240.f,-(1.f-460.f/480.f),0,
        0,0,(far+near)/(near-far),2.f*far*near/(near-far),0,0,-1,0};
    hud_projection(hp,HUD_CENTRE);
    bank->drawn_tris=0;matrix(GU_VIEW,identity);matrix(GU_PROJECTION,hp);
    if(!map_depth)sceGuClear(GU_DEPTH_BUFFER_BIT);
    sceGuDepthFunc(GU_GEQUAL);
    for(const PauseEntry *e=PAUSE_ENTRIES[stage];e->model>=0;e++){
        if(e->type==1 && !allow_quit)continue;
        float x=e->x,y=e->y,z=e->z,s=e->scale;
        float sx=s,sy=s;
        if(e->type==5)x+=h->volume*.45f-.25f;
        /* 0348dc / 034928: type 2 is the radio, whose position and
           nonuniform size respond to BGM volume. */
        if(e->type==2){x+=.1f;y+=.09f;sx*=h->volume*.55f+.3f;sy*=h->volume*.5f+.5f;}
        int clip=h->phase==1 ? e->intro:e->selected;
        /* Null-motion 02f064 uses doubled reciprocal depth even when
           it retains the final pose written by the intro motion. */
        chars_hud_depth(clip<0 ? 2:1,clip<0 ? 0:1);
        float frame=h->phase==1 || e->type==h->selection || e->type==4 ? h->clock%30:0;
        /* NJS motion drawing mutates the original node tree. A later
           null-motion draw retains the last intro pose; it does NOT
           restore the off-screen asset bind pose. */
        if(h->phase!=1 && clip<0 && e->intro>=0){clip=e->intro;frame=29;}
        const float root[16]={sx,0,0,x,0,sy,0,y,0,0,s,z,0,0,0,1};
        const CharLight light={{1,1,1},1.f/s,.5f/s,{0xffffffff,0xffffffff},1};
        chars_scene_lighting(&light,NULL);chars_draw_material(bank,e->model,root,NULL,clip,frame,1,NULL);
    }
    chars_hud_depth(1,0);chars_scene_lighting(NULL,NULL);sceGuDepthMask(GU_FALSE);sceGuDepthFunc(GU_GEQUAL);
    matrix(GU_PROJECTION,projection);matrix(GU_VIEW,view);matrix(GU_MODEL,identity);
}
