/* Ordinary character confirmation: 025758 -> 0257f8 -> 025810.
   Separate from 026234's draw phase. Special two-character departures
   and voice/statistics scheduling have different owners. */
#ifndef JGR_GARAGE_EXIT_H
#define JGR_GARAGE_EXIT_H
#include <string.h>
typedef struct {int phase,fading,done;float alpha,remainder;unsigned edges;} GarageExit;
static inline void garage_exit_begin(GarageExit *e)
{memset(e,0,sizeof *e);e->phase=4;}
/* Update parent, then its task-217 child. The finished draw is observed
   by the NEXT update; the fade must finish before task-6's handoff. */
static inline void garage_exit_step(GarageExit *e,int draw_phase,int confirm)
{
    if(e->phase==4 && (draw_phase==4 || confirm)) {
        e->phase=5;e->alpha=0;e->fading=1;
    } else if(e->phase==5 && !e->fading)e->phase=6;
    else if(e->phase==6)e->done=1;
    if(e->fading){e->alpha+=.04f;if(e->alpha>1.f){e->alpha=1;e->fading=0;}}
}
#endif
