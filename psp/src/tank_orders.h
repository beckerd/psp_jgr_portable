#ifndef JGR_TANK_ORDERS_H
#define JGR_TANK_ORDERS_H
#include <stdint.h>
/* Original 0741c8 / 07425c / 0742a0 / 074378 / 074408, with
   Tank's 1c8fe0 parameters: 90 ticks between bursts, 9 between members,
   at most 5 orders. Caller supplies the existing bounded member pool.
   These are independent root groups (+ec == 0), as authored for tanks.
   Sound events 2/3 are empty for tank kinds 10/30 (096178 dispatch).
   No allocation, terrain probes, animation evaluation or rendering here. */
typedef struct {int clock,cursor,delay,left,state;} TankOrders;
static inline void tank_orders_tick(TankOrders *o,unsigned *flags,
    unsigned *members,int count,int another_attacking,int police_attack,
    int continue_attack)
{
    switch(o->state) {
    case 0:o->state=1;o->cursor=0;break;
    case 1:
        if((unsigned)o->clock>=90u && (*flags&2)) {
            if(another_attacking){*flags&=~2u;break;}
            int busy=0;for(int i=0;i<count;i++)busy|=members[i]&2;
            if(busy){*flags&=~2u;o->clock=45;break;}
            o->state=2;
        }
        break;
    case 2:
        o->left=5;*flags|=8;o->cursor=0;o->delay=0;o->state=3;
        break;
    case 3:
        if(!(*flags&2) || (*flags&0x10)){o->state=0;*flags&=~2u;break;}
        if((unsigned)o->clock>=90u){o->clock=0;*flags|=0x80000000u;o->left=5;}
        if(!(*flags&0x80000000u)) {
            if(police_attack || !continue_attack || (*flags&0x4000))*flags&=~2u;
            break;
        }
        if(o->left<1 || o->cursor>=count || police_attack) {
            *flags&=~0x80000000u;o->cursor=0;
        } else if(++o->delay>=9) {
            o->delay=0;
            if(members[o->cursor]&0x40){members[o->cursor]|=0x80000002u;o->left--;}
            o->cursor++;
        }
        break;
    }
    o->clock++;
}
#endif
