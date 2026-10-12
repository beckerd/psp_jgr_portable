#ifndef JGR_SCENE_H
#define JGR_SCENE_H
#include <stdint.h>
#include <stddef.h>
#define SCENE_PAGE_SHIFT 12u
#define SCENE_PAGE_SIZE (1u << SCENE_PAGE_SHIFT)

/* Original EVENT image addresses stay virtual. No pointers into Dreamcast
   memory are dereferenced on the PSP. Scene commands are 20 bytes, NOT the
   32-byte JMDc event records. The VM is independent of rendering and I/O. */
typedef struct {
    int active, type, motion, old_motion, mode, next_mode;
    uint32_t track, raw_motion, old_raw_motion;
    int variant;
    float pos[3], scale[3], frame, old_frame, blend, track_frame;
    int32_t angle[3];
} SceneActor;
typedef struct {
    int active,kind,bank,reference,remaining,character,mouth_mode;
    unsigned serial;
    float mouth_frame;
} SceneTypedSound;
typedef struct {
    SceneActor actor;
    int character,pingpong,forward,companion_mode;
    float companion_frame;
    int voice_ref,voice_remaining;unsigned voice_serial;
} SceneRecruit;
typedef struct {
    int mode,infinite,x,y;
    float remaining;
    char text[256];
} SceneCaption;
/* 0ccaf8 actors use their own 30-entry table (2fae58), distinct from
   the game-character table used by commands 21..2c. */
typedef struct {
    SceneActor transform;
    uint32_t root,texlist,flags,njs_motion;
    int voice,voice_ref,voice_remaining;unsigned voice_serial;
    unsigned voice_kind,voice_bank;
} SceneObject;
typedef struct {
    int active, mode, reverse;
    uint32_t initial, track;
    float frame, pos[3], forward[3], near_clip, far_clip;
    int32_t roll, fov;
    float offset[3];int32_t angle[3];
} SceneCamera;
typedef struct {
    int active,mode,type;unsigned serial,sound_serial;
    float pos[3],scale,frame[2],alpha_offset[2],age;
    int32_t yaw;
} SceneEffect;
typedef struct Scene {
    const unsigned char *image;
    uint32_t length, base, pc, scripts[3], flags;
    int id, phase, return_phase, error, language, character, handoff;
    float clock, wait, limit;
    SceneActor player, actors[30], result_player;
    SceneTypedSound typed_sound[30];
    int player_mouth_mode;float player_mouth_frame;
    SceneRecruit recruit;SceneCaption caption;
    SceneCaption older_captions[7];
    int recruit_mode,mission_context;
    float (*sin_bam)(int);
    SceneObject objects[30];
    SceneEffect effects[16];unsigned effect_serial;
    struct SceneAuxEffect {
        int active,kind,mode,slot,remaining,mesh,frame;
        unsigned serial,sound_serial;float age,pos[2][3];uint32_t track[2];
    } aux[32];unsigned aux_serial;
    unsigned (*random15)(void *);void *random_ctx;
    SceneCamera camera, result_camera;
    uint32_t text, texture_loader;
    int voice_ref,voice_remaining;unsigned voice_serial;
    int text_x, text_y;
    char dialog[512];int dialog_cursor,dialog_wait,dialog_mode;
    /* 026b4c/026bc0 are letterbox transitions, not full-screen fades. */
    float letterbox;int letterbox_angle,letterbox_step;
    /* 0fc920/0fca24/0fccc8 -> PVR FOG_CLAMP_MAX/MIN, not vertex tint. */
    int colour_phase,colour_active,colour_overlay;
    float clamp_max[4],clamp_min[4],clamp_max_step[4],clamp_min_step[4],overlay_step;
    uint32_t colour_max,colour_min,overlay_argb;
    void *ctx;
    void (*command)(void *ctx, const struct Scene *, const uint32_t command[5]);
    /* EVENT playback owns small pages; other source consumers may still
       supply a contiguous image. Both retain the original virtual addresses. */
    unsigned char *const *pages;
} Scene;
uint32_t scene_word(const Scene *s, uint32_t address);
int scene_range(const Scene *s, uint32_t address, size_t length);
int scene_read(const Scene *s, uint32_t address, void *out, size_t length);
/* Direct pointer only when the entire range is contiguous. Use scene_range
   for validation and scene_read for copies that may cross an EVENT page. */
