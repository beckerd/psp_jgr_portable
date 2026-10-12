#ifndef HELIDESTRUCT_H
#define HELIDESTRUCT_H
#include <stdint.h>
/* Recovered helicopter wreck task fields. Flight state 15 supplies entry;
   0a88b8's delayed collision result supplies landing, never a timeout. */
typedef struct {
    unsigned flags,parent_flags;
    uint8_t state,anim,previous_anim,sub;
    int16_t timer;
    int8_t explosion;
    uint8_t variant;
    float frame,clock[3],material[6]; /* +150, +178..180, +184..198 */
    unsigned motion_frames[2];       /* original +134[0/1] header counts */
} HeliDestruction;
typedef struct {
    int explosion; /* -1 none, otherwise original 1c9330[index], scale 2 */
    unsigned sound; /* 0961b8 source ID, zero none */
    int landed_bounds; /* install source 1c91c8 collision descriptor */
} HeliDestructionEvents;
/* Main state 16/17/18 THEN animation dispatcher 0a4eb4 (30 Hz).
   `landed` is 0a88b8's result, not a guessed duration or world-Y threshold.
   The landing probe is required only when state 16 clock[0] >= 56. */
HeliDestructionEvents heli_destruction_tick(HeliDestruction *,int landed);
typedef struct { int part,motion;float frame,material; } HeliWreckDraw;
/* 0a6710/67f4/6894 draw selection. Parts 0/1 are wreck body passes;
   parts 2..9 are original paired effect trees, not particle substitutes.
   material is the source additive ALPHA term, not a clamped clock. */
int heli_wreck_draws(const HeliDestruction *,const unsigned effect_frames[8],HeliWreckDraw out[8]);
#endif
