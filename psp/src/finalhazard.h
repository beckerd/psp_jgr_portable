#ifndef FINALHAZARD_H
#define FINALHAZARD_H
#include "charanim.h"
typedef struct {
    int state,request,direction,idle,frame,motion,position,deleted;
    unsigned voice_serial,projectile_serial;
    float world[16],attachment[6];
} FinalHazard;
/* 0e99bc launcher state machine. Events are original frame boundaries;
   the 0ebc50 projectile and damage task is a separate recovery. */
void finalhazard_init(FinalHazard *);
int finalhazard_select(float x,float z); /* parent 0e51a4, strict radius 350 */
void finalhazard_tick(FinalHazard *);
int finalhazard_pose(FinalHazard *,const Chars *);
void finalhazard_draw(const FinalHazard *,Chars *);
int finalhazard_yaw(const FinalHazard *);
#endif
