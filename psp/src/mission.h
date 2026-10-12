/* Mission interpreter transcribed from the DC game's stage-module / JMDc mission-record code.
   The real mission image (E_MISSIONDATA JMDc file) is loaded as-is and its lists are walked with the game's own semantics:
   FUN_8c03db94 (spot groups per area from +0x64 required / +0x68 excluded / +0x6c pre-tagged), FUN_8c059288 (list registration),
   FUN_8c059748 (per-frame list processors), FUN_8c059858 (event executor, codes 1..0x2d), FUN_8c0c24a8 (mission timer),
   FUN_8c06bdf0 head + FUN_8c06d868/8c06d5e4/8c06d8a0 (clear / fail decision from the result flags 0x8c2c2c30..40). */
#ifndef MISSION_H
#define MISSION_H
#include "graffiti.h"
#include "message_hud.h"
enum { MISSION_PHASE_EXIT = 3 }; /* task 19 phase b/c, without a result */
typedef struct { unsigned st, mid, set, kind, sub; float start[3]; char name[16]; unsigned time_sec, pretag, base, blob_off, blob_len, rec_off; int next; unsigned flags; int yaw; unsigned first; } MissionRec;   /* MIS3 record: one JMDc record; next = chain index (+0xec) or -1 */
typedef struct {
    const MissionRec *rec; unsigned char *img; unsigned base, len, ra; int active;   /* ra = address of the current record inside the image (chain member) */
    int kind, time_left, time_total;                 /* ticks (0x8c2e2e18), 30 per second */
    int f30, f34, f38, f3c, f40;                     /* result flags 0x8c2c2c30..40: f30 clear (ev 0x11), f38/f3c time up (kind 2 / others), f40 fail (ev 0x12) */
    int phase;                                       /* 0 playing, 1 clear (FUN_8c06d718), 2 fail (FUN_8c06d640), 3 exit */
    int fail_code;                                   /* 0x13 flag fail, 0x14 caught (player +0xa8) */
    int tag_prev[6], tag_cur[6], tot_prev, tot_cur;  /* 0x8c2c524c / 0x8c2c5230 / 0x8c2c5248 / 0x8c2c522c */
    int time_prev, time_cur, score;                  /* 0x8c2c5270 / 0x8c2c526c: score thresholds (0x8c1793f0 is the score, fed by the shell) */
    int next_rec, next_set, flag_2faee8, rival_cur;  /* ev 0x14/0x15: loader task 0x1c +0x90/+0x94 = next chain record / set index (-1 = mission done, -2 = unset); ev 0x2c/0x2d, ev 0x26 */
    int retry_rec, retry_set;                        /* ev 0x22/0x23: loader +0x98/+0x9c = record / set reloaded by FUN_8c0ca1dc after a failed phase (retry) */
    int seq;                                         /* FUN_8c059748 round-robin counter (mod 9) */
    int scene_request, scene_preload;                /* 04eab8 PLAY and 04ece4 PRELOAD are distinct */
    float position[3], quad_previous[3];             /* 0c2828 input and 2c5204: previous SLOT-0 sample, not previous physics tick */
    MessageHud message; int message_error;           /* original task 56 (ev 0x10) */
    char sub[6][64]; int nsub; float sub_t;          /* cutscene subtitle lines (ev 0xe/0x1c: EVENT_NNNN dialog from psp/events.txt) */
    int nreq, ntagged;                               /* HUD: required spots and how many of them are tagged */
    int police_level;   /* events 0x17/0x18 (FUN_8c049c7c) */
    int police_armed;                               /* 1cf658: sampled idle before pursuit */
    int (*police_route)(void *ctx,int record);       /* 0dd4b8/510: success only for a resident route; -1 clears */
    int (*police_status)(void *ctx);                 /* -1 absent manager, otherwise original +6c flags */
    void (*police)(void *ctx, int code, int a1, int a2, int a3, int a4);   /* events 0x17 (alert object at a1..a3, level 0), 0x1d (level a4), 0x18/0x1e (remove), 0x1f/0x20 (route load / clear) -> police.c */
    void (*train)(void *ctx,int code,unsigned route,unsigned frame,unsigned percent); /* 0d05bc, 27/28/29 */
    void (*ped_zone)(void *ctx, int list, int zone, int on);   /* events 6/7: pedestrian list assigned to a zone slot (FUN_8c05a038: spawned while the player's zone neighbours it) */
    int res_life, res_time, res_cans, res_total, res_rank, res_done;   /* results tally; rank 0..5 from the JMDc +0x2c threshold table */
    char log[6][40]; int nlog;
    int trial_result, trial_done1, trial_done2;      /* 2c5c68 result, 2c5c6c/70 latches */
    void *npcs; void (*ped_list)(void *npcs, int list, int on); void (*wave)(void *ctx, int list);   /* event hooks: 6/7 pedestrian groups, 2/3 rivals, 0x21 waves */
    void *ctx; int (*rival_spawn)(void *ctx, int slot, int table, unsigned event); /* 069a7c: nonzero opaque parent handle */
    void (*rival_delete)(void *ctx,unsigned event); /* 069b54, event 0d */
    unsigned (*group_spawn)(void *ctx,int record,unsigned event); /* 06f9a4: distinct parent per spawn event */
    unsigned (*group_graffiti)(void *ctx,unsigned parent,int descriptor); /* 06fb0c: original attached graffiti object */
    void (*group_signal)(void *ctx,unsigned parent,int leave); /* 06fa1c force-trigger / 06f9f4 depart */
    unsigned (*path_spawn)(void *ctx,unsigned event,const unsigned args[7]); /* 0698e0 -> stage callback, args begin at event+4 */
    void (*path_control)(void *ctx,unsigned handle,int opcode); /* 0a delete / 0b resume, task 8a8 */
    void *path_ctx; /* independent manager context; NULL retains legacy ctx */
    void (*path_reset)(void *ctx); /* release borrowed contacts BEFORE image teardown */
    /* Actor handles are opaque, never host/DC pointers. These read only the
       named original fields (6e flags, 14c/150/154 graffiti state). */
    unsigned (*task_word)(void *ctx,unsigned handle,unsigned offset);
    void (*task_delete)(void *ctx,unsigned handle);
    unsigned task_parents;                          /* bit 0: 8a3 groups; bit 1: 8a5 controller rivals; bit 2: 8a8 path objects */
    int object_tagged, object_required;             /* 0aa7c4 -> 2c5d04/08, sampled in slot 1 */
    int extra_tagged;                               /* Legacy, unbound gang back-tags only; NOT mode-3 rivals. */
} MissionState;
unsigned mission_word(const MissionState *M, unsigned off);   /* word of the current JMDc record (+0x18 = start zone) */
/* 042168/0421f0: authored clear pose, optionally selected by current area. */
int mission_result_pose(const MissionState *,int area,float position[3],int32_t angles[3]);
int mission_message_event(MissionState *,unsigned event); /* 05d514, isolated message dispatcher */
int  missions_read(const char *path, MissionRec *out, int max);                 /* returns count, keeps the file for blobs */
/* 041f78: ordinal within THIS JMDc +ec chain, not first+ordinal in the
   global mission menu. NULL leaves the caller's current selection intact. */
