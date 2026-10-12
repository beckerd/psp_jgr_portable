#ifndef FINALBOSS_H
#define FINALBOSS_H
#include "charpose.h"
/* 0e9368 / 1dc0c8: platform graffiti follows node 1, including collapse. */
int finalboss_platform_attachment(const Chars *,unsigned model,float frame,float collapse,const float *world,float out[16]);
#include <stdint.h>
/* 0ec72c / 0ec93c / 0ecbd8. Actor animation and voice clocks;
   finalarena binds the graffiti, damage and mission controllers. */
typedef struct {
    int motion,old_motion;
    float frame,blend;
    unsigned orbit,left,right,texture_clock,texture_rotation,final_rotation;
    int voice_ticks,voice_active;
    CharPose pose[20],entry[20];
} FinalBoss;
void finalboss_init(FinalBoss *,const Chars *);
void finalboss_animation(FinalBoss *,const Chars *,unsigned (*random15)(void *),void *);
void finalboss_clocks(FinalBoss *,const Chars *,int phase,int cinematic);
/* 0ed15c. Call outside EVENTs only. An ended owner retires on its own
   tick; the countdown resumes next tick. Returns a source reference or -1. */
int finalboss_voice(FinalBoss *,int phase,int playing,unsigned (*random15)(void *),void *);
/* Source 0b9e30/0ba088 and attachment table 1de910 (preorder bones 4/7).
   All matrices exposed here are row-major, unlike Dreamcast RAM. */
void finalboss_attachments(const Chars *,float frame,const float *world,float out[2][16]);
/* 0ed42c / table 1df2e0: final boss placement is central model bone 63. */
int finalboss_central_attachment(const Chars *,float frame,const float *world,float out[16]);
unsigned finalboss_random15(void *state);
#endif
