#ifndef GFX_H
#define GFX_H

/* GU bring-up and frame management. 3D state is the default inside a
 * frame; switch to 2D (HUD, sky gradient) with gfx_mode2d(). Adapted
 * from Tetralumen's gfx layer, extended for depth-tested 3D. */

typedef struct {
    unsigned int color;
    short x, y, z;
} VertC;

#define GCOLOR(r, g, b, a) \
    (((unsigned int)(r)) | ((unsigned int)(g) << 8) | \
     ((unsigned int)(b) << 16) | ((unsigned int)(a) << 24))

#define SCR_W 480
#define SCR_H 272

void gfx_init(void);
void gfx_term(void);
/* Present/fence any submitted frame before freeing or overwriting GPU data.
   gfx_begin also fences before reusing the single GU arena. */
#if defined(__PSP__) || defined(JGR_TEST_GFX)
void gfx_wait_idle(void);
#else
static inline void gfx_wait_idle(void){}
#endif
/* Fenced unused EDRAM, borrowed until the next level upload. Load-time only. */
void *gfx_stage_texture_memory(unsigned *bytes);
void gfx_begin(void);
/* Same natural fence as gfx_begin, but retain the previous city masks until
   the caller has inspected a completed diagnostic frame. Idempotent. */
void gfx_prepare_frame(void);
/* Garage-only list may reuse the idle city command cache. */
void gfx_begin_room(void);
void *gfx_stage_command_memory(void);
/* Remaining live GU arena, for optional transient geometry paths. */
unsigned gfx_list_available(void);
/* Select before loading a city, after freeing its previous texture pins.
   Title-only 2x2 supersampling borrows EDRAM, never a heap framebuffer. */
void gfx_title_smoothing(int enabled);
unsigned gfx_title_tiles(void);
void gfx_title_tile_begin(unsigned tile);
void gfx_title_tile_end(unsigned tile);
void gfx_end(void);
void gfx_end_vsync(int enabled);
/* Wall-clock measurements on the PSP, including audio-thread preemption.
   Aggregate only completed frames; counters never drive simulation dt. */
enum { GFX_OTHER, GFX_SKY, GFX_STAGE, GFX_PROPS, GFX_ACTORS, GFX_EFFECTS,
       GFX_HUD, GFX_TAGS, GFX_SECTIONS };
enum { LOGIC_OTHER, LOGIC_PLAYER, LOGIC_RIVALS, LOGIC_NPCS, LOGIC_MISSION, LOGIC_CANS, LOGIC_GANGS,
       LOGIC_SECTIONS };
typedef struct {
    unsigned frames,logic_us,submit_us,ge_us,vblank_us,max_frame_us,max_list_bytes,max_list_capacity;
    unsigned section_us[GFX_SECTIONS];
    unsigned logic_section_us[LOGIC_SECTIONS];
} GfxProfile;
void gfx_frame_start(unsigned now);
void gfx_note_resume(unsigned now);
int gfx_profile_read(GfxProfile *out);
/* Non-destructive totals for a fenced, render-only diagnostic sample. */
void gfx_profile_totals(GfxProfile *out);
typedef struct {
    unsigned serial,logic_us,submit_us,ge_us,vblank_us,present_us,span_us;
    unsigned list_bytes,list_capacity,overlap_logic_us;
    unsigned section_us[GFX_SECTIONS],logic_section_us[LOGIC_SECTIONS];
    unsigned overlap_section_us[LOGIC_SECTIONS];
} GfxFrameSample;
void gfx_capture_mode(int enabled);
int gfx_capture_latest(GfxFrameSample *out);
int gfx_capture_submitted(GfxFrameSample *out);
/* CPU submission wall time, not separate GPU execution time. */
void gfx_profile_section(int section);
void gfx_profile_logic(int section);

void gfx_mode3d(void);   /* depth test+write, cull on, blend off */
void gfx_mode2d(void);   /* depth off, cull off, alpha blend on */

void gfx_clear(unsigned int color);

/* 2D helpers (GU_TRANSFORM_2D, use inside gfx_mode2d) */
void gfx_quad(float x, float y, float w, float h, unsigned int color);
/* EVENT 60/61: per-channel RGB bounds, before the scene's captions. */
void gfx_colour_clamp(unsigned int maximum, unsigned int minimum, unsigned int overlay);
void gfx_quad_grad(float x, float y, float w, float h,
                   unsigned int tl, unsigned int tr,
                   unsigned int bl, unsigned int br);
VertC *gfx_alloc_verts(int n);
void gfx_draw_sprites(VertC *v, int n);

#endif
