#ifndef GRIND_LIFT_STATE_H
#define GRIND_LIFT_STATE_H
typedef struct {unsigned phase,frame,contact;} GrindLiftState;
/* 0f81dc, one original 30 Hz update. Return the original bank-16 cue.
   Contact is the previous actor/body registration, not distance to a floor. */
static inline int grind_lift_step(GrindLiftState *s,float player_y,float distance2)
{
    int sound=-1;
    switch(s->phase){
    case 0:s->frame=27;s->phase=1;break;
    case 1:
        if(s->contact){s->phase=5;sound=47;}
        else if(player_y<50.f && distance2<22500.f){s->phase=2;sound=45;}
        break;
    case 2:if(s->frame)s->frame--;else s->phase=3;break;
    case 3:if(s->contact || distance2>22500.f){s->phase=4;sound=45;}break;
    case 4:if(++s->frame>27){s->frame=27;s->phase=1;sound=46;}break;
    case 5:
        s->frame++;if(s->frame==201)sound=48;
        if(s->frame>225){s->frame=225;s->phase=6;}break;
    case 6:if(player_y<50.f){s->phase=7;sound=47;}break;
    case 7:
        if(s->frame<29){s->frame=27;s->phase=1;sound=48;}else s->frame-=2;
        break;
    }
    s->contact=0;return sound;
}
#endif
