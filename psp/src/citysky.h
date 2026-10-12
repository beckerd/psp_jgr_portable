#ifndef JGR_CITYSKY_H
#define JGR_CITYSKY_H
#include "charanim.h"
#include "stage.h"
#include "zones.h"
typedef struct {
    Chars models;
    int stage,error;
    int visible,sector,pending_sector;
    unsigned yaw[2][3];
    float remainder;
} CitySky;
/* Original stage-loader sky roots, not a radius or no-UV heuristic. */
int citysky_bind_stage(Stage *,int stage);
int citysky_load(CitySky *,int stage);
void citysky_free(CitySky *);
/* 039af0 initializes from JMDc+d4; 03a2e4 changes the latched sky only
   at sector transitions. These are NOT EVENT descriptor IDs. */
void citysky_sector(CitySky *,int sector);
/* 020580 / 0cfc8c -> 0dce34 -> 0dcd3c. A missed volume retains the
   previous sector. The sampled sector takes effect on the next 30Hz draw.
   event_forward is NULL for gameplay; handoff is task 1's gameplay-camera
   eye (+7c), used by 0cfc8c when the cinematic-release flag is set. */
void citysky_camera(CitySky *,const Zones *,const float eye[3],
                    const float *event_forward,const float *handoff);
void citysky_draw(CitySky *,const float eye[3],const float planes[6][4],float dt);
void citysky_step(CitySky *,float dt);
#endif
