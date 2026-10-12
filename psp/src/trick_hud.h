#ifndef JGR_TRICK_HUD_H
#define JGR_TRICK_HUD_H
#include <stdint.h>
#include <string.h>
#include "hud_text.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_trick_hud_tables.inc"
#else
#include "generated/source_data/trick_hud_tables.inc"
#endif

/* 05b1b4 / 05b0ac own 118..11b labels and 11c..126 numbers/bonus
   labels. Marked labels still occupy a slot until the task walker visits
   them. Numbers commit their score from 05b568, including after a crash. */
typedef struct {
    int type,deleted,phase,value,kind,clock,hold;
    float x,y;
    unsigned order;
} TrickTask;
typedef struct {
    TrickTask task[15];
    unsigned serial,retired;
    int blocked; /* source 2ae160 / actor +74 & 2000; not HUD_HIDE_SCORE */
} TrickHud;

static inline const char *trick_hud_name(int kind,int id)
{return (unsigned)kind<7 && (unsigned)id<30 ? TRICK_NAMES[kind][id]:NULL;}
static inline TrickTask *trick_hud_alloc(TrickHud *h,int type,int value,int kind)
{
    int first=type==1 ? 0:4,last=type==1 ? 4:15;
    /* 0b3878 first avoids IDs retired during this frame (2e48fc), then
       permits reuse only if no other free slot exists. */
    for(int pass=0;pass<2;pass++)for(int i=first;i<last;i++)if(!h->task[i].type && (pass || !(h->retired&(1u<<i)))){
        TrickTask *t=&h->task[i];
        *t=(TrickTask){.type=type,.value=value,.kind=kind,.clock=12,
            .hold=kind==5 ? 15:60,.x=-200,.y=type==2 ? 96:140,.order=++h->serial};
        return t;
    }
    return NULL;
}
static inline void trick_hud_clear_labels(TrickHud *h)
{for(int i=0;i<4;i++)if(h->task[i].type)h->task[i].deleted=1;}
static inline void trick_hud_reset(TrickHud *h,int *combo,float *distance)
{if(!h->blocked){*combo=0;*distance=0;trick_hud_clear_labels(h);}}
static inline void trick_hud_event(TrickHud *h,int *combo,float *distance,int chain,int kind,int name)
{
    if(h->blocked || !trick_hud_name(kind,name) || *combo<0)return;
    int mult=kind<2 ? (chain>0 ? 2:1):kind==2 || kind==4 ? 3:kind==3 ? 6:kind==5 ? 2:1;
    trick_hud_alloc(h,2,TRICK_POINTS[*combo<20 ? *combo:19],mult);
    int found=0;
    for(int i=0;i<4;i++){
        TrickTask *t=&h->task[i];if(!t->type)continue;
        if(!found && t->value==name && t->kind==kind){found=1;t->hold=kind==5 ? 15:60;}
        else t->deleted=1;
    }
    if(!found)trick_hud_alloc(h,1,name,kind);
    *distance=0;if(kind!=5)++*combo;
}
static inline void trick_hud_finish(TrickHud *h,int *combo,float *distance)
{
    if(h->blocked || *combo<0)return;
    int bonus=TRICK_BONUS[*combo<16 ? *combo:15];
    if(bonus){trick_hud_alloc(h,2,bonus,1);trick_hud_alloc(h,3,*combo,0);}
    trick_hud_reset(h,combo,distance);
}
static inline int trick_hud_tick(TrickHud *h,float (*sine)(int))
{
    int score=0;h->retired=0;
    for(int i=0;i<15;i++){
        TrickTask *t=&h->task[i];if(!t->type)continue;
        if(!t->deleted){
            if(t->phase==0){
                int angle=(int)((float)(t->clock*16384)/12.f);
                /* Separate SH4 FMUL/FADD; do not contract to FMA. */
                volatile float dx=sine(angle)*-300.f;t->x=dx+100.f;
                t->y=t->type==2 ? 96.f:140.f;
                if(--t->clock<0){
                    t->x=100;t->phase=1;
                    if(t->type!=1)t->clock=t->type==2 ? 15:30;
                }
            }else if(t->phase==1){
                if(t->type==1){if(--t->hold<0)t->deleted=1;}
                else if(--t->clock<0)t->deleted=1;
            }
        }
        /* 0b2d2c retires immediately after the callback marks deletion. */
        if(t->deleted){if(t->type==2)score+=t->value*t->kind;t->type=0;h->retired|=1u<<i;}
    }
    return score;
}
static inline void trick_hud_bonus_text(char out[100],int count)
{
    char number[24];hud_number(number,count);
    /* 02f394 then 013b18, source suffix 183200[0], not a translation. */
    strcpy(out,number);strcat(out,TRICK_SUFFIX);
}
#endif
