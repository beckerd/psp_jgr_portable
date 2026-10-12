#ifndef JGR_TRAFFIC_CONTACT_H
#define JGR_TRAFFIC_CONTACT_H
#include "source_body.h"
/* 055bc8 table 182c14: collision hierarchy attributes, NOT render bones. */
static inline unsigned traffic_limb_group(unsigned attribute)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_traffic_contact_h__groups_0.inc"
#else

#include "generated/source_data/traffic_contact_h__groups_0.inc"

#endif

    return attribute<20 ? groups[attribute]:0;
}
/* 0559ac, with the original rotated (0,0,-10) supplied by the caller.
   Contact payloads retain the previous limb ORIGIN, not previous sphere
   center. Nonzero local sphere offsets must not be folded into history. */
static inline int traffic_contact_direction(const SourceActorContact *c,unsigned group,
    const float forward[3],int (*atan_bam)(float,float),int *yaw)
{
    if(group<1 || group>3)return -1;
    float d[3];for(int j=0;j<3;j++)d[j]=c->center[j]-c->other_previous[j];
    float approaching=probe_fipr(d,c->other_direction),facing=probe_fipr(forward,c->other_direction);
    int reverse=approaching<0 ? facing<0:facing>0;
    int base=approaching<0 ? (reverse?9:3):(reverse?6:0);
    float x=c->previous[0]-c->other_previous[0],z=c->previous[2]-c->other_previous[2];
    *yaw=reverse ? atan_bam(-x,-z):atan_bam(x,z);
    return base+(int)group-1;
}
#endif
