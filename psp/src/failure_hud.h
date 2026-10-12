#ifndef JGR_FAILURE_HUD_H
#define JGR_FAILURE_HUD_H
#include <math.h>
/* 0329ec / 032aa8 / 032ebc. Keep update and presentation distinct:
   the original +78 clock increments in BOTH during phase 1. */
typedef struct {int active,phase,alpha,selection,clock,cause,choices;int stick_angle;float stick_mag;} FailureHud;
enum { FAILURE_NONE=0,FAILURE_RETRY=8,FAILURE_GARAGE=5,FAILURE_TITLE=16 };
static inline void failure_hud_init(FailureHud *h,int cause)
{ *h=(FailureHud){1,0,0,1,0,cause,1}; }
/* Original controller bits: A=4, horizontal d-pad=0xc0. The wrapper below
   recovers the original controller's analog history gates.
   sound: bank-6 program 10/11, or menu program 0/1 encoded as 100/101. */
static inline int failure_hud_tick(FailureHud *h,unsigned edge,int horizontal,int mode,int chapter,int fade_done,int *sound)
{
    int action=0;*sound=-1;
    if(!h->active)return 0;
    switch(h->phase) {
    case 0:h->phase=1;break;
    case 1:
        if(++h->clock>90){h->clock=0;h->phase=2;}
        else if(h->clock==30)*sound=h->cause && chapter==8 ? 11:10;
        break;
    case 2:
        h->alpha=(h->alpha+4)&255;
        if(h->alpha>=124){h->alpha=128;h->phase=3;h->clock=0;}
        break;
    case 3:
        if(edge&4){*sound=101;h->phase=4;action=h->selection==0 ? FAILURE_GARAGE:h->selection==2 ? FAILURE_TITLE:FAILURE_RETRY;}
        else if(((edge&0xc0) || horizontal) && h->choices){
            h->selection=mode ? h->selection==0:(h->selection==2 ? 1:2);*sound=100;
        }
        break;
    case 4:if(fade_done)h->phase=5;break;
    case 5:h->active=0;break;
    }
    return action;
}
static inline int failure_hud_tick_analog(FailureHud *h,unsigned edge,int mode,int chapter,int fade_done,
                                         int angle,float magnitude,float (*sine)(int),int *sound)
{
    /* 032d08..032da6: strict, asymmetric previous-stick half-plane
       checks; not a held-axis repeat or a guessed radial direction test. */
    float previous=sine(h->stick_angle),s=sine(angle),c=sine(angle+16384);
    int horizontal=(!(previous<.5f && h->stick_mag>0) && s<-.5f && fabsf(c)<.5f && magnitude>0) ||
                   (!(previous>.5f && h->stick_mag>0) && s>-.5f && fabsf(c)<.5f && magnitude>0);
    int action=failure_hud_tick(h,edge,horizontal,mode,chapter,fade_done,sound);
    /* 032e70 also records history during the introduction/confirmation. */
    h->stick_angle=angle;h->stick_mag=magnitude;return action;
}
static inline void failure_hud_present(FailureHud *h)
{h->clock++;}
/* 032f60: cause=1 is TIME OVER for normal stages, LOSE for races,
   FAILED for trials. A health failure normally chooses BUSTED. */
static inline int failure_hud_heading(int kind,int cause)
{
    if(kind==5 || kind==7 || kind==8)return 1;
    if(cause==1){if(kind==2)return 2;if(kind==0 || kind==1 || kind==3 || kind==4)return 3;}
    return 0;
}
#endif
