#ifndef JGR_FINALDAMAGE_H
#define JGR_FINALDAMAGE_H
#include <stdint.h>
/* 042b14/042b60, called by contact mode 8 (attribute 29), not the
   ordinary NPC knock-back handler. The Player adapter queues this response
   from the arena's previous registered contacts. In particular,
   state=7 below is NOT permission to enter Player's generic stun state.
   The original movement callback address remains unchanged. */
typedef struct {
    int32_t state,mode,health,dead,invincible,combo,burn;
    float grind_distance;
    int32_t overlay,overlay_ticks;
    float overlay_frame,overlay_blend;
} FinalDamage;
enum { FINAL_DAMAGE_RETIRE_COMBO_TASKS=1 };
static inline unsigned enemy_overlay_damage(FinalDamage *s,int32_t overlay,int32_t amount,
    int32_t duration,unsigned options,unsigned player_flags,int combo_disabled)
{
    s->state=7;s->mode=0;
    /* 05b00c clears +3d0/+3d4 and retires tasks 118..11b. It does NOT
       clear the skater's +1fc chain field like the old generic hit did. */
    unsigned requests=0;
    if(!combo_disabled && !(player_flags&0x2000)) {
        s->combo=0;s->grind_distance=0;requests|=FINAL_DAMAGE_RETIRE_COMBO_TASKS;
    }
    if(!(options&2))s->health=(int32_t)((uint32_t)s->health-(uint32_t)amount);
    if(s->health<=0){s->health=0;s->dead=1;}
    else s->invincible=duration; /* shared tuning +0, NOT per-character */
    s->burn=s->overlay_ticks=90;s->overlay=overlay;
    s->overlay_frame=s->overlay_blend=0;
    return requests;
}
/* 042b14/042b60: fire. 042a7c/042ac8 share the health and overlay
   path with logical motion 157 for bullets, preserving locomotion. */
static inline unsigned finaldamage_fire(FinalDamage *s,int32_t amount,
    int32_t duration,unsigned options,unsigned player_flags,int combo_disabled)
{return enemy_overlay_damage(s,158,amount,duration,options,player_flags,combo_disabled);}
/* 0198f8's two counter sites, after movement/overlay update. Other main-
   actor flags and the overlay pose are owned by their respective kernels. */
static inline void finaldamage_clocks(FinalDamage *s)
{if(s->burn>0)s->burn--;if(s->invincible>0)s->invincible--;}
#endif
