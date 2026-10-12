#ifndef JGR_SPRAY_SCORE_H
#define JGR_SPRAY_SCORE_H
#include <string.h>

/* Original 040d34/0418a8/0418cc/041c18/041dfc/041ec0 task kernels.
   The persistent tally runs before the player; score children run after the
   player and cue HUD, including on their creation tick. Keep those calls
   separate: a new flight must hold off an already armed tally next tick. */
#define SPRAY_SCORE_ROWS 10
#define SPRAY_SCORE_FLIGHTS 3
typedef struct {
    int alive,life,amount,base,bonus;
    float x,y,vx,vy,ay;
} SprayScoreFlight;
typedef struct {
    int timer,armed,settled,pending,deleted;
    float next_y;
    int base[SPRAY_SCORE_ROWS],bonus[SPRAY_SCORE_ROWS];
    SprayScoreFlight flight[SPRAY_SCORE_FLIGHTS];
} SprayScore;

static inline void spray_score_init(SprayScore *s)
{memset(s,0,sizeof *s);s->timer=20;s->next_y=96.f;}
static inline int spray_score_flights_alive(const SprayScore *s)
{for(unsigned j=0;j<SPRAY_SCORE_FLIGHTS;j++)if(s->flight[j].alive)return 1;return 0;}
static inline int spray_score_queued(const SprayScore *s)
{
    int total=0;
    for(int j=0;j<s->settled;j++)total+=s->base[j];
    for(int j=s->settled;j<s->settled+s->pending;j++)total+=s->base[j]+s->bonus[j];
    return total;
}
static inline void spray_score_append(SprayScore *s,int base,int bonus)
{
    if(s->deleted || !base)return;
    if(s->settled+s->pending==SPRAY_SCORE_ROWS) {
        /* The original task has two ten-entry arrays and assumes its
           authored seven-stroke sequence fits. Preserve points if a port
           caller exceeds that bound, without reproducing an out-of-bounds
           write. Ordinary authored sequences do not enter this branch. */
        s->base[0]=spray_score_queued(s);s->bonus[0]=0;
        s->settled=1;s->pending=0;s->next_y=160.f;
    }
    int at=s->settled+s->pending++;
    s->base[at]=base;s->bonus[at]=bonus;
}
static inline void spray_score_finish_flight(SprayScore *s,unsigned j)
{
    SprayScoreFlight *f=s->flight+j;
    if(!f->alive)return;
    f->alive=0;spray_score_append(s,f->base,f->bonus);
}
static inline void spray_score_finish_flights(SprayScore *s)
{
    /* 040d34 appends cue 1, cue 2, then cue 0 to the player task list.
       Only cue zero carries the aggregate payload consumed by 041ec0. */
    for(unsigned j=1;j<SPRAY_SCORE_FLIGHTS;j++)spray_score_finish_flight(s,j);
    spray_score_finish_flight(s,0);
}
static inline void spray_score_arm(SprayScore *s)
{if(!s->deleted)s->armed=1;}
static inline void spray_score_begin(SprayScore *s,const int amounts[3],unsigned count,int bonus)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_spray_score_h__cue_x_0.inc"
#else

#include "generated/source_data/spray_score_h__cue_x_0.inc"

#endif

    if(!count || count>SPRAY_SCORE_FLIGHTS)return;
    /* Normal source scheduling waits until the previous score children
       retire. Keep a premature port call lossless as well. */
    spray_score_finish_flights(s);
    memset(s->flight,0,sizeof s->flight);
    int total=0;for(unsigned j=0;j<count;j++)total+=amounts[j];
    for(unsigned j=0;j<count;j++) {
        SprayScoreFlight *f=s->flight+j;
        int digits=1;for(int value=amounts[j];value>=10;value/=10)digits++;
        f->alive=1;f->life=20;f->amount=amounts[j];
        f->base=j ? 0:total;f->bonus=j ? 0:bonus;
        f->x=cue_x[count-1][j]+(float)(digits*32)*.5f;f->y=360.f;
        f->vx=(200.f-f->x)/20.f;
        f->ay=s->deleted ? 0.f:(360.f-s->next_y)/84.f;
        f->vy=f->ay*-14.f;
    }
    if(!s->deleted && total)s->next_y+=32.f;
}
static inline void spray_score_flights_tick(SprayScore *s,int player_state)
{
    for(unsigned at=0;at<SPRAY_SCORE_FLIGHTS;at++) {
        unsigned j=(at+1)%SPRAY_SCORE_FLIGHTS;SprayScoreFlight *f=s->flight+j;
        if(!f->alive)continue;
        if(player_state==10 || player_state==11){spray_score_finish_flight(s,j);continue;}
        int deleted=--f->life<=0;
        /* 041dfc still integrates on its deletion tick. The destructor
           executes after the callback returns, before the next sibling. */
        f->x+=f->vx;f->y+=f->vy;f->vy+=f->ay;
        if(deleted)spray_score_finish_flight(s,j);
    }
}
static inline int spray_score_tick(SprayScore *s,int player_state)
{
    if(s->deleted)return 0;
    if(player_state==10 || player_state==11) {
        int total=spray_score_queued(s);
        s->settled=s->pending=0;s->deleted=1;return total;
    }
    if(!s->armed || spray_score_flights_alive(s)){s->timer=20;return 0;}
    if(--s->timer>=0)return 0;
    if(s->pending) {
        int total=0;
        for(int j=s->settled;j<s->settled+s->pending;j++)total+=s->base[j]+s->bonus[j];
        s->base[s->settled]=total;s->bonus[s->settled]=0;
        s->pending=0;s->settled++;
        s->next_y=(float)s->settled*32.f+96.f;s->timer=20;return 0;
    }
    int total=spray_score_queued(s);
    s->settled=0;s->next_y=96.f;s->armed=0;return total;
}
static inline int spray_score_flush(SprayScore *s)
{
    spray_score_finish_flights(s);int total=spray_score_queued(s);
    spray_score_init(s);return total;
}
#endif
