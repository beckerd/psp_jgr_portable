#ifndef JGR_POISON_ROUTE_H
#define JGR_POISON_ROUTE_H
#include "title_route.h"
#include "ctrl.h"
#include "player.h"
#include <string.h>

typedef struct {const char *mission,*prefix;unsigned set,character;} GangRoute;
static const GangRoute GANG_ROUTES[]={
    {"S1M03","love",0,11},
    {"S2M03","poison",0,10},
    {"S3M03","noise",0,12},
    {"S3M05","mask",1,15},
};
static inline const GangRoute *gang_route_find(const char *mission,unsigned set)
{
    if(mission)for(unsigned i=0;i<sizeof GANG_ROUTES/sizeof *GANG_ROUTES;i++)
        if(GANG_ROUTES[i].set==set && !strcmp(GANG_ROUTES[i].mission,mission))return GANG_ROUTES+i;
    return NULL;
}

/* Mode-3 relocation restores the authored angles/speed without resetting
   the live graffiti owner, health or the host controller's fractional clock. */
static inline void rival_recorded_start(Player *p,const float position[3],const int32_t angle[3],float speed)
{
    player_reposition(p,position);
    p->yaw=(angle[1]+32768)*(6.283185307f/65536.f);p->speed=speed;
    p->mirrored=0;p->pose_valid=0;p->logical_motion=0;p->frame=0;
    p->visual_valid=1;memcpy(p->visual_angles,angle,sizeof p->visual_angles);
    p->overlay_motion=-1;p->overlay_blend=0;
    player_cam_reset(p);
}
/* The original mode-3 controller owns waits, tags and transitions. Its
   deterministic escape performance uses completed original world/pose/effect
   samples, as the Garam demos do. No collision rebuild may alter this route. */
static inline int gang_route_validate(const TitleRoute *route,const CtrlSub *set,int member,unsigned character)
{
    if(!route->image || !route->has_water_point || route->character!=character ||
       !set || (unsigned)member>=3 || member>=set->ntab || set->nseg[member]!=3)return 0;
    unsigned total=0;
    for(int part=0;part<3;part++) {
        const CtrlSeg *seg=set->segs[member]+part;
        if(seg->stream<0 || seg->stream>=set->nstream || seg->flags!=(part==2 ? 3u:1u))return 0;
        const CtrlStream *stream=set->streams+seg->stream;
        if(stream->chr!=character || stream->nrec<2 || route->section[part]!=total)return 0;
        unsigned ticks=0;
        for(int i=0;i<stream->nrec-1;i++)ticks+=(stream->rec[i*4]&127)+1;
        if(!ticks)return 0;
        total+=ticks-1; /* controller advances before its first child update */
    }
    return total==route->count;
}
static inline int poison_route_validate(const TitleRoute *route,const CtrlSub *set,int member)
{return gang_route_validate(route,set,member,10);}
static inline int recorded_rival_apply(TitleRoute *route,int section,unsigned tick,Player *p,const Chars *bank,int model,CharPose *tag_pose)
{
    unsigned sections=route->section[2]>route->section[1] ? 3:2;
    if((unsigned)section>=sections)return 0;
    unsigned first=route->section[section],end=(unsigned)section+1<sections ? route->section[section+1]:route->count;
    if(tick>=end-first)return 0;
    TitleSample sample;
    if(!title_route_sample(route,first+tick,0,&sample) || sample.part!=(unsigned)section || sample.tick!=tick+1)return 0;
    p->px=p->x;p->py=p->y;p->pz=p->z;
    p->x=sample.pos[0];p->y=sample.pos[1];p->z=sample.pos[2];
    p->yaw=(sample.effects.travel_angles[1]+32768)*(6.283185307f/65536.f);
    p->state=sample.state;p->grinding=sample.state==3;p->onground=sample.state==1 || p->grinding;
    p->speed=sample.effects.speed;p->logical_motion=sample.effects.motion;
    p->frame=sample.effects.frame;p->motion_flags=sample.effects.flags;
    p->mirrored=sample.effects.mirrored;p->grind_pose=sample.effects.grind_pose;
    p->dash_held=sample.effects.dash;p->boost=sample.effects.boost;
    p->visual_valid=1;memcpy(p->visual_angles,sample.angle,sizeof sample.angle);
    if(tag_pose)memcpy(tag_pose,sample.pose,sizeof sample.pose);
    memcpy(p->pose,sample.pose,sizeof sample.pose);
    /* The actor/effect hierarchy uses the already resolved PLAY channels.
       The painted tag separately retains source channel presence. */
    for(int j=0;j<20;j++)p->pose[j].keyed=7;
    p->pose_valid=1;p->pose_bank=bank;p->pose_model=model;
    p->overlay_motion=-1;p->overlay_blend=0;p->logic_tick=sample.tick;
    memcpy(p->shallow_point,sample.water_point,sizeof p->shallow_point);
    player_effects_tick(p,&sample.effects);
    return 1;
}
static inline int poison_route_apply(TitleRoute *route,int section,unsigned tick,Player *p,const Chars *bank,CharPose *tag_pose)
{return recorded_rival_apply(route,section,tick,p,bank,13,tag_pose);}
#endif
