/* 022e94's pending-rival STREET path, before task 22a's invitation.
   Parent update precedes task 217's fade; neither A nor B skips this path. */
#ifndef JGR_GARAGE_RIVAL_H
#define JGR_GARAGE_RIVAL_H
#include <string.h>
typedef struct {int phase,clock,done,sound;float fade,step,remainder;} GarageRival;
static inline void garage_rival_init(GarageRival *r)
{memset(r,0,sizeof *r);r->sound=-1;}
static inline void garage_rival_step(GarageRival *r)
{
    r->sound=-1;
    switch(r->phase) {
    case 0:r->fade=1;r->step=-.04f;r->sound=14;r->phase=1;break;
    case 1:if(!r->step)r->phase=2;break;
    case 2:r->phase=4;break; /* 0236d8 clears the VMU-query latch. */
    case 4:r->fade=0;r->step=.04f;r->phase=5;break;
    case 5:if(!r->step)r->phase=6;break;
    case 6:r->done=1;break;
    }
    if(r->step){r->fade+=r->step;
        if(r->fade>1){r->fade=1;r->step=0;}
        if(r->fade<0){r->fade=0;r->step=0;}}
    r->clock++;
}
#endif
