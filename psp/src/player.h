#ifndef PLAYER_H
#define PLAYER_H
#include <stdint.h>
#include <pspctrl.h>
#include "collide.h"
#include "charpose.h"
#include "skatefx.h"
#include "spray_camera.h"
#include "game_controls.h"
#include "graffiti.h"
#include "trick_hud.h"
#include "player_sound.h"
#include "car_pull.h"
#include "police_grab.h"
#include "kogane_water.h"
/* Surface-type sets from the decompile (FUN_8c051f0c / list 0x8c10d974). */
/* grindable rail tops: the surface types FUN_8c0548dc routes to the rail snap FUN_8c05651c (list 0x8c10da48) */
#define SURF_IS_RAIL(s)   ((s) == 3 || (s) == 15 || (s) == 19 || (s) == 20 || (s) == 57 || (s) == 58 || (s) == 59 || (s) == 60)
/* rail sides as classified by the contact query FUN_8c051f0c (no wall ride off these) */
#define SURF_IS_RAIL_SIDE(s) ((s) == 0x18 || (s) == 0x1e || (s) == 0x1f || (s) == 0x20)
int  surf_is_ground(unsigned s);
int  player_anim(int motion_id);   /* game motion id -> PLAY animation index */
/* Character records selected by 0x8c182884 (player +0x8c), NOT the shared
   tuning block at 0x8c17ffc8 (player +0x88). Units per 30 Hz tick. */