const MissionRec *mission_chain_select(const MissionRec *bank, int count,
                                      const MissionRec *current, int ordinal);
/* 0ca2ac state 1: either negative selector ends the chain; a missing
   nonnegative record/set is an error, never an implicit mission clear.
   Return 1 selected, 0 terminal, -1 unavailable. */
int mission_handoff(const MissionRec *bank,int count,const MissionRec *current,
                    int record,int set,int set_count,const MissionRec **selected);
int  mission_start(MissionState *M, const MissionRec *r, const char *path, Graffiti *g);   /* loads the image, filters spots, runs init events */
void mission_tick(MissionState *M, Graffiti *g, int player_caught);             /* one 30 Hz tick while playing */
/* 018690(1), shared with event 11: a queued fail/time-up/special result
   must not be overwritten by an external stage-controller clear. */
int mission_request_clear(MissionState *);
/* 0d050c -> 02ff20(b): an authored boundary leaves through the stage fade,
   not the clear/fail result path. An existing terminal phase owns teardown. */
int mission_request_exit(MissionState *);
void mission_position(MissionState *M, float x, float y, float z);
void mission_rival_result(MissionState *M,unsigned event,int result); /* controller +c4 -> event +0c */
void mission_results(MissionState *M, int score, int health, int health_max, int cans);   /* FUN_8c0361ec bonus formulas (once, on clear) */
void mission_free(MissionState *M);
void mission_path_released(MissionState *,unsigned event,unsigned handle);
/* trial completion word (player +0x61c -> 0x8c2c5c68, FUN_8c09b314): 1 = success runs the record's +0x54 script, 2 = failure runs +0x58 (each once) */
void mission_trial_result(MissionState *M, Graffiti *g, int result);
#endif
