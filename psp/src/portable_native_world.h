#ifndef JGR_PORTABLE_NATIVE_WORLD_H
#define JGR_PORTABLE_NATIVE_WORLD_H
#include "portable_tables.h"
#include "player_follow.h"
#include "ctrl.h"
#include "portable_replay_pack.h"
#include "portable_native_cache.h"

/* Setup-only context. Every ordinary asset is opened through dependency once
 * before a path-only runtime loader receives its bounded installed path. */
typedef struct {
    JgrDisc *disc;
    const char *asset_directory,*scratch_directory;
    JgrTableDependency dependency;void *dependency_user;
    int (*progress)(unsigned long long,void *);void *progress_user; /* nonzero continues */
    char *error;size_t error_size;
    JgrNativeCache *cache; /* Optional session-owned collision scratch and pose bank. */
} JgrNativeConfig;
typedef struct JgrNativeWorld JgrNativeWorld;
int jgr_native_error(const JgrNativeConfig *,const char *);
int jgr_native_asset_path(const JgrNativeConfig *,const char *,char *,size_t);
void *jgr_native_asset_read(const JgrNativeConfig *,const char *,size_t *,size_t);
int jgr_native_bank_load(const JgrNativeConfig *,Chars *,const char *);
/* Use the loading configuration so a shared view releases its session owner.
 * Spray aliases must be destroyed before their local owner descriptor. */
void jgr_native_bank_free(const JgrNativeConfig *,Chars *);
int jgr_native_graffiti_load(const JgrNativeConfig *,Graffiti *,unsigned stage);
void jgr_native_graffiti_free(Graffiti *);
int jgr_native_player_model(const JgrNativeConfig *,unsigned,unsigned *);
int jgr_native_player_parameters(const JgrNativeConfig *,int32_t surfaces[19],unsigned idle_thresholds[2]);
int jgr_native_collision_load(const JgrNativeConfig *,Collision *,unsigned);
int jgr_native_objects_load(const JgrNativeConfig *,Collision *,unsigned,unsigned,unsigned);
JgrNativeWorld *jgr_native_world_create(const JgrNativeConfig *,Collision *,Player *,PlayerFollow *,
    Chars *,unsigned model,unsigned stage,unsigned set,unsigned subset,const char *mission,unsigned record);
int jgr_native_world_step(JgrNativeWorld *,const CtrlPlay *,float,float,unsigned,unsigned);
/* Mode-3 actors do not consume the spectator's cans or enter player tags. */
int jgr_native_world_rival_step(JgrNativeWorld *,const CtrlPlay *,float,float,unsigned,unsigned,int race);
int jgr_native_world_rival_wait(JgrNativeWorld *);
void jgr_native_world_destroy(JgrNativeWorld *);
int jgr_native_sample(const Player *,const PlayerFollow *,unsigned part,unsigned cut,JgrReplayNativeSample *);
#endif
