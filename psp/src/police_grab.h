#ifndef JGR_POLICE_GRAB_H
#define JGR_POLICE_GRAB_H
#include <stdint.h>
/* Player +810..818 (01b298). Tokens replace source task pointers: group
   storage is compacted on despawn, so an address is not a stable owner. */
typedef struct { uint32_t owner[3],seen[3],epoch; } PoliceGrips;
static inline int police_grip_count(const PoliceGrips *p)
{return !!p->owner[0]+!!p->owner[1]+!!p->owner[2];}
static inline int police_grip_allowed(int state,int trick)
{
    /* 06e238, with 078a84's additional state-9 exclusion. */
    switch(state){case 2:return !trick;case 3:case 4:case 7:case 8:case 9:case 10:case 11:return 0;default:return 1;}
}
static inline int police_grip_claim(PoliceGrips *p,uint32_t token)
{
    if(!p || !token)return -1;
    for(int i=0;i<3;i++)if(p->owner[i]==token){p->seen[i]=p->epoch;return i;}
    for(int i=0;i<3;i++)if(!p->owner[i]){p->owner[i]=token;p->seen[i]=p->epoch;return i;}
    return -1;
}
static inline int police_grip_touch(PoliceGrips *p,int slot,uint32_t token)
{
    if(!p || (unsigned)slot>=3 || p->owner[slot]!=token)return 0;
    p->seen[slot]=p->epoch;return 1;
}
static inline void police_grip_release(PoliceGrips *p,int slot,uint32_t token)
{if(p && (unsigned)slot<3 && p->owner[slot]==token)p->owner[slot]=0;}
static inline void police_grip_begin(PoliceGrips *p)
{if(++p->epoch==0){p->epoch=1;for(int i=0;i<3;i++)p->seen[i]=0;}}
static inline void police_grip_end(PoliceGrips *p)
{for(int i=0;i<3;i++)if(p->seen[i]!=p->epoch)p->owner[i]=0;}
#endif
