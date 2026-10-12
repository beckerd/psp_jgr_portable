#ifndef JGR_HOUSE_STATE_H
#define JGR_HOUSE_STATE_H
/* 04d2d0 (paper), 0ea910 (window), 05a544 (door), 05c868 (gate),
   05835c (hanging panel). One 30 Hz task tick.
   Contact is the previous collision pass. Doors and paper use scale Z to mirror the
   authored motion according to the contacting limb's direction of travel. */
typedef struct {unsigned phase;float frame,mirror;int contact;float direction[3];} HouseState;
static inline int house_is_door(int kind)
{return kind==17 || kind==76 || kind==85 || kind==86 || kind==87;}
static inline int house_state_tick(HouseState *s,int kind,unsigned frames,float side,float distance2)
{
    int sound=0,door=house_is_door(kind);
    switch(s->phase) {
    case 0:
        if(s->contact){s->mirror=(kind==4 || kind==18 || kind==58) ? 1.f:(side>0)==!!door ? -1.f:1.f;s->frame=0;s->phase=1;sound=1;}
        break;
    case 1:
        s->frame+=1.f;
        if(s->frame>(float)(frames-1)){s->frame=(kind==4 || kind==58) ? 0.f:(float)(frames-1);s->phase=kind==58 ? 0:2;}
        break;
    case 2:if(door && distance2>4225.f)s->phase=3;break;
    case 3:
        if(door){s->frame-=1.f;if(s->frame<=0){s->frame=0;s->mirror=1;s->phase=0;}}
        break;
    }
    s->contact=0;return sound;
}
#endif
