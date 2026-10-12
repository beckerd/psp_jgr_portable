#include "hud_layout.h"
#include "police_hud.h"
#include <pspgu.h>
#include <math.h>
#include <string.h>
static void matrix(int mode,const float values[16])
{
    ScePspFMatrix4 m;float *out=(float *)&m;
    for(int r=0;r<4;r++)for(int c=0;c<4;c++)out[c*4+r]=values[r*4+c];
    sceGuSetMatrix(mode,&m);
}
void police_hud_draw_alert(PoliceHud *h,int level,float frame,const PoliceArrow *arrow,float arrow_frame,
                           const float projection[16],const float view[16])
{
    float sample;int sign=police_hud_sign(level,frame,&sample);CharPose poses[64];
    int draw_arrow=arrow && (level==0 || level==1 || level==3);
    if(!h || !h->models.h || (sign<0 && !draw_arrow))return;
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const float root[16]={.04f,0,0,0,0,.04f,0,0,0,0,.04f,-1.2f,0,0,0,1};
    /* 049684/049940: original focal=415, 640x480, centre=(320,150),
       translation -1.2 and scale .04. Source vertices/XY are unchanged.
       PSP-only independent depth mapping: the .01 near plane quantizes
       RUN/OK's raised lettering into its backing at 16-bit precision.
       ALWAYS is also incorrect: later rear faces overwrite the letters. */
    const float near=.5f,far=1000;
    float hp[16]={415.f/320.f,0,0,0,0,415.f/240.f,-(1.f-300.f/480.f),0,
        0,0,(far+near)/(near-far),2.f*far*near/(near-far),0,0,-1,0};
    hud_projection(hp,HUD_CENTRE);
    matrix(GU_VIEW,identity);
    sceGuClear(GU_DEPTH_BUFFER_BIT);sceGuDepthFunc(GU_GEQUAL);
    if(draw_arrow && police_hud_pose(h,3,arrow_frame,poses)>0) {
        /* 049750: centre (320,70), .03 scale, original root X=0x4000.
           2346c8 stores raw (1,1,1), 234700 diffuse 1/ambient .5,
           and 23470e sets white RGB. Do not normalize this direction. */
        const CharLight light={{1,1,1},1,.5f,{0xffffffff,0xffffffff},1};
        hp[6]=-(1.f-140.f/480.f);matrix(GU_PROJECTION,hp);
        chars_scene_lighting(&light,NULL);
        chars_draw_material(&h->models,3,arrow->root,poses,-1,0,1,NULL);
        chars_scene_lighting(NULL,NULL);
    }
    if(sign>=0 && police_hud_pose(h,sign,sample,poses)>0) {
        hp[6]=-(1.f-300.f/480.f);matrix(GU_PROJECTION,hp);
        chars_draw_material(&h->models,sign,root,poses,-1,0,1,NULL);
    }
    /* This draw runs AFTER world objects. The old sign pass left a HUD
       camera active for gang members, helicopter wrecks and pickups. */
    sceGuDepthMask(GU_FALSE);sceGuDepthFunc(GU_GEQUAL);
    matrix(GU_PROJECTION,projection);matrix(GU_VIEW,view);matrix(GU_MODEL,identity);
}
void police_hud_draw(PoliceHud *h,int level,float frame,const float projection[16],const float view[16])
{ police_hud_draw_alert(h,level,frame,NULL,0,projection,view); }