typedef struct {
    int grab_damage;float grab_stun_ticks,grab_recover,grab_mash;
    int health_max,can_capacity,health_low; /* original +00 / +0c / +1c */
    float vmax, vmax_dash, vmax_stumble, vmax_stumble_dash, vmax_contact, vmax_contact_dash;
    float stop_speed, max_speed, dash_coast;
    int uphill_limit; float uphill_speed; int uphill_dead; float ramp_uphill_speed;
    int downhill_limit; float downhill_speed; int downhill_dead; float ramp_downhill_speed;
    float ramp_align_speed; int ramp_align_slow,ramp_align_fast;
    int back_angle;
    float turn_dash_a, turn_dash_b, turn_a, turn_b;
    float ramp_turn_dash_a, ramp_turn_dash_b, ramp_turn_a, ramp_turn_b;
    float jump_h; int jump_ticks; float jump_boost, land_k, jump_max_speed, stumble_bounce;
    float fall_release, rise_fraction; int jump_min;
    float fall_max, air_min_speed, air_brake;
    int air_dead, air_turn, air_back_angle;
    float ground_up, ground_down, shallow_down, air_up;
    float trick_speed;
    float body_radius, body_ground[3], body_air[3];
    int recovery_ticks, wall_damage, fall_damage, water_damage, vehicle_damage,bullet_damage,fire_damage;
    int explosion_damage,taban_damage;float damage_jump_h;
    float wall_radius,wall_distance,wall_up,wall_down,body_wall[3];
    float ramp_min_vy, ramp_launch_speed; int ramp_launch_pitch;
    float rail_up,rail_down,body_rail[3];
} CharTuning;
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_player_tuning.inc"
#else
extern const CharTuning PLAYER_TUNING[16];
#endif
#define BEAT_TUNING PLAYER_TUNING[0]
typedef struct {
    float x, y, z, yaw, speed;  /* yaw in radians; speed in units per tick */
    TrickHud trick_hud;
    SprayCamera spray_camera;
    int spray_active,spray_style,spray_cursor,spray_subvolume;
    int spray_mode,spray_variant; /* source +80 == 2, then 03f926 model publication */
    int small_spray_active,small_spray_ref,pose_overlays_blocked; /* +294/+2b8/+5d0 */
    float small_spray_frame; /* +29c, sampled before advancing */
    float small_spray_target[3]; /* +2a0: accepted small-tag world target */
    int spray_hint_ready,spray_hint_kind; /* 01ab2c -> +28c/+290 */
    float spray_reveal_step; /* +3c4 from 0407d4, consumed by 03bd08 next tick */
    const Chars *spray_bank;
    unsigned spray_used;unsigned char spray_shuffle[24];float spray_matrix[16];
    float spray_subject[3];
    float vy, tick_acc;         /* vertical velocity per tick; accumulated time for 30 Hz stepping */
    int onground, grinding; float rail_dir[3]; int stop_ticks; float grind_dist; unsigned rnd; int health, health_max; float kb; int combo, chain, score_add; float push[2];   /* trick combo +0x3d0, rail chain +0x1fc, points earned this tick (FUN_8c035f70 target) */   /* health +0x8b8/+0x8bc (char record word 0); kb = knock-back accel +0x20c */   /* grind (state 3): rail direction, FUN_8c020924 stop counter (+0x1e8), distance accumulator (+0x3d4) */
    float gn[3],ground_y;       /* normal / last accepted height (+fc), retained in air */
    float surface_motion[3],air_surface_motion[3]; /* source +82c / +838 */
    int slip_yaw,slip_mode;float slip_speed,slip_decay; /* +118..+124 */
    int queued_root_yaw; /* animation turn after the next movement query was queued */
    PoliceGrips police_grips;int police_grab_damage_clock,police_knockdown;float police_recovery;
    CarPull car_pull;
    int (*car_pull_query)(void *,const float point[3],CarPullContact *);
    void *car_pull_context;
    int pitch;                 /* +0x174: contact pitch before the control turn */
    int air_pitch;             /* +208: takeoff slope, retained through the rising phase */
    int stair_pitch,ground_heading; /* +180/+188: upright stair pitch / downhill heading */
    float air_probe[3]; int air_probe_valid; /* +0x40c: previous airborne segment endpoint */
    float body_centres[3][3]; int body_valid;
    int contact_mode, obstacle_cooldown, contact_protected; float contact_speed;
    unsigned contact_surface; /* +18c: selected body-contact material */
    unsigned logic_tick;       /* +0x98, also participates in original trick RNG */
    int rail_detached;         /* +0x1ec: omit mask 4 after a forced rail release */
    int rail_side; float local_shift; /* +1e0 / +844: one-shot lateral rail release */
    unsigned surf;              /* surface type under the player (player +0x178) */
    int state;                  /* 1 control, 2 jump, 9 stumble (player +0x70) */
    int sub;                    /* 0 rising, 1 walk-off fall, 2 jump descent, 3 long fall */
    int fire_pending,burn_ticks,damage_invincible;unsigned fire_hits,bullet_hits;
    int recovery_flash_ticks; /* original +0x890: health pickup, 30 logical ticks */
    int vehicle_recovery;unsigned vehicle_hits;
    int rise_ticks, stun;
    int boost, boost_cool;   /* +0x10c boost, +0x110 cooldown (shared +0xe8 = 60 ticks) */
    int dash_held; /* +108, separate from the timed +10c dash boost */
    int braking, brake_released, brake_forced; float brake_step; /* +210/+214/+218 */
    int brake_contact, contact_headon; /* +21c stop latch; +5b4 current body contact */
    int turn_lock,turn_delay,practice_event; /* +228/+3d8/+894; last is NOT a voice ID */
    float stick_history[2]; /* +0x1b8/+0x1bc: recent magnitudes used by the low-speed release stop */
    float input_mag[9]; int input_yaw[9]; /* +0x1b4/+0x190: trick gesture history */
    unsigned pending_pressed;   /* render-frame edges retained until a physics tick consumes them */
    int jump_held, jump_motion; float jump_start_y, jump_peak_y;
    float rise_gravity, fall_gravity; int air_trick, trick_link;
    int rail_uphill_jump; /* +1f8: uphill rail departure enables recatch speed loss */
    int logical_motion, character_id, motion_done;
    int motion_phase, mirrored, motion_flags;
    float motion_blend, motion_hold, motion_progress;
    /* Simulation clock (+5ac) is advanced AFTER sampling the pose. Keep the
       rendered samples separate, including the frozen first-clip blend. */
    int pose_motion, pose_motion2;
    float pose_frame, pose_frame2, pose_blend;
    /* Original locomotion graph: +17c cadence, +1d8 lean, +114 boost,
       +5b0 skate/dash blend, +224 coast slope blend, +5b8 entry fade. */
    float skate_rate, skate_lean, skate_dash, skate_boost, coast_blend;
    float pose_dash, pose_boost, entry_weight, pose_entry_weight;
    const Chars *pose_bank; int pose_model, pose_valid;
    CharPose pose[64], entry_pose[64];
    int overlay_logical, overlay_mirror, overlay_ticks, overlay_motion; /* +5dc is an animation reference, despite legacy field name */
    int stumble_ticks;           /* +5d4 movement penalty, independent of overlay +5d8 */
    float overlay_frame, overlay_blend;
    int wall_side, wall_used; float wall_origin[3],wall_normal[3];
    int grind_pose;
    int visual_angles[3],visual_valid; /* +678/+67c/+680, separate from travel direction */
    SkateEffects effects;
    int ramp_active, ramp_axis, ramp_target, ramp_turn, ramp_rx, ramp_rz;
    int motion; float frame;    /* PLAY animation index + frame */
    float px, py, pz;
    int world_stage;
    float portal_pending[3],portal_translation[3]; /* +81c / +820, deferred passage crossing */
    float cam_ex, cam_ez, cam_yaw; int cam_init;   /* the object's own follow camera (camera task +0x88 eye / +0xc4 yaw): 43-unit leash (30 at the spawn), trace17 */
    float (*camera_tick)(void *);void *camera_context;int camera_reset;
    const CharTuning *tun;
    /* A world-owned 30 Hz step for the controlled actor only. This keeps
       moving collision and the player paired when a render frame catches
       up more than one simulation tick; NPCs must not advance the world. */
    int (*before_tick)(void *);void *before_tick_context;
    int stage_fall_recovery;
    int water_enabled,water_area,water_sector;WaterRecovery water;
    int reset_query; /* source +90: one reset update without collision queries */
    void (*after_tick)(void *);void *after_tick_context;
    /* Observe actual authored surface contacts without changing response. */
    void (*surface_contact)(void *,int);void *surface_contact_context;
    unsigned ground_ticks; /* +f8: mode-8 calls since 06bd04, NOT idle time */
    unsigned idle_ticks, idle_thresholds[2]; /* +5c4; externally supplied shared movement delays */
    void (*trial_event)(void *,int);void *trial_context;
    unsigned grind_voice_ticks; /* +1f0; reset on each original rail entry */
    int sound_blocked,sound_contact_override; /* 2ae160 / +548 adapter */
    int shallow_pending;float shallow_point[3]; /* deferred mask-8 contact, +544/+54c */
    void (*sound_event)(void *,int,int);void *sound_context;
    void (*sound_spatial)(void *);
} Player;
void player_init(Player *p, float x, float y, float z);
void player_effects_tick(Player *p,const SkateInput *recorded);
void player_restart_score(Player *p);
void player_set_character(Player *p, int character_id);
void player_bind_animation(Player *p,const Chars *bank,int model);
void player_rival_wait(Player *p); /* 027224: logical motion 3 while its controller waits */
/* 06d718 -> 06d8e4: original clear animation, then hold its final pose. */
void player_result_begin(Player *,const float position[3],const int32_t angles[3]);
void player_reset_controls(Player *p); /* clear queued input when restarting/teleporting */
void player_reposition(Player *p,const float position[3]); /* 01b2c0(position,NULL,1), preserved heading/stats */
void player_trace_fixture(Player *p,int wall); /* opt-in debug fixture; original walk-off/jump entry */
void player_cam_reset(Player *p);   /* place the camera 30 behind the current heading (spawn / teleport) */
struct FollowInput;
void player_follow_input(const Player *,struct FollowInput *);
void player_root_matrix(const Player *p,float *matrix); /* row-major T * Rx * Rz * Ry */
void player_motion_angles(const Player *p,int32_t angles[3]); /* +16c/+168/+170, before visual smoothing */
int player_sound_points(const Player *p,float out[2][3]); /* original anchors 18 / 28 */
void player_scene_matrix(const float *position,const int32_t *angles,const float *scale,float *matrix); /* 27a06c mode 0: T*Rz*Ry*Rx*S */
/* Ninja 27a7c8 / 27d6a8. View output is row-major PSP coordinates
   (Y up, -Z forward), not Ninja's column-major Y-down/+Z matrix. */
