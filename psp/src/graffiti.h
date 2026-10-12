#ifndef GRAFFITI_H
#define GRAFFITI_H
#include "stage.h"
#include "spray.h"
#include "spray_hud.h"
#include "spray_score.h"
#include "finish_hud.h"
#include "item_flight.h"
typedef struct { float position[3]; int32_t rotation[3]; uint32_t kind, radial; } SprayAnchor;
/* Original static-city descriptor +4..58 and camera records. Separate
   allocation keeps five volumes per large tag out of every small Spot. */
typedef struct {
    float origin[3];int32_t rotation[3];float scale[3],center[3],facing[3],unused[6]; /* descriptor +40 arrow position, +4c scale */
    uint32_t nvolume;unsigned char volumes[];
} SpotSource;
typedef struct { float x, y, z; unsigned id; int tagged; float yaw; int tex; int hasvol, size; float pts[18], nrm[18]; int area, idx, active, required, pretag;
    int source_data,attached; unsigned style_mask, nstrokes; SprayAnchor anchors[7];
    uint32_t stroke, reveal, complete; int started, revealing; /* 1 player, 2 rival reveal owner */
    float facing[3];unsigned camera_valid;unsigned char camera_volume[176];float camera_matrix[16];
    const float *camera_candidates;unsigned camera_ncandidate;
    float arrow_frame; /* independent 03e6f8 clock; both original clips have 120 frames */
    SpotSource *source; /* owned by graffiti_load/free; NULL for attached GF */
} Spot; /* 0c4ea4 template/runtime fields; size does NOT determine stroke count. */
typedef struct { float x, y, z; unsigned kind; int taken; float frame, scale;
    unsigned set,area,source,respawn;int active,contact;
} Can;
typedef struct CanDrop { struct CanDrop *next; Can can; ItemFlight flight; } CanDrop;
typedef struct Graffiti { Spot *spots; int n, tagged; Stage tags; int have_tags; int near; float progress; int presses; Can *cans; int ncans, cancount; int score;
    /* stick-sequence minigame for medium/large tags (FUN_8c03b0fc / FUN_8c03b91c) */
    int mg_active, mg_stroke, mg_nstrokes, mg_cue, mg_wait, mg_award;
    SprayInput input; float tick_acc; unsigned pending, events;
    float mg_flash; char mg_text[48];
    int mg_setup,actor_ticks,actor_damage,block_large;
    int mg_reveal;float reveal_clock,reveal_step;
    SprayHud hud;unsigned hud_cues[3];float (*hud_sine)(int);
    SprayScore score_hud;int hud_player_state;
    FinishHud finish;
    void (*actor)(void *,struct Graffiti *,Spot *,int,int);void *actor_context;
    unsigned can_sets,can_set,can_areas;int can_capacity;float can_acc;
    CanDrop *can_drops;unsigned ncan_drops,can_area,health_pickups[2];
} Graffiti;
enum { SPRAY_ACT_ENTER,SPRAY_ACT_SETUP,SPRAY_ACT_IDLE,SPRAY_ACT_CUE,
       SPRAY_ACT_MISS,SPRAY_ACT_TICK,SPRAY_ACT_EXIT,SPRAY_ACT_PRE,SPRAY_ACT_INTERRUPT,SPRAY_ACT_SMALL,SPRAY_ACT_DAMAGE,SPRAY_ACT_SOUND,
       SPRAY_ACT_FINISH };
enum {SPRAY_SOUND_SMALL,SPRAY_SOUND_EMPTY,SPRAY_SOUND_CUE,SPRAY_SOUND_FINAL,
      SPRAY_SOUND_SYSTEM=16}; /* + original SYS_SE program, never a PLAY RNG draw */
enum { GRAFFITI_DEFER=1, GRAFFITI_TAG=2, GRAFFITI_FINISH=4, GRAFFITI_RIVAL_FINISH=8 };
int  graffiti_load(Graffiti *g, const char *spots_path, const char *tags_path);
int  graffiti_load_cans(Graffiti *g, const char *path);   /* CAN3 paint + health */
int  graffiti_cans_select(Graffiti *g,unsigned set,unsigned areas);
void graffiti_cans_reset(Graffiti *g,int capacity,int initially_full);
int graffiti_can_drop(Graffiti *,const float *origin,const float *target,unsigned source_kind);
void graffiti_can_drops_free(Graffiti *);
/* returns 1 while spraying (player should hold still) */
int  graffiti_update(Graffiti *g, float px, float py, float pz, float pyaw, int spray_pressed, int cancel_pressed, float ax, float ay, int character, float dt);
/* Recorded controllers already contain 027788's BAM angle and 0277a0's
   magnitude. Do not feed them through the movement-axis mirror. */
int graffiti_update_input(Graffiti *g,float x,float y,float z,float yaw,int pressed,int cancel,int angle,float magnitude,int character,float dt);
/* 01abb8's enemy small-tag branch, using the same live city volumes and
   claim state as the player. Returns the accepted spot index, or -1. */
int graffiti_rival_spray(Graffiti *g,float x,float y,float z,int *cans,int pressed);
void graffiti_reset(Graffiti *g); /* mission retry: no progress/reveal tasks survive */
/* Retire the outgoing score owner once. The caller transfers the returned
   earned points before replacing/resetting a mission; reset itself discards
   outgoing points so they cannot leak into the next owner. */
int graffiti_score_retire(Graffiti *g);
/* 03c05c, called BEFORE the accepted collision's reaction handler. Unlike
   voluntary exit this must not restore normal locomotion over the reaction. */
unsigned graffiti_interrupt(Graffiti *g);
void graffiti_free(Graffiti *g);
#endif
