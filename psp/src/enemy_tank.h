#ifndef JGR_ENEMY_TANK_H
#define JGR_ENEMY_TANK_H
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
/* Stationary Tank: 0a2b00/0a2b60, shared aim/fire 0a2734, recovery
   0a28f4. Uses the same-sized storage as a gun; no per-ped heap growth. */
typedef struct {int state,aim,turn;} EnemyTank;
static inline int enemy_tank_angle(int a)
{return (int16_t)(uint16_t)a;}
static inline int enemy_tank_turn(int from,int to,int cap)
{
    int d=enemy_tank_angle(to-from);
    if(d>cap)d=cap;if(d< -cap)d=-cap;
    return enemy_tank_angle(from+d);
}
static inline int enemy_tank_tick(EnemyTank *t,unsigned *flags,int leader_attack,
    float distance,int target_yaw,int *yaw,int *animation,float *frame)
{
    if(*animation==7 && *frame>=30.f){*animation=0;*frame=0;}
    switch(t->state) {
    case 0:t->state=2;t->aim=t->turn=0;*animation=0;*frame=0;*flags=0x140;break;
    case 2:
        if(*flags&2)t->state=3;
        t->aim=enemy_tank_turn(t->aim,0,0x222);break;
    case 3:
        if(distance<=150.f){*flags&=~2u;break;}
        if(!leader_attack){*flags&=~2u;t->state=2;break;}
        {
            int aim=enemy_tank_angle(target_yaw-*yaw);
            if(abs(aim)<=0x6000) {
                t->turn=0;t->aim=enemy_tank_turn(t->aim,aim,0x222);
                if((*flags&2) && t->aim==aim) {
                    t->state=4;*animation=7;*frame=0;return 1;
                }
            } else {
                t->turn+=16;if(t->turn>0x222)t->turn=0x222;
                *yaw=enemy_tank_turn(*yaw,target_yaw,t->turn);
            }
        }
        break;
    case 4:if(*animation!=7){t->state=3;*flags&=~2u;}break;
    }
    return 0;
}
#endif
