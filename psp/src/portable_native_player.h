#ifndef JGR_PORTABLE_NATIVE_PLAYER_H
#define JGR_PORTABLE_NATIVE_PLAYER_H
#include "player.h"
#include "player_follow.h"
#include "ctrl.h"
#include "player_body.h"
/* Synchronous install-only player. Original scalar tables are loaded by the
 * caller; exact animation arithmetic never changes the gameplay renderer. */
typedef int (*JgrNativeContact)(void *,Player *,int handler_state);
typedef int (*JgrNativeAnimationObserver)(void *,Player *,int completed);
void jgr_native_player_contact(Player *,JgrNativeContact,void *);
void jgr_native_player_observe(Player *,JgrNativeAnimationObserver,void *);
int jgr_native_player_observer_failed(void);
void jgr_native_player_animation(Player *);
void jgr_native_player_visual(Player *);
int jgr_native_player_landing_surfaces(const int32_t *,unsigned count);
int jgr_native_player_collision_surfaces(const int32_t *,unsigned count);
void jgr_native_player_motion(Player *,int motion);
int jgr_native_player_motion_valid(int motion,int overlay_reference,int overlay_ticks);
void jgr_native_player_spawn(Player *,const Chars *,unsigned model,unsigned character,
                             const CtrlStream *,unsigned stage,const unsigned idle_thresholds[2]);
void jgr_native_player_follow_bind(PlayerFollow *,Player *,const Collision *,const int32_t surfaces[19]);
void jgr_native_player_follow_spray(PlayerFollow *);
void jgr_native_player_spray(void *,Graffiti *,Spot *,int action,int cue);
int jgr_native_player_spray_tracks(const void *,unsigned bytes);
void jgr_native_player_effect_input(const Player *,SkateInput *);
int jgr_native_player_sample_pose(const Player *,unsigned joint,CharPose *);
int jgr_native_player_body_matrices_masked(const Player *,float matrices[20][16],unsigned mask);
int  jgr_native_surf_is_ground(unsigned s);
int  jgr_native_player_anim(int motion_id);
void jgr_native_player_init(Player *p, float x, float y, float z);
void jgr_native_player_effects_tick(Player *p,const SkateInput *recorded);
void jgr_native_player_restart_score(Player *p);
void jgr_native_player_set_character(Player *p, int character_id);
void jgr_native_player_bind_animation(Player *p,const Chars *bank,int model);
void jgr_native_player_rival_wait(Player *p);
/* Mode-3 controller relocation preserves the animation and published visual
 * history (01ae70 with param3=0); waiting still completes an actor tick. */
void jgr_native_player_rival_relocate(Player *,const CtrlStream *);
void jgr_native_player_rival_wait_tick(Player *);
void jgr_native_player_result_begin(Player *,const float position[3],const int32_t angles[3]);
void jgr_native_player_reset_controls(Player *p);
void jgr_native_player_reposition(Player *p,const float position[3]);
void jgr_native_player_trace_fixture(Player *p,int wall);
void jgr_native_player_cam_reset(Player *p);
void jgr_native_player_follow_input(const Player *,struct FollowInput *);
void jgr_native_player_root_matrix(const Player *p,float *matrix);
void jgr_native_player_motion_angles(const Player *p,int32_t angles[3]);
int jgr_native_player_sound_points(const Player *p,float out[2][3]);
void jgr_native_player_scene_matrix(const float *position,const int32_t *angles,const float *scale,float *matrix);
void jgr_native_player_camera_matrix(const float *position,const float *direction,int32_t roll,float *matrix);
float jgr_native_player_camera_focal(int32_t angle,float width,float scale_x);
float jgr_native_player_fsca_sin(int angle);
float jgr_native_player_recovery_flash(const Player *p);
int jgr_native_player_bam_atan2(float y,float x);
void jgr_native_player_scene_handoff(Player *p,const float *position,const int32_t *angles,int clip,float frame);
int jgr_native_player_surface_motion(const float *current,const float *old,const float *point,float *delta,int *yaw);
void jgr_native_player_hit(Player *p, float dx, float dz);
int jgr_native_player_bullet_vulnerable(const Player *);
int jgr_native_player_receive_vehicle(Player *,int mode,int yaw);
int jgr_native_player_receive_explosion(Player *,int mode,int yaw);
int jgr_native_player_fire_hit(Player *);
int jgr_native_player_receive_bullet(Player *);
int jgr_native_player_receive_taban(Player *,int mode,int yaw);
int jgr_native_player_grab_points(const Player *,float out[3][3]);
int jgr_native_player_police_receive(Player *);
void jgr_native_player_spray_actor(void *,Graffiti *,Spot *,int action,int cue);
void jgr_native_player_small_spray_begin(Player *,const float target[3]);
int jgr_native_player_load_spray_cameras(void);
void jgr_native_player_input_axes(const SceCtrlData *pad, float *ax, float *ay, int graffiti);
void jgr_native_player_update(Player *p, const Collision *col, const SceCtrlData *pad, unsigned pressed, float cam_yaw, float dt);
void jgr_native_player_update_axes(Player *p, const Collision *col, float ax, float ay, unsigned buttons, unsigned pressed, float cam_yaw, float dt);
void jgr_native_player_profile_read(PlayerProfile *out);
void jgr_native_player_profile_totals(PlayerProfile *out);
int jgr_native_player_wave_surface(const Collision *,const ColHit *,int,float [3],int *);
#endif
