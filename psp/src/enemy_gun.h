#ifndef JGR_ENEMY_GUN_H
#define JGR_ENEMY_GUN_H
#include <stdint.h>
/* 09aae4/09ab40: Rocket's original member burst, independent of the
   flight/formation and animation controllers. Flags are actor +a8. */
typedef struct {int initial,shots,interval,warning;} EnemyGunParams;
typedef struct {int delay,left,first;unsigned probe,audio_token;} EnemyGun;
typedef struct {int mode,query,warning,first_shot,finished;} EnemyGunEvents;
static inline void enemy_gun_begin(EnemyGun *s,uint32_t *flags,const EnemyGunParams *p)
{s->delay=p->initial;s->left=p->shots;s->first=1;s->probe=0;*flags&=~0x60000000u;}
static inline EnemyGunEvents enemy_gun_tick(EnemyGun *s,uint32_t *flags,
    const EnemyGunParams *p,int blocked,int vulnerable)
{
    EnemyGunEvents e={.mode=-1};
    if(--s->delay<=0) {
        e.mode=s->first==1 || !(*flags&0x20000000u) ? 0:1;s->first=0;
        e.first_shot=s->left==p->shots;
        if(--s->left<=0){*flags&=~0xe0000002u;s->delay=0;e.finished=1;}
        else s->delay=p->interval;
    } else if((unsigned)s->delay<=(unsigned)p->warning) {
        if(!(*flags&0x40000000u)){e.query=1;*flags|=0x60000000u;}
        else if(*flags&0x20000000u) {
            if(!blocked && vulnerable)e.warning=1;
            else *flags&=~0x20000000u;
        }
    }
    return e;
}
/* 0867d4 / 1b91ec. Six live rounds (the first two deliberately miss),
   followed by three empty trigger pulls before recovery. Tests can drive
   this without moving an actor or replacing the original shot task. */
static inline EnemyGunEvents enemy_onishima_gun_tick(EnemyGun *s,uint32_t *flags,
    int blocked,int vulnerable)
{
    EnemyGunEvents e={.mode=-1};
    if(s->delay--<1) {
        if(s->left>0) {
            e.mode=s->left>4 || !(*flags&0x20000000u) ? 0:1;
            *flags&=~0x60000000u;
        }
        e.first_shot=s->left>0 ? 1:2; /* live report / empty click */
        if(--s->left < -2)e.finished=1;
        else s->delay=54;
    } else if((unsigned)s->delay<=45u && (unsigned)s->left<=4u) {
        if(!(*flags&0x40000000u)){e.query=1;*flags|=0x60000000u;}
        else if(*flags&0x20000000u) {
            if(!blocked && vulnerable)e.warning=1;
            else *flags&=~0x20000000u;
        }
    }
    return e;
}
#endif
