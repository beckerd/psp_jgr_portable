#ifndef JGR_RECRUIT_SPRAY_H
#define JGR_RECRUIT_SPRAY_H
#include "player.h"
#include "recruit_audio.h"

/* A sampled demo already includes 03ed98's completed partial pose. Do not
   run it again, or leave a live GF callback's small-spray flag latched.
   03f926 publishes the can model after +294 retires; 047084 reads the same
   flag/target for mist. Reapplying a sample at a render fraction is inert. */
static inline void recruit_spray_apply(Player *p,const RecruitDemo *demo,unsigned tick)
{
    const RecruitSprayCue *cue=NULL;
    if(demo)for(unsigned i=0;i<demo->spray_count && demo->sprays[i].tick<=tick;i++)cue=demo->sprays+i;
    p->small_spray_active=cue ? cue->active:0;
    p->spray_variant=cue ? cue->variant:0;
    p->small_spray_ref=cue ? cue->ref:0;
    p->small_spray_frame=cue ? cue->frame+(cue->active ? tick-cue->tick:0):0;
    if(cue)memcpy(p->small_spray_target,cue->target,sizeof p->small_spray_target);
    else memset(p->small_spray_target,0,sizeof p->small_spray_target);
}
#endif
