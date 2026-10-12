#ifndef JGR_OBJECT_GRAFFITI_POSE_H
#define JGR_OBJECT_GRAFFITI_POSE_H
#include "object_graffiti.h"
#include "charpose.h"
#include <string.h>
/* 019cfc at 01a1cc..01a1e2 submits GF+bc with PLAY+594, frame zero.
   Reuse completed channels but preserve the tag tree's own unkeyed bind TRS. */
static inline int gfo_completed_pose(const GfoTemplate *tag,const CharPose input[20],CharPose out[64])
{
    if(!tag || !input || tag->nbones>20)return 0;
    const JgcBone *bones=gfo_bones(tag);
    for(unsigned j=0;j<tag->nbones;j++) {
        const JgcBone *b=bones+j;const CharPose *p=input+j;CharPose *q=out+j;
        *q=*p;q->ev=b->ev;
        if(p->keyed&1)q->ev&=~1u;else memcpy(q->pos,b->pos,sizeof q->pos);
        if(p->keyed&2)q->ev&=~2u;else memcpy(q->ang,b->ang,sizeof q->ang);
        if(p->keyed&4)q->ev&=~4u;else memcpy(q->scl,b->scl,sizeof q->scl);
    }
    return 1;
}
#endif
