#ifndef JGR_ENEMYCOMBAT_H
#define JGR_ENEMYCOMBAT_H
#include <stdint.h>
#include "enemyshot.h"
#include "enemy_missile.h"
#include "enemy_shell.h"
#include "enemy_wave.h"
#include "gang.h"
#include "player.h"
#include "enemy_warning.h"
#include "enemy_gas.h"
#include "enemy_fire.h"
#define ENEMY_FIRE_CAPACITY 16
#define ENEMY_BURN_CAPACITY 64
#include "tex2d.h"
#include "charpose.h"
typedef struct {
    struct {int active,age,yaw,variant;float pos[3];} blasts[16];
    float chain_centers[16][3];unsigned chain_count;
    float body_previous[20][3];int body_valid,body_track;
    float feet_history[3][3];int feet_valid;
    float police_spheres[20][4],police_centers[20][3];int police_body_ready;
    EnemyGases gases;
    Tex2dSet taban_texture;int taban_attempted,taban_error;
    Chars gas_models;int gas_attempted,gas_error,gas_skin,gas_camera_yaw;
    Chars burning_models;int burning_attempted,burning_error;
    EnemyFire *fires;unsigned fire_serial,fire_tick,fire_created,fire_dropped;
    EnemyBurn burns[ENEMY_BURN_CAPACITY];unsigned burns_created,burns_dropped;
    PlayerBurn player_burn;unsigned player_fire_seen;
    Chars flame_models;int flame_attempted,flame_error;
    EnemyShots shots;EnemyMissiles missiles;EnemyShells shells;EnemyWaves waves;GangCombat gang;
    Player *player;const Collision *collision;Chars *actors;Chars models,warning_models;
    EnemyWarning warning;int warning_attempted,warning_error;
    unsigned random;int error,attempted;
    int pending_attribute,pending_mode,pending_yaw;
    unsigned received,explosions,audio_errors;int last_audio_error;
} EnemyCombatWorld;
void enemycombat_bind(EnemyCombatWorld *,Player *,const Collision *,Chars *);
void enemycombat_tick(EnemyCombatWorld *);
int enemycombat_receive(EnemyCombatWorld *);
void enemycombat_reset(EnemyCombatWorld *);
void enemycombat_free(EnemyCombatWorld *);
void enemycombat_draw(EnemyCombatWorld *,const float *view,const float planes[6][4]);
/* Translucent player attachment: submit after opaque actors/props. */
void enemycombat_draw_player_fire(EnemyCombatWorld *,const float view[16],const float planes[6][4]);
void enemycombat_draw_taban(EnemyCombatWorld *,const Gang *,const float view[16],const float planes[6][4]);
void enemycombat_draw_sniper(EnemyCombatWorld *,const Gang *,const float view[16],const float planes[6][4]);
/* Potential GRP1 users of the shared projectile/explosion bank. For
   formation types 26/28/29/30, the caller must also find an actual member
   after the leader; 26/29 entries with nonzero pose are parked vehicles. */
int enemycombat_type_uses_projectiles(int group_type);
/* Call during mission loading, including enemies scheduled to spawn later.
   Returns 1 when ready, 0 on failure (details in world->error). Shares the
   attack callbacks' resident bank. A failed preload releases partial data
   and allows the normal first-use attempt later; an existing attack-time
   failure stays cached until enemycombat_free. No prior bind is required. */
int enemycombat_prepare_projectiles(EnemyCombatWorld *);
/* Release idle model storage before a presentation needs the heap. Live
   shots/effects keep their bank. Returns 1 if released; never resets actors,
   callbacks, cached animation lengths or an existing failed load attempt. */
int enemycombat_release_idle_projectiles(EnemyCombatWorld *);
enum {
    ENEMY_GRAPHICS_WARNING=1u, ENEMY_GRAPHICS_GAS=2u,
    ENEMY_GRAPHICS_CHAIN=4u, ENEMY_GRAPHICS_FLAME=8u,
    ENEMY_GRAPHICS_BURNING=16u
};
/* GRP1 creator kinds, after the caller excludes empty/parked formations.
   Includes only these extra banks; projectiles use the API above. */
unsigned enemycombat_graphics_for_kinds(uint64_t kinds);
/* Optional loading-only call, after mandatory assets and under the caller's
   held heap reserve. Returns the requested banks that are ready. Reuses the
   normal first-use loaders; failed new attempts release partial allocations
   and remain retryable. Existing attack-time failures remain memoized. */
unsigned enemycombat_prepare_graphics(EnemyCombatWorld *,uint64_t kinds);
/* Release unused extra banks before EVENT allocations. Live gas/warnings,
   fire pools and actors that directly use the banks retain them. Does not
   reset simulation state; returns a mask of the released banks. */
unsigned enemycombat_release_idle_graphics(EnemyCombatWorld *,const Gang *,unsigned count);
int enemycombat_prepare_denki(EnemyCombatWorld *);
int enemycombat_prepare_burning(EnemyCombatWorld *);
int enemycombat_draw_strong(EnemyCombatWorld *,const Gang *,const GangMember *,const float root[16],const float planes[6][4]);
void enemycombat_fire_pose(const Chars *,const JgcChar *,const GangMember *,CharPose *);
int enemycombat_prepare_flames(EnemyCombatWorld *);
void enemycombat_draw_hands(EnemyCombatWorld *,const GangMember *,const float root[16],const CharPose *,const float view[16],const float planes[6][4]);
void enemycombat_draw_bomb(EnemyCombatWorld *,const Gang *,const GangMember *,const float planes[6][4]);
void enemycombat_draw_denki(EnemyCombatWorld *,Gang *,const float planes[6][4],int advance);
#endif
