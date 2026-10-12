/* 0361ec / 036978 / 0369dc result sequencer. Class 1 recruitment and
   the other five source classes share the same tally/fade controller.
   Record insertion, destination selection and artwork remain consumers. */
#ifndef JGR_RECRUIT_RESULT_H
#define JGR_RECRUIT_RESULT_H
#include <string.h>
#include "hud_state.h"
#include "hud_layout.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_result_hud_text.inc"
#else
#include "generated/source_data/result_hud_text.inc"
#endif
static inline void result_hud_number(char out[24],int value)
{
    unsigned n=value<0 ? 0u-(unsigned)value:(unsigned)value;char reverse[20];int k=0,at=0;
    do{reverse[k++]=n%10;n/=10;}while(n);
    if(value<0){out[at++]=RESULT_DIGITS[10][0];out[at++]=RESULT_DIGITS[10][1];}
    while(k){int digit=reverse[--k];out[at++]=RESULT_DIGITS[digit][0];out[at++]=RESULT_DIGITS[digit][1];}out[at]=0;
}
typedef struct {
    int active,phase,clock,count,done,health,timer,alpha;
    float fade;
    int fading;
    int class_id,max_health,total_ticks,inventory,life_bonus,time_bonus,can_bonus;
    int displayed,total,draw_clock;
    int record_place,new_record,record_clock,souls,joined;
} RecruitResult;

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_recruit_result_h__result_flags_0.inc"
#else

#include "generated/source_data/recruit_result_h__RESULT_FLAGS_0.inc"

#endif

static inline void recruit_result_start(RecruitResult *r,int health,int timer)
{ memset(r,0,sizeof *r);r->active=1;r->health=health;r->timer=timer;r->class_id=1;r->inventory=-1;r->record_place=5;r->joined=-1; }
static inline void result_hud_start(RecruitResult *r,int kind,int score,int health,int maximum,int timer,int total_ticks,int cans)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_recruit_result_h__classes_0.inc"
#else

#include "generated/source_data/recruit_result_h__classes_0.inc"

#endif

    recruit_result_start(r,health,timer);r->class_id=(unsigned)kind<10 ? classes[kind]:5;
    r->max_health=maximum;r->total_ticks=total_ticks;r->inventory=cans;r->displayed=r->total=score;
}
static inline int result_hud_visible(const RecruitResult *r)
{return r->phase==0xc || r->phase==0xd || r->phase==0xe || r->phase==0xf || r->phase==0x10 || r->phase==0x11 || r->phase==0x14 || r->phase==0x1f;}
static inline int result_hud_record_visible(const RecruitResult *r)
{return r->new_record && (r->phase==0xe || r->phase==0xf || r->phase==0x1f);}
/* 0360d8 snapshots the per-mission count, not the lifetime unlock total.
   Joined is 038518's result branch, NOT the active playable model id. */
static inline void result_hud_context(RecruitResult *r,int place,int souls,int joined)
{r->record_place=place;r->souls=souls;r->joined=joined;}
static inline void result_hud_present(RecruitResult *r)
{if(r->active && result_hud_visible(r)){if(result_hud_record_visible(r))r->record_clock++;r->draw_clock++;}}
static inline int result_hud_score_y(const RecruitResult *r)
{return RESULT_FLAGS[r->class_id][6] && r->souls>0 ? 168:200;}
static inline float result_hud_text_x(int source_width)
{return hud_x((int)((640.f-source_width)*.5f),HUD_CENTRE);}
static inline const char *result_hud_joined_name(const RecruitResult *r,int language)
{return RESULT_FLAGS[r->class_id][7] && r->joined>0 && r->joined<15 ? RESULT_NAMES[language!=0][r->joined]:NULL;}
/* Called BEFORE the update: A/automatic departure bypass the sound branch.
   0e6354(mode=9,parent=0,pos=0,bank=6,program=11,-1), not bank 9. */
static inline int result_hud_voice(const RecruitResult *r,unsigned character,int confirm)
{
    if(!r->active || character>=16 || (r->phase!=0xc && r->phase!=0xd) || confirm || (r->count>160 && r->done))return -1;
    return (unsigned)r->count==RESULT_VOICE_DELAY[r->phase==0xd][character] ? 11:-1;
}
/* One original 30-Hz parent callback. Return 1 at the scene/destination
   request boundary; return 2 after the requested EVENT has retired. */
