#ifndef JGR_ENEMY_SQUAD_H
#define JGR_ENEMY_SQUAD_H
#include <stdint.h>
/* Common anchored formation leader, 092204..0924c4. Member removal is a
   separate visibility/departure event; a finished burst is never removal. */
typedef struct {uint32_t flags;int state;} EnemySquadLeader;
static inline void enemy_squad_leader_tick(EnemySquadLeader *s,int trigger,
    int police,int on_graph,int ready,int remaining)
{
    if(s->flags&0x20)return;
    /* A departure request is latched once by this PSP task adapter. */
    if((s->flags&0x10) && s->state<5)s->state=5;
    else if((s->flags&2) && s->state<5)s->state=3;
    switch(s->state) {
    case 0:s->flags|=0x101;s->state=1;break;
    case 1:if(ready){s->flags&=~0x101u;s->state=2;}break;
    case 2:if(trigger && !police && on_graph){s->flags|=2;s->state=3;}break;
    case 3:if(!police && (s->flags&2) && trigger)s->flags|=0x20000;
        else {s->flags&=~2u;s->state=4;}break;
    case 4:s->flags&=~0x20000u;s->state=2;break;
    case 5:if(s->flags&0x100)s->flags|=0x20;s->state=6;break;
    case 6:if(!remaining)s->flags|=0x20;break;
    }
}
#endif
