#ifndef JGR_OPENING_STATE_H
#define JGR_OPENING_STATE_H
/* 0edffc, 0efdf0, 100284 and 0ffb70. The original startup state consumes
   one task tick; contacts are consumed on the following tick. */
enum {OPENING_AUTO,OPENING_GATE,OPENING_WINDOW,OPENING_DOOR,OPENING_HINGED,OPENING_BARRIER,OPENING_REACTIVE};
typedef struct {unsigned phase,contact,floor,variant;float frame,mirror,direction[3];} OpeningState;
static int opening_state_tick(OpeningState *s,unsigned kind,unsigned frames,float side,float distance2)
{
    /* 05c868: contact scenery plays once, then restores its static pose.
       The final motion frame is displayed before the following tick retires it. */
    if(kind==OPENING_REACTIVE){
        int started=0;
        if(s->phase<2){
            s->phase=1;
            if(s->contact){s->phase=2;s->frame=0;s->mirror=1;started=1;}
        }else if(++s->frame>frames-1){s->phase=1;s->frame=0;}
        s->contact=s->floor=0;return started;
    }
    int opened=0;
    switch(s->phase){
    case 0:s->phase=1;break;
    case 1:
        if(kind==OPENING_AUTO ? distance2<100.f:(s->contact || s->floor)){
            if(kind==OPENING_GATE)s->mirror=side>0 ? -1:1;
            else if(kind==OPENING_HINGED)s->variant=side>0;
            else if(kind!=OPENING_AUTO && kind!=OPENING_BARRIER)s->mirror=s->contact ? (side>0 ? 1:-1):(s->floor==1 ? -1:1);
            s->phase=2;opened=1;
        }break;
    case 2:
        s->frame+=kind==OPENING_WINDOW ? .5f:1.f;
        if(kind==OPENING_HINGED ? s->frame>frames-1:s->frame>=frames-1){s->frame=frames-1;s->phase=3;}break;
    case 3:
        if(kind==OPENING_BARRIER)break; /* 0ee470: stays broken until stage reset. */
        if(kind==OPENING_WINDOW)s->phase=5; /* original owner deletion */
        else if(distance2>(kind==OPENING_AUTO ? 100.f:kind==OPENING_HINGED ? 40000.f:10000.f))s->phase=4;
        break;
    case 4:
        s->frame-=1.f;if(s->frame<=0){s->frame=0;s->phase=1;}break;
    }
    s->contact=s->floor=0;return opened;
}
#endif
