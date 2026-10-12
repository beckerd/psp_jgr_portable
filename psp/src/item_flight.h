#ifndef JGR_ITEM_FLIGHT_H
#define JGR_ITEM_FLIGHT_H
#include <math.h>
#include <stdint.h>
#include <string.h>
/* Original loose-item task 0def4c, states 0df174/0df270/0df374.
   Destinations come from the ENEMY record, not random scatter. State 2
   creates the ordinary pickup at that destination on the following tick. */
typedef struct {
    float pos[3],target[3],direction[3],speed,vy;
    int32_t angle[3];unsigned state,age;
} ItemFlight;
static inline void item_flight_start(ItemFlight *f,const float *origin,const float *target)
{memset(f,0,sizeof *f);memcpy(f->pos,origin,12);memcpy(f->target,target,12);}
static inline void item_flight_tick(ItemFlight *f)
{
    if(f->state==0) {
        float x=f->target[0]-f->pos[0],z=f->target[2]-f->pos[2];
        float distance=sqrtf(x*x+z*z);
        if(distance<=0){f->state=2;return;}
        float inverse=1.f/distance;f->direction[0]=x*inverse;f->direction[2]=z*inverse;
        float time=f->target[1]>f->pos[1] ? 10.f+sqrtf((f->target[1]-f->pos[1])*2.f/.2f)
                                                 : (f->pos[1]-f->target[1])/2.f+20.f;
        f->speed=distance/time;f->vy=2.f;f->state=1;f->age=0;
    }else if(f->state==1) {
        f->pos[0]+=f->direction[0]*f->speed;f->pos[2]+=f->direction[2]*f->speed;
        f->pos[1]+=f->vy;f->vy-=.2f;if(f->vy< -2.f)f->vy=-2.f;
        f->angle[0]+=0x500;f->angle[1]+=0x800;f->angle[2]+=0x1000;
        float x=f->pos[0]-f->target[0],z=f->pos[2]-f->target[2];
        if(x*x+z*z<9.f || ++f->age>150)f->state=2;
    }
}
#endif
