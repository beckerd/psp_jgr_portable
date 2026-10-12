#ifndef JGR_STAGE_EXIT_H
#define JGR_STAGE_EXIT_H
#include <string.h>
#include "mission_quad.h"

typedef struct {
    int stage_index,mission_id;
    unsigned clock;
    float previous[3];
    int last_quad;
    unsigned last_cross;
} StageExit;
typedef struct {
    MissionQuad quad;
    int mission_id;
    unsigned direction,index;
} StageExitBoundary;
#if JGR_PORTABLE_ASSETS
#include "source_data_stage_exit_data.inc"
#else
#include "generated/source_data/stage_exit_data.inc"
#endif

static inline unsigned stage_exit_boundary_count(void)
{return sizeof benten_exits/sizeof *benten_exits;}
static inline const StageExitBoundary *stage_exit_boundary(unsigned ordinal)
{return ordinal<stage_exit_boundary_count() ? benten_exits+ordinal:NULL;}

/* stage_index is the original JMDc +08 value (Benten = 2), not the
   one-based PSP stage filename. Reset on mission entry or an explicit
   player relocation. No heap, filesystem, or clock reads are involved. */
static inline void stage_exit_reset(StageExit *e,int stage_index,int mission_id,const float position[3])
{
    memset(e,0,sizeof *e);e->stage_index=stage_index;e->mission_id=mission_id;
    memcpy(e->previous,position,sizeof e->previous);e->last_quad=-1;
}

/* Port coordinate-wrap safety: passage teleports change the coordinate
   frame, rather than moving the player through the intervening space.
   Preserve the sampled physical movement without inventing an exit
   crossing from that translation. This is not a claimed source callback. */
static inline void stage_exit_translate(StageExit *e,const float delta[3])
{for(int j=0;j<3;j++)e->previous[j]+=delta[j];}

/* Call once per running 30 Hz stage tick. 0c24a8 samples every fourth
   tick; 0c2e8c spans from the previous SAMPLE and updates that position
   before invoking callbacks. The local clock begins at mission entry;
   the original process-global sampling phase is not represented here.
   This helper reports only 032118's exit requests, not its unrelated
   traffic, police, and pedestrian callbacks. The owner arbitrates an
   exit against results/retry and performs the fade/garage handoff. */
static inline int stage_exit_tick(StageExit *e,const float position[3])
{
    e->last_quad=-1;e->last_cross=0;
    if(e->stage_index!=2 || (e->clock++&3u))return 0;
    float segment[6];
    for(int j=0;j<3;j++){segment[j]=e->previous[j];segment[j+3]=position[j]-segment[j];}
    memcpy(e->previous,position,sizeof e->previous);
    int exit=0;
    for(unsigned i=0;i<sizeof benten_exits/sizeof *benten_exits;i++) {
        const StageExitBoundary *b=benten_exits+i;
        if(b->mission_id>=0 && b->mission_id!=e->mission_id)continue;
        if(!mission_quad_near(&b->quad,segment))continue;
        unsigned crossed=mission_quad_cross(&b->quad,segment);
        if(crossed==b->direction){exit=1;e->last_quad=(int)b->index;e->last_cross=crossed;}
    }
    return exit;
}
#endif