static inline int recruit_result_step(RecruitResult *r,int confirm,int event_alive)
{
    int action=0;
    if(!r->active)return 0;
    switch(r->phase) {
    case 0:r->phase=3;break;
    case 3:r->phase=4;break;
    case 4:r->alpha=(r->alpha+8)&255;if(r->alpha>=120){r->alpha=128;r->phase=0x17;}break;
    case 0x17:
        if(RESULT_FLAGS[r->class_id][4]){
            if(r->class_id==4){r->time_bonus=r->total=r->timer=r->total_ticks-r->timer;r->phase=0xd;r->new_record=r->record_place==0;break;}
            r->time_bonus=(r->timer/30)*30;r->total+=r->time_bonus;
        }
        r->phase=0x15;break;
    case 0x15:
        if(RESULT_FLAGS[r->class_id][2] && r->max_health>0){
            r->life_bonus=(int)(((float)r->health/(float)r->max_health)*100.f)*30;
            if(r->health==r->max_health)r->life_bonus+=3000;
            r->total+=r->life_bonus;
        }
        r->phase=0x19;break;
    case 0x19:
        if(RESULT_FLAGS[r->class_id][3]){r->can_bonus=r->inventory*100;r->total+=r->can_bonus;}
        r->new_record=r->class_id>=2 && r->class_id<=4 && r->record_place==0;
        r->phase=0xc;break;
    case 0xc:
        if(confirm || (r->count>160 && r->done)) {
            r->health=r->timer=0;r->inventory=0;r->displayed=r->total;r->count=0;r->phase=0x1f;r->draw_clock=200;
        } else {
            r->done=1;
            if(r->life_bonus>0 && RESULT_FLAGS[r->class_id][2]){
                int n=r->life_bonus<60 ? r->life_bonus:60;r->displayed+=n;r->life_bonus-=n;r->done=0;
            }
            if(r->health>0){r->health--;r->done=0;}else r->health=0;
            /* Source deliberately does not clear +9c for the time bonus. */
            if(r->time_bonus>0 && RESULT_FLAGS[r->class_id][4]){
                int n=r->time_bonus<90 ? r->time_bonus:90;r->displayed+=n;r->time_bonus-=n;
            }
            if(r->timer>150){r->timer-=150;r->done=0;}else r->timer=0;
            if(r->can_bonus>0 && r->clock%5==0 && RESULT_FLAGS[r->class_id][3]){
                int n=r->can_bonus<100 ? r->can_bonus:100;r->displayed+=n;r->can_bonus-=n;
                r->inventory=r->inventory>0 ? r->inventory-1:0;r->done=0;
            }
            r->count++;
        }
        break;
    case 0xd:
        if(confirm || (r->count>160 && r->done)){r->count=0;r->phase=0x1f;r->draw_clock=200;}
        else {r->done=1;r->count++;}
        break;
    case 0x1f:
        if(r->count>45){r->fade=0;r->fading=1;r->phase=0xe;}
        r->count++;break;
    case 0xe:
        if(!r->fading){r->phase=0x1e;action=1;}break;
    case 0x1e:
        if(!event_alive)r->phase=0xf;
        break;
    case 0xf:action=2;break;
    }
    r->clock++;
    return action;
}
/* 02106c runs as a child after the result parent, including the tick on
   which 036978 creates it. Strict >1 endpoint, not >=1 or a timer. */
static inline void recruit_result_fade_step(RecruitResult *r)
{
    if(r->fading){r->fade+=0.04f;if(r->fade>1.f){r->fade=1.f;r->fading=0;}}
}
/* 036644 drains actor +8b8 and the resident task-21 timer, which the
   ordinary HUD tasks read. Keep these presentation counters separate on
   PSP so tallying a completed mission cannot dispatch a new player death.
   Run once after each result callback, even when actor motion is frozen. */
static inline void recruit_result_hud_tick(const RecruitResult *r,HealthHud *h,int low)
{
    if(!r->active)return;
    /* 036694..03669c: leaving tally clears the resident health task's
       target AND displayed values, including an A-skipped tally. */
    if(r->phase==0x1f && !r->count)h->target=h->displayed=0;
    health_hud_tick(h,r->health,low,0);
}
static inline int recruit_result_hud_time(const RecruitResult *r,int live)
{ return r->active ? r->timer:live; }
static inline void recruit_result_cans(const RecruitResult *r,int *inventory)
{
    if(!r->active || !inventory)return;
    /* 03683a..036866 bypass 0368b0 when the can bonus is disabled or
       empty. Class 1 keeps its cans until 036684 clears the tally. */
    if(r->inventory>=0)*inventory=r->inventory;
}
#endif
