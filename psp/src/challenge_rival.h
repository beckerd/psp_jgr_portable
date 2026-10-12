#ifndef JGR_CHALLENGE_RIVAL_H
#define JGR_CHALLENGE_RIVAL_H
#include "poison_route.h"
typedef struct {const char *mission,*path;unsigned set,character;} ChallengeRival;
static const ChallengeRival CHALLENGE_RIVALS[]={
    {"S1M07","challenge_rival_s1m07.bin",1,0},
    {"S2M07","challenge_rival_s2m07.bin",0,4},
    {"S3M07","challenge_rival_s3m07.bin",0,8},
};
static inline const ChallengeRival *challenge_rival_find(const char *mission,unsigned set)
{
    if(mission)for(unsigned i=0;i<sizeof CHALLENGE_RIVALS/sizeof *CHALLENGE_RIVALS;i++)
        if(CHALLENGE_RIVALS[i].set==set && !strcmp(CHALLENGE_RIVALS[i].mission,mission))return CHALLENGE_RIVALS+i;
    return NULL;
}
static inline int challenge_rival_validate(const TitleRoute *route,const CtrlSub *set,unsigned character)
{
    if(!route->image || !route->has_water_point || route->character!=character ||
       !set || set->ntab!=1 || set->nseg[0]<2 || route->section[1]!=route->section[2])return 0;
    unsigned total=0;
    /* A loop flag terminates the authored table. Trailing pointer-shaped
       data in the bank is never a third section. */
    for(int part=0;part<2;part++) {
        const CtrlSeg *seg=set->segs[0]+part;
        if(seg->stream<0 || seg->stream>=set->nstream || seg->flags!=(part ? 3u:1u))return 0;
        const CtrlStream *stream=set->streams+seg->stream;
        if(stream->chr!=(int)character || stream->nrec<2 || route->section[part]!=total)return 0;
        unsigned ticks=0;
        for(int i=0;i<stream->nrec-1;i++)ticks+=(stream->rec[i*4]&127)+1;
        if(!ticks)return 0;
        total+=ticks-1;
    }
    return total==route->count;
}
#endif
