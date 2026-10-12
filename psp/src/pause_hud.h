#ifndef JGR_PAUSE_HUD_H
#define JGR_PAUSE_HUD_H
#include "charanim.h"
#include <math.h>
/* 033844 / special 03527c controllers and parent 018a0c START debounce. */
typedef struct {
    int active,phase,clock,parent_clock,selection;unsigned angle;float volume;
    int stick_angle;float stick_mag;
    /* Per-update original bank-0 program / 0b23c4 dispatch, -1 if absent. */
    int sound,volume_command;
    int practice,resume_latch;
    int pose_clip[3],pose_frame[3];
} PauseHud;
static inline void pause_hud_init(PauseHud *h,float volume)
{*h=(PauseHud){.active=1,.volume=volume,.sound=-1,.volume_command=-1,.pose_clip={-1,-1,-1}};}
static inline void pause_hud_present(PauseHud *h)
{
    if(!h->practice)return;
    /* The NJS draw mutates nodes. An unselected/null-motion object keeps
       its last sampled pose, including a formerly selected loop frame. */
    for(int i=0;i<3;i++)if(h->phase==1 || (i<2 && h->selection==i)){
        h->pose_clip[i]=h->phase==1 ? (i<2 ? 1:0):0;h->pose_frame[i]=h->clock%30;
    }
}
/* DC input bits. Return 1 resume, 2 quit request. Commands stay disabled
   during the 30-frame menu introduction; parent START unlocks after 15. */
static inline int pause_hud_update_analog(PauseHud *h,unsigned held,unsigned edge,int allow_quit,
                                        int angle,float magnitude,float (*sin_bam)(int))
{
    float s=sin_bam ? sin_bam(angle):0,c=sin_bam ? sin_bam(angle+16384):1;
    float ps=sin_bam ? sin_bam(h->stick_angle):0,pc=sin_bam ? sin_bam(h->stick_angle+16384):1;
    float pm=h->stick_mag;
    /* 033f44 stores analog history even during introduction/confirmation. */
    h->stick_angle=angle;h->stick_mag=magnitude;h->sound=h->volume_command=-1;
    h->clock++;h->angle+=0x71c;
    switch(h->phase){
    case 0:h->clock=0;h->phase=1;break;
    case 1:if(h->clock>=30)h->phase=2;break;
    case 2:
        if(edge&8){h->active=0;h->sound=3;return 1;}
        /* Both source menus latch controller-66's held/previous A bits.
           The physical-input adapter consumes the same confirm edge. */
        if(edge&4){if(!h->selection){h->active=0;h->sound=3;h->resume_latch=1;return 1;}if(h->selection==1){h->phase=3;h->sound=1;return 2;}break;}
        {
            /* Preserve the asymmetric previous-stick gates at 033abc.
               Held volume has no magnitude gate, and strict sin < 1
               excludes the exact positive cardinal (033b70/033cdc). */
            int horizontal=(edge&0xc0) ||
                (!(ps<.5f && pm>0) && s<-.5f && fabsf(c)<.5f && magnitude>0) ||
                (!(ps>.5f && pm>0) && s>-.5f && fabsf(c)<.5f && magnitude>0);
            int increase=(s<1 && s>.5f && fabsf(c)<.5f) || (held&0x80);
            int decrease=(s<-.5f && fabsf(c)<.5f) || (held&0x40);
            if(horizontal && h->selection!=5){
                if(allow_quit){h->selection=h->selection==1 ? 0:1;h->sound=0;}
            }else if(h->selection==5 && (horizontal || increase || decrease)){
                if(increase && h->volume<1)h->volume+=.01f;
                else if(decrease && h->volume>0)h->volume-=.01f;
                h->volume_command=(int)(h->volume*16.f);
            }else if(!h->practice && ((edge&0x30) ||
                     (!(pc>-.5f && pm>0) && c>.5f && fabsf(s)<.5f && magnitude>0) ||
                     (!(pc<.5f && pm>0) && c<-.5f && fabsf(s)<.5f && magnitude>0))){
                h->selection=h->selection==5 ? 0:5;h->sound=0;
            }
        }
        break;
    }
    return 0;
}
static inline int pause_hud_update(PauseHud *h,unsigned held,unsigned edge,int allow_quit)
{return pause_hud_update_analog(h,held,edge,allow_quit,0,0,0);}
static inline int pause_hud_tick_analog(PauseHud *h,unsigned held,unsigned edge,int allow_quit,
                                      int angle,float magnitude,float (*sin_bam)(int))
{
    h->parent_clock++;
    h->sound=h->volume_command=-1;
    if((edge&8) && h->parent_clock>15){h->active=0;h->sound=3;return 1;}
    return pause_hud_update_analog(h,held,edge,allow_quit,angle,magnitude,sin_bam);
}
static inline int pause_hud_tick(PauseHud *h,unsigned held,unsigned edge,int allow_quit)
{return pause_hud_tick_analog(h,held,edge,allow_quit,0,0,0);}
void pause_hud_draw(Chars *,const PauseHud *,int stage_class,int allow_quit,const float[16],const float[16],int map_depth);
#endif
