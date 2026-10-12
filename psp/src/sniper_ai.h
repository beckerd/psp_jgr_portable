#ifndef JGR_SNIPER_AI_H
#define JGR_SNIPER_AI_H
#include <stdint.h>
/* Original 09c54c..09ca5c. Visibility (10000), trigger (06e40c),
   close/front contact (09cf84) and animation completion are supplied by
   their respective engine adapters; the controller never invents a route. */
enum {SNIPER_QUERY=1,SNIPER_FIRE=2,SNIPER_WARNING=4,SNIPER_TURN=8,
      SNIPER_RUN=16,SNIPER_AIM=32};
typedef struct {int trigger,close,blocked,vulnerable,history;float speed;int beam;} SniperInput;
static inline unsigned sniper_step(int *state,int *timer,int *anim,int *cursor,
                                  uint32_t *flags,const SniperInput *in)
{
    unsigned e=0;
    switch(*state) {
    case 1:
        if(*flags&0x10000){*flags&=~0x100u;*state=2;*timer=0;}
        else if(*flags&0x10)*flags|=0x20;
        break;
    case 2:
        if((*flags&0x10) || in->close){*state=6;*anim=2;}
        else if((*timer)--<1){*timer=60;if(in->trigger){*state=3;*cursor=in->history;}}
        break;
    case 3: {
        int lag=(int)(in->speed*150.f);
        *cursor=(unsigned)lag<30u?(in->history-lag+30)%30:(in->history+1)%30;
        if(!in->trigger){*state=2;*timer=0;}
        else if(in->close){*state=6;*anim=2;}
        else if(--*timer<1){*timer=120;*state=4;}
        else if((unsigned)*timer<=45u && !(*flags&0x40000)){
            *flags|=0xc0000;e|=SNIPER_QUERY;
        } else if(*flags&0x80000) {
            if(!in->blocked && in->vulnerable)e|=SNIPER_WARNING;
            else *flags&=~0x80000u;
        }
        if(in->beam)*flags|=0x20000;else *flags&=~0x20000u;
        e|=SNIPER_AIM;
        break; }
    case 4:
        *state=5;*anim=1;*cursor=(*cursor+1)%30;
        e|=SNIPER_FIRE;if(*flags&0x80000)e|=SNIPER_WARNING;
        *flags&=~0xc0000u;
        break;
    case 5:if(*anim!=1)*state=2;break;
    case 6:*state=7;e|=SNIPER_TURN;break;
    case 7:if(*anim!=2){*state=8;*timer=0;}e|=SNIPER_TURN;break;
    case 8:if(*flags&0x10000)*flags|=0x20;else e|=SNIPER_RUN;break;
    }
    return e;
}
/* 09bacc/09bc10: rank 1 turns the whole body with two aiming clips;
   rank 2 retains the placed body and overlays the left/right torso aim. */
static inline int sniper_motion(int rank,int anim,int aim_delta)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_sniper_motion.inc"
#else

#include "generated/source_data/sniper_motion.inc"

#endif

    if(!rank && !anim && aim_delta)return turn[aim_delta>=0];
    return primary[rank!=0][(unsigned)anim<4u?anim:0];
}
#endif
