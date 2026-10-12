#ifndef POLICE_H
#define POLICE_H
#include "police_arrow.h"
/* Police pursuit manager, transcribed from the Dreamcast task 0x9fe (FUN_8c0d828c exec, FUN_8c0d84f0 draw, FUN_8c0d8238 init;
   created by FUN_8c0d80c8) and its route data (mission-set +0x24, tools/jgs_police.py -> setN_police.bin, 'POL1'). */
typedef struct { float pos[3], quad[12], r2; int nadj, adj0; int seg; } PolPt;          /* seg = index of the segment starting here (-1) */
typedef struct { float pos[3], quad[12], r2; int pt, pt2; } PolArea;
typedef struct { float pos[3]; int kind, start, nmem, mem0; float geom[12]; } PolSeg;
typedef struct { float pos[3], rot[3], scl[3]; } PolMem;
typedef struct { int idx; float cost; } PolAdj;
typedef struct { int npts, nareas, nsegs, nmem, nadj; PolPt *pt; PolArea *ar; PolSeg *sg; PolMem *mem; PolAdj *adj; } PolRoute;
typedef struct {
    unsigned char *data; int nrec; PolRoute rec[32];
    const PolRoute *route;              /* +0xa8..+0xb4 (0 = none) */
    int flags;                          /* +0x6c: 1 attack, 2 pursuit, 4 marker visible, 8 marker at the segment */
    int trig; float trig_dist; int trig_a, trig_b;   /* +0x70 / +0xa0 / +0x9c / +0x9e (FUN_8c0d8080 reports) */
    int cnt, cnt2, seg;                 /* +0x74, +0x76, +0x78 */
    float pos[3], prev[3];              /* +0x7c, +0x88: player position this / last tick */
    int frame;                          /* +0xb8: cordon effect frame (wraps at the motion's 120 frames) */
    int stack[16], sp;                  /* 0x8c2fc8b4 / 0x8c1cf2f8: route points from the cordon (stack[0]) to the player (stack[sp]) */
    int attack;                         /* 0x8c2fc8b0: police attack in progress (squads/pedestrians react) */
    /* alert task 0x5a (FUN_8c049484 / FUN_8c049590 / FUN_8c049c7c): level, world marker, animation */
    int level;                          /* +0xc0 (-1 = no task) */
    float marker[3];                    /* +0x70..+0x78 */
    int lstate; float lframe; int lvis; /* +0x6c state, +0xa8 frame, +0xc8 visibility (2 hidden / 3 shown) */
    int lmotion; float arrow_frame;    /* +b8 optional sign motion, +a4 independent arrow clock */
    PoliceArrow arrow; int arrow_tick;
    int log_level, log_flags;                      /* last logged level (debug) */
} Police;
int  police_load(Police *P, const char *path);      /* POL1 (all records of the set) */
void police_free(Police *P);
void police_reset(Police *P);                       /* FUN_8c0d8238 */
void police_route(Police *P, int idx);              /* events 0x1f (record idx) / 0x20 (-1): FUN_8c0d8120 */
void police_report(Police *P, float dist, int a, int b);   /* FUN_8c0d8080: an NPC that sees the player reports its distance */
void police_tick(Police *P, float px, float py, float pz); /* FUN_8c0d828c, one 30 Hz tick */
void police_level_set(Police *P, int level);        /* FUN_8c049c7c */
void police_alert_place(Police *P, float x, float y, float z, int level);   /* FUN_8c049b9c: create (or move) the alert object with a level (events 0x17 / 0x1d) */
void police_alert_kill(Police *P);                  /* FUN_8c0da280 / FUN_8c0d04e8: remove the alert object (events 0x1e / 0x18) */
void police_observe_cordon(Police *,const float view[16],int fov,float (*sin_bam)(int));
#endif