void player_camera_matrix(const float *position,const float *direction,int32_t roll,float *matrix);
float player_camera_focal(int32_t angle,float width,float scale_x);
float player_fsca_sin(int angle); /* shared exact SH-4 ROM values, BAM input */
float player_recovery_flash(const Player *p);
int player_bam_atan2(float y,float x); /* original 103df0 quadrant search */
void player_scene_handoff(Player *p,const float *position,const int32_t *angles,int clip,float frame);
/* 0b5b58 / 0b5cf4: source moving-triangle displacement and BAM yaw delta.
   old==NULL is a static surface. Returns 0 on degenerate input (the original
   loops forever if all three projected determinants are <=0.1). */
int player_surface_motion(const float *current,const float *old,const float *point,float *delta,int *yaw);
void player_hit(Player *p, float dx, float dz);   /* Legacy NPC push approximation. Original 03d3a0 is the water-return handler, now ported separately. */
int player_bullet_vulnerable(const Player *);
int player_receive_vehicle(Player *,int mode,int yaw);
int player_receive_explosion(Player *,int mode,int yaw);
/* Attribute-29 fire: original health/overlay response; leaves the movement
   callback represented by state intact. Invulnerability is shared +880. */
int player_fire_hit(Player *);
int player_receive_bullet(Player *);
int player_receive_taban(Player *,int mode,int yaw);
int player_grab_points(const Player *,float out[3][3]);
int player_police_receive(Player *);
void player_spray_actor(void *,Graffiti *,Spot *,int action,int cue);
float player_recorded_spray_reveal_step(const Player *);
void player_small_spray_begin(Player *,const float target[3]);
int player_load_spray_cameras(void);
/* Raw live PSP input only; horizontal nub/D-pad reversed while skating.
   Graffiti follows the screen arrows and retains its /128 axis scaling.
   Shared radial neutral zone; never mutates the caller's pad sample. */
void player_input_axes(const SceCtrlData *pad, float *ax, float *ay, int graffiti);
void player_update(Player *p, const Collision *col, const SceCtrlData *pad, unsigned pressed, float cam_yaw, float dt);
/* same with raw stick axes (no dead zone): recorded-input playback (controller modes 2/3/4 have no axis dead zone) */
void player_update_axes(Player *p, const Collision *col, float ax, float ay, unsigned buttons, unsigned pressed, float cam_yaw, float dt);
/* Main-thread wall time, aggregated over all player/rival ticks. Sections
   are inclusive (e.g. audio callbacks made by animation also count there). */
typedef struct {unsigned ticks,total_us,animation_us,body_us,ground_us,audio_us,camera_us,effects_us;
                unsigned body_prepared_bones,body_reused_bones;} PlayerProfile;
void player_profile_read(PlayerProfile *out);
void player_profile_totals(PlayerProfile *out); /* current interval; does not reset */
/* 0fe4d0: electric-wave routing through the shared original rail projection. */
int player_wave_surface(const Collision *,const ColHit *,int,float [3],int *);
#endif
