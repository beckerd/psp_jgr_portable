#ifndef JGR_NPC_AUDIO_H
#define JGR_NPC_AUDIO_H
#include <stdint.h>
/* Pure 08b558 and the 08a538..08a594 special-person branch. depth is
   NPC+ b8, written by the PREVIOUS 08b608 draw; not Euclidean distance.
   08a814 resets +f8 to zero, superseding the allocator-address seed. */
typedef struct {int16_t reference,reaction;uint32_t period;} NpcAudioType;
typedef struct {float speed;int16_t reference;uint16_t period;} NpcAudioRoute;
static inline int source_npc_ambient(uint32_t *clock,unsigned type,unsigned state,
    float depth,int has_owner,const NpcAudioType *person,const NpcAudioRoute *route,int *reference)
{
    if(type==43 || type==44){
        if(type!=44 || state!=3 || has_owner)return 0;
        uint32_t old=(*clock)++;
        if(old<=600 || !(depth>0 && depth<100))return 0;
        *clock=0;*reference=-4;return 1;
    }
    if((!person->reference && !route->reference) || state<2 || state>=5 || !(depth>0 && depth<100))return 0;
    uint32_t period=person->reference>0 ? person->period:route->period,old=(*clock)++;
    if(period>old)return 0;
    *clock=0;if(has_owner)return 0;
    *reference=person->reference>0 ? person->reference:route->reference;return 1;
}
#endif
