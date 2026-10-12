#ifndef JGR_LEVEL_EXIT_H
#define JGR_LEVEL_EXIT_H
#include <string.h>

/* 018a0c phases 0xb/0xc -> 020f2c: ordinary boundary departure uses
   a black fade at .04 per 30 Hz tick, without a clear/results task. */
typedef struct {
    float alpha, remainder;
    int active, black_presented;
} LevelExit;

static inline void level_exit_begin(LevelExit *e)
{ memset(e,0,sizeof *e);e->active=1; }

static inline void level_exit_advance(LevelExit *e,float dt)
{
    if(!e->active || e->alpha>=1.f || dt<=0.f)return;
    e->remainder+=dt;
    while(e->remainder>=1.f/30.f && e->alpha<1.f) {
        e->remainder-=1.f/30.f;
        e->alpha+=.04f;
        if(e->alpha>1.f)e->alpha=1.f;
    }
}

/* Wait until the full black frame reaches the display before freeing
   the world or loading the next screen, including at low frame rates. */
static inline void level_exit_presented(LevelExit *e)
{ if(e->active && e->alpha>=1.f)e->black_presented=1; }

/* Task 6 +80 is the mission selection slot. Stage 0, slot 4 selects
   S1M05 (public mission id 32); its original destination is screen 0. */
static inline int level_exit_returns_title(unsigned stage,const char *name)
{ return stage==0 && name && !strcmp(name,"S1M05"); }
#endif