const void *scene_bytes(const Scene *s, uint32_t address, size_t length);
int scene_start(Scene *s, const void *image, size_t length, uint32_t base,
                int id, uint32_t flags, int character);
int scene_start_pages(Scene *s, unsigned char *const *pages, size_t length, uint32_t base,
                      int id, uint32_t flags, int character);
/* Setup and main playback are separate, like 04ece4 preload vs 04eab8 play.
   Host asset I/O completes before scene_play; I/O latency is not story time. */
void scene_play(Scene *s);
/* Union every cue in setup, playback and cleanup, including skipped branches. */
int scene_voice_usage(const Scene *,uint32_t *primary,uint32_t *recruit);
void scene_tick(Scene *s);
int scene_busy(const Scene *s);
/* Production routes require original task traces AND resident asset bindings.
   This is deliberately narrower than the VM's supported opcode inventory. */
static inline int scene_supported(int id)
{ return id==4 || id==7 || id==74 || id==97 || id==102; }
static inline int scene_bespoke_supported(int id)
{
    switch(id) {
    case 0:case 1:case 8:case 9:case 10:case 23:case 24:case 25:case 26:
    case 32:case 33:case 34:case 35:case 36:case 41:case 42:case 43:case 44:
    case 49:case 50:case 59:case 60:case 61:case 62:case 73:case 75:case 78:
    case 79:case 80:case 84:case 85:case 88:case 89:case 91:case 92:case 98:case 99:return 1;
    default:return 0;
    }
}
#include "scene_dialogue.inc"
static inline int scene_recruit_supported(int id,int mode)
{ return (id==12 && mode==1) || ((id==13 || id==56) && mode==2) || (id==14 && mode==3) ||
         (id==11 && mode==6) || (id==45 && mode==5) || (id==51 && mode==10) ||
         (id==69 && mode==13) || (id==70 && mode==9) || (id==81 && mode==8); }
int scene_clip(int reference);
int scene_frames(int character, int clip);
void scene_sample_actor(const Scene *, uint32_t track, float frame, SceneActor *);
void scene_sample_camera(const Scene *, uint32_t initial, uint32_t track, float frame, SceneCamera *);
struct CharPose;
/* Sparse NJS_MDATA3/type 7 (EVENT/traffic) and MDATA2/type 3 (traffic).
   The caller supplies bind TRS.
   cel selects the original 0bfb24 path, not Ninja's 236ab6/23730e path. */
int scene_sample_njs(const Scene *,uint32_t motion,float frame,unsigned bone,int cel,struct CharPose *);
/* Prepared 0bfb24/cel sampling. The scene and its immutable key image must
   outlive the context. Optional caller-owned hints are three upper-key
   indices per bone, never pointers; zero or stale indices are safe. */
typedef struct {
    const Scene *scene;
    const unsigned char *mdata_bytes;
    uint32_t mdata,frames,channels,next,nodes;
    float frame,wrap_t;
} SceneNjsCelSample;
int scene_njs_cel_prepare(SceneNjsCelSample *,const Scene *,uint32_t motion,float frame,unsigned nodes);
int scene_njs_cel_sample(const SceneNjsCelSample *,unsigned bone,uint32_t hint[3],struct CharPose *);
int scene_sample_cnk(const Scene *,uint32_t motion,float frame,unsigned bone,struct CharPose *);
/* DEMO set hierarchies with an explicit validated node count (up to 512). */
int scene_sample_njs_nodes(const Scene *,uint32_t motion,float frame,unsigned bone,int cel,unsigned nodes,struct CharPose *);
/* 0bea0c/0bf0b4 sparse collision traversal; caller supplies collider bind TRS. */
int scene_sample_collision(const Scene *,uint32_t motion,float frame,unsigned bone,struct CharPose *);
int scene_effect_init(SceneEffect *,int type,const float pos[3],unsigned random15);
void scene_effect_tick(SceneEffect *);
typedef struct { int x,y;uint32_t argb;uint16_t character; } SceneGlyph;
/* 0c0fb0: source-space ASCII layout, $m=32 pixels, $n=24 and 1aac colours. */
unsigned scene_dialog_glyphs(const Scene *,SceneGlyph *,unsigned capacity);
unsigned scene_caption_glyphs(const Scene *,SceneGlyph *,unsigned capacity);
#endif
