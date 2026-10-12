#ifndef JGR_POLICE_HUD_H
#define JGR_POLICE_HUD_H
#include "charpose.h"
#include "scene.h"
#include "police_arrow.h"
typedef struct { Chars models; unsigned char *image; Scene source; unsigned motion[4]; } PoliceHud;
int police_hud_load(PoliceHud *,const char *mesh,const char *motion);
void police_hud_free(PoliceHud *);
int police_hud_pose(const PoliceHud *,int level,float frame,CharPose poses[64]);
/* Independent sign depth layer; call after all world drawing. */
void police_hud_draw(PoliceHud *,int level,float frame,const float projection[16],const float view[16]);
void police_hud_draw_alert(PoliceHud *,int level,float frame,const PoliceArrow *,float arrow_frame,
                           const float projection[16],const float view[16]);
/* 049c7c: levels 3..5 retain the previous sign/motion pointer, not a
   new generic thirty-frame animation. Level zero uses GAME +58 (30),
   while levels 1/2 use +64/+6c (60). Motion -1 means pointer cleared. */
static inline void police_hud_select(int level,int *motion,int *phase,float *frame)
{
    if((unsigned)level<3)*motion=level;
    *phase=1;*frame=0;
}
/* 049590: the directional arrow loops independently at 30. Sign playback
   clears the motion at its end WITHOUT resetting +a8; phase 2 continues
   the clock for levels below four. Drawing then holds the last posed mesh. */
static inline void police_hud_tick(int level,int *motion,int *phase,float *frame,float *arrow)
{
    if(*phase==1) {
        *frame+=1;
        if(*motion>=0 && *frame>=(*motion==0 ? 30.f:60.f)){*motion=-1;*phase=2;}
    } else if(*phase==2 && (unsigned)level<4)*frame+=1;
    *arrow+=1;if(*arrow>=30)*arrow=0;
}
/* 049684/049940 screen-centre arguments, translation and uniform scale.
   The directional arrow (049750) is a separate draw, not this sign. */
static inline int police_hud_sign(int level,float frame,float *sample)
{
    if((unsigned)level>=3)return -1;
    float last=level==0 ? 29:59;*sample=frame<last ? frame:last;
    return level;
}
#endif
