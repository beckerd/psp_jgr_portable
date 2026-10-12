#ifndef JGR_STAFF_ROUTE_H
#define JGR_STAFF_ROUTE_H
#include "title_route.h"
#include <stdint.h>

/* SRT1 is the small companion to one selected skater's TRT2 performance.
   All offsets are frame indices, never file pointers or wall-clock time. */
typedef struct {
    uint32_t row,first_frame,count,replay_tick0,player_tick0,reserved;
} StaffRouteSegment;
typedef struct {
    uint32_t frame,active,variant,ref;
    float pose_frame,target[3];
} StaffRouteSpray;
typedef struct {
    uint32_t frame,area,index,stroke,reveal,complete,started,revealing;
} StaffRouteGraffiti;
typedef struct {
    TitleRoute route;
    unsigned char *metadata;
    const StaffRouteSegment *segments;
    const StaffRouteSpray *sprays;
    const StaffRouteGraffiti *graffiti;
    unsigned nspray,ngraffiti,next_spray,next_graffiti;
    unsigned frame,have_frame;
    const StaffRouteSpray *spray;
} StaffRoute;
#endif
