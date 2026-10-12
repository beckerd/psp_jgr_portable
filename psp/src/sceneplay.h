#ifndef JGR_SCENEPLAY_H
#define JGR_SCENEPLAY_H
#include "scene.h"
#include "scene_storage.h"
#include "charanim.h"
#include "player.h"
typedef struct {
    int type;uint32_t root,texlist,flags;const char *file;
} SceneModelBinding;
typedef struct {
    const SceneModelBinding *binding;Chars bank,mouth;
    int loaded,mouth_loaded;
} SceneModel;
typedef struct {
    Scene vm;SceneStorage image;Chars actors,effects;int actors_loaded,effects_loaded,transferred,audio_loaded,voice_ref;
    unsigned voice_serial;int audio_error;
    unsigned object_voice_serial;int object_audio_loaded,object_voice_active;
    unsigned object_serial[30];int object_active[30];int object_bank_loaded;
    uint32_t inspection_rng;
    float remainder;
    Chars mouth;int mouth_loaded,recruit_audio_loaded,recruit_voice_ref;
    Chars player_mouth;int player_mouth_loaded;
    unsigned recruit_voice_serial;
    int wide_font_owned;
    SceneModel *models;unsigned model_count;
    int generic,event_audio_loaded;
    unsigned typed_serial[30];int typed_active[30];
    unsigned aux_serial[32];int aux_active[32];
    unsigned effect_serial[16];int effect_active[16];
    Chars flare,blast,alternate;int flare_loaded,blast_loaded,alternate_loaded;
} ScenePlay;
void sceneplay_free(ScenePlay *);
int sceneplay_preflight(int id,unsigned *bytes); /* validate only; gameplay keeps its assets until play */
int sceneplay_begin(ScenePlay *,int id,int character,int preload);
int sceneplay_event_begin(ScenePlay *,int id,int character,int secondary,int preload);
/* Recovered recruitment route. Unsupported IDs fail closed. */
int sceneplay_recruit_begin(ScenePlay *,int id,int character,int recruit_mode,int preload);
/* Recovered final-stage EVENT path. Object audio is reported separately;
   this API does not mark either descriptor as campaign-fidelity complete. */
int sceneplay_final_begin(ScenePlay *,int id,int character,int preload,uint32_t *rng);
int sceneplay_status(const ScenePlay *);
int sceneplay_resident(const ScenePlay *);
/* A phase-0f preload owns storage, but gameplay/audio/pause stay live. */
static inline int sceneplay_blocks_gameplay(const ScenePlay *p)
{ return p->image.pages && p->vm.phase!=0xf; }
/* Explicit diagnostic entry only. Does not enable a campaign ending. */
int sceneplay_inspect(ScenePlay *,int id,int character);
int sceneplay_tick(ScenePlay *,Player *,float dt);
void sceneplay_draw(ScenePlay *,Chars *);
void sceneplay_overlay(const ScenePlay *);
#endif
