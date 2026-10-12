#ifndef FINALARENA_H
#define FINALARENA_H
#include "finalcoll.h"
#include "charanim.h"
#include "player.h"
#include "finalfall.h"
#include "finalboss.h"
#include "finalpiece.h"
#include "finalprogress.h"
#include "finaltags.h"
#include "finalhazard.h"
#include "finalemitter.h"
typedef struct {
    Chars models,actors,hazard_models,emitter_models; FinalBoss boss;uint32_t boss_rng;
    FinalHazard hazard;
    FinalEmitter emitter;FinalEmitterContact emitter_contact;
    int emitter_alive;unsigned emitter_request,emitter_serial,fire_contacts;
    float emitter_radius;
    int (*attack_sound)(void *,unsigned,int);void *attack_sound_context;
    /* reference >=0 starts, -1 follows/polls, -2 stops. Return live=1. */
    int (*boss_voice)(int reference,const float position[3]);
    FinalCol collider,boss_collider; unsigned char *image,*boss_image;
    FinalPiece pieces[4];unsigned tagged;unsigned char *collapse_image;
    int loaded,error,phase,cinematic; float frames[6],background,remainder,boss_world[16];
    Collision *world;
    Player *player;FinalFall fall;
    FinalTags tags;unsigned char *tag_image;Graffiti *graffiti;
    FinalProgress progression;int scene_status,event_alive,clear_requested,progression_error;
    float progress_remainder;
    int (*scene_begin)(void *,int,int);void *scene_context;
} FinalArena;
/* Window totals, read/reset alongside GfxProfile. All times are CPU wall
   microseconds. collision/publish/tags/other partition update_us+scene_us;
   after_us is separate. Draw subphases are inside draw_us; fire_us is separate.
   Loading/reset preparation is excluded. No per-bone/vertex clocks are used. */
typedef struct {
    unsigned updates,scene_updates,ticks,scene_ticks;
    unsigned collision_samples,world_publishes,world_triangles,tag_prepares,after_ticks;
    unsigned update_us,scene_us,collision_us,publish_us,tags_us,other_us,after_us;
    unsigned draw_calls,fire_calls;
    unsigned draw_us,hazard_us,background_us,platforms_us,center_us,boss_us,fire_us;
    unsigned boss_root_us,boss_surface_us,goji_us,attachments_us,orbit_parts_us;
} FinalArenaProfile;
void finalarena_profile_read(FinalArenaProfile *);
/* Recovered arena, moving targets and phase controller. Normal campaign entry
   uses these handlers; inspection fixtures cannot grant campaign progress. */
int finalarena_load(FinalArena *a,Collision *world);
int finalarena_bind_tags(FinalArena *,Graffiti *);
int finalarena_reset(FinalArena *);
/* Runs the recovered parent branch once. Host must publish EVENT residency
   separately from completion status. Failed scene I/O never grants a clear. */
int finalarena_progress(FinalArena *);
static inline int finalarena_clear_ready(const FinalArena *a)
{return a && a->loaded && !a->error && !a->progression_error && a->phase==8 && a->clear_requested && !a->event_alive;}
void finalarena_free(FinalArena *a);
int finalarena_update(FinalArena *a,Collision *world,float dt);
int finalarena_scene_update(FinalArena *a,float dt);
/* Explicit inspection fixture; never used to complete normal gameplay tags. */
int finalarena_inspect_tag(FinalArena *,unsigned piece);
/* Only after all four inspected collapses finish. No story or save advance. */
int finalarena_inspect_finalform(FinalArena *);
/* Prepare a late final-form scene fixture by running the original collapse
   handlers first. Explicit inspection, not a campaign completion shortcut. */
int finalarena_inspect_scene(FinalArena *,int scene);
int finalarena_tick(void *context);
void finalarena_after_tick(void *context);
void finalarena_draw(FinalArena *a);
void finalarena_draw_fire(FinalArena *,const float forward[3]);
#endif
