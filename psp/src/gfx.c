#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspge.h>
#include <stdint.h>
#include "gfx.h"
#include "diagnostics.h"
#include "hang.h"
#include "fps_counter.h" /* 30-Hz presentation pacing; HUD is opt-in in main. */
#include "charanim.h"
#include "ge_submit.h"
#include "ge_writeback.h"
#include "ge_projection.h"
#include "stage.h"

#define BUF_WIDTH 512

/* sceGuGetMemory() also carves transient CPU-skinned character vertices out
 * of this arena.  The exact vehicle/prop banks add enough draw calls to push
 * the old 512 KiB list a few KiB past its end in crowded Shibuya views,
 * corrupting the globals immediately after it. Source final-stage cel
 * splitting now uses up to 856,192 bytes of transient arena/actor vertices
 * alone (whole-scene sanitizer fixture); player, effects and GE commands
 * need their own headroom. Keep a two-MiB list within the 32-MiB target. */
/* The packet cache replaces repeated transient vertices. Its 240 KiB comes
 * from this arena, preserving the 32 MiB memory budget. The remaining
 * arena also lends 57 KiB to cached CPU skin/pose/light workspace. Published
 * vertices keep their own live-list storage. Static city commands borrow a
 * further 516 KiB (including city batch bounds) plus 144 KiB for visibility data.
 * Native pedestrian sidecar/owner-cache scratch borrow another 56 KiB;
 * current city masks, visible list and pedestrian cones borrow 10 KiB. The
 * combined budget stays fixed; WORLD_LIST_BYTES is the remaining capacity. */
/* Charge both the shared Final preparation workspace and its bounded,
   bank-owned index maps here so total renderer memory stays fixed. */
#define WORLD_LIST_BYTES (2u*1024u*1024u-CHAR_GE_PACKET_BYTES-CHAR_GE_PACKET_EXTRA_BYTES-CHAR_GE_GROUP_METADATA_BYTES-CHAR_CPU_WORK_BYTES-CHAR_DRAW_PLAN_BYTES-CHAR_PREPARATION_BYTES-CHAR_MATERIAL_CACHE_BYTES-CHAR_ARROW_BYTES-CHAR_LOD_BYTES-CHAR_PED_SHARED_BYTES-CHAR_FINAL_INDEXED_WORK_BYTES-CHAR_FINAL_INDEXED_MAP_BYTES-CHAR_FINAL_CLIP_BYTES-CHAR_FINAL_CEL_PACKET_BYTES-STAGE_COMMAND_BYTES-STAGE_VISIBILITY_BYTES-STAGE_PREPARED_BYTES-STAGE_PED_PROXY_BYTES)
/* City command packets occupy the tail during gameplay. The garage has
   no city draws, so it can reuse that tail for its larger resident list.
   This replaces stage.c's separate static array: total RAM is unchanged. */
static unsigned int __attribute__((aligned(64))) dlist[(WORLD_LIST_BYTES+STAGE_COMMAND_BYTES)/4];
static unsigned list_capacity=WORLD_LIST_BYTES;
void *gfx_stage_command_memory(void){return (unsigned char *)dlist+WORLD_LIST_BYTES;}
unsigned gfx_list_available(void)
{
    unsigned used=sceGuCheckList();
    return used<list_capacity ? list_capacity-used:0;
}
static unsigned present_vblank;
static void *draw_buffer;
static unsigned title_target;
static int present_started;
static int pending_present,pending_vsync;
static int begin_prepared;
#if JGR_DIAGNOSTICS
static GfxProfile profile;
static unsigned frame_start,render_start;
static unsigned section_start;
static int section_current;
static unsigned logic_start;
static unsigned pending_frame_start,last_present,frame_logic_us;
static int logic_current;
static int capture_enabled;
static unsigned capture_serial;
static GfxFrameSample capture_current,capture_pending,capture_completed;
#endif
void gfx_capture_mode(int enabled)
{
#if JGR_DIAGNOSTICS
    capture_enabled=!!enabled;capture_current=(GfxFrameSample){0};capture_pending=(GfxFrameSample){0};capture_completed=(GfxFrameSample){0};
#else
    (void)enabled;
#endif
}
int gfx_capture_latest(GfxFrameSample *out)
{
#if JGR_DIAGNOSTICS
    if(!capture_enabled || !capture_completed.serial)return 0;*out=capture_completed;return 1;
#else
    (void)out;return 0;
#endif
}
int gfx_capture_submitted(GfxFrameSample *out)
{
#if JGR_DIAGNOSTICS
    if(!capture_enabled || !capture_pending.serial)return 0;*out=capture_pending;return 1;
#else
    (void)out;return 0;
#endif
}
void gfx_frame_start(unsigned now)
{
    begin_prepared=0;
#if JGR_DIAGNOSTICS
    frame_start=logic_start=now;logic_current=LOGIC_OTHER;
    if(capture_enabled){capture_current=(GfxFrameSample){0};capture_current.serial=++capture_serial;}
#else
    (void)now;
#endif
}
void gfx_note_resume(unsigned now)
{
    /* GE/EDRAM restoration belongs to the OS. Retain any pending list and
       its resource pins, but do not count sleep as a slow rendered frame
       or wait against a VBlank count from before suspend. */
    present_started=0;begin_prepared=0;
#if JGR_DIAGNOSTICS
    last_present=0;pending_frame_start=now;
    frame_start=logic_start=0;profile=(GfxProfile){0};
    capture_current=(GfxFrameSample){0};capture_pending=(GfxFrameSample){0};capture_completed=(GfxFrameSample){0};
#else
    (void)now;
#endif
}
void gfx_profile_logic(int section)
{
    hang_mark(HANG_LOGIC+(unsigned)section);
#if JGR_DIAGNOSTICS
    if(!logic_start)return;
    unsigned now=sceKernelGetSystemTimeLow();
    profile.logic_section_us[logic_current]+=now-logic_start;
    if(capture_enabled)capture_current.logic_section_us[logic_current]+=now-logic_start;
    logic_start=now;logic_current=section;
#endif
}
void gfx_profile_section(int section)
{
    hang_mark(HANG_DRAW+(unsigned)section);
#if JGR_DIAGNOSTICS
    unsigned now=sceKernelGetSystemTimeLow();
    profile.section_us[section_current]+=now-section_start;
    if(capture_enabled)capture_current.section_us[section_current]+=now-section_start;
    section_start=now;section_current=section;
#endif
}
int gfx_profile_read(GfxProfile *out)
{
#if JGR_DIAGNOSTICS
    if(profile.frames<60)return 0;
    *out=profile;profile=(GfxProfile){0};return 1;
#else
    (void)out;return 0;
#endif
}
void gfx_profile_totals(GfxProfile *out)
{
#if JGR_DIAGNOSTICS
    *out=profile;
#else
    *out=(GfxProfile){0};
#endif
}

void gfx_init(void)
{
    /* The loader zero-fills BSS (where dlist lives) through the cached path,
     * but pspgu writes GE commands through the uncached alias. Flush those
     * dirty lines once, or they can evict later and corrupt the live display
     * list — hangs the GE on real hardware (invisible in PPSSPP). */
    sceKernelDcacheWritebackInvalidateAll();

    sceGuInit();
    /* Swap only inside our scheduled VBlank; do not defer another blank. */
    guSwapBuffersBehaviour(PSP_DISPLAY_SETBUF_NEXTHSYNC);
    sceGuStart(GU_DIRECT, dlist);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)0x88000, BUF_WIDTH);
    sceGuDepthBuffer((void *)0x110000, BUF_WIDTH);
    sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(GE_DEPTH_NEAR, GE_DEPTH_FAR);   /* Full 16-bit reversed depth precision. */
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);          /* GU_FALSE = depth writes ON */
    sceGuShadeModel(GU_SMOOTH);
    sceGuFrontFace(GU_CW);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuDisable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

/* The two 8888 color buffers and the 16-bit depth buffer end at 0x154000.
   Keep the title's temporary target out of the stage texture allocator.
   Texture pins live until stage_free; never evict them during a draw. */
void *gfx_stage_texture_memory(unsigned *bytes)
{
    const unsigned first=BUF_WIDTH*SCR_H*(4+4+2);
    unsigned size=title_target ? title_target:sceGeEdramGetSize();*bytes=size>first ? size-first:0;
    if(!*bytes)return NULL;
    gfx_wait_idle();
    return (void *)(((uintptr_t)sceGeEdramGetAddr()|0x40000000u)+first);
}

void gfx_term(void)
{
    gfx_wait_idle();
    sceGuTerm();
}

/* Finish the previous frame only when its resources must be reused. The
   single list arena and all GE packet pins remain live during game logic. */
void gfx_wait_idle(void)
{
    if(!pending_present)return;
    unsigned phase=hang_mark(HANG_GE_WAIT);
#if JGR_DIAGNOSTICS
    unsigned waiting=sceKernelGetSystemTimeLow();
#endif
    sceGuSync(0,0);chars_frame_end();
#if JGR_DIAGNOSTICS
    unsigned done=sceKernelGetSystemTimeLow();
#endif
    if(pending_vsync) {
        hang_mark(HANG_VBLANK);
        unsigned now;
        do {sceDisplayWaitVblankStart();now=sceDisplayGetVcount();}
        while(!frame_pacer_due(present_vblank,now,present_started));
        present_vblank=now;present_started=1;
    } else present_started=0;
    draw_buffer=sceGuSwapBuffers();
#if JGR_DIAGNOSTICS
    unsigned end=sceKernelGetSystemTimeLow();
    unsigned elapsed=end-(last_present ? last_present:pending_frame_start);
    last_present=end;
    profile.frames++;profile.ge_us+=done-waiting;profile.vblank_us+=end-done;
    if(elapsed>profile.max_frame_us)profile.max_frame_us=elapsed;
    if(capture_enabled && capture_pending.serial) {
        capture_completed=capture_pending;
        capture_completed.ge_us=done-waiting;capture_completed.vblank_us=end-done;
        capture_completed.present_us=elapsed;capture_completed.span_us=end-pending_frame_start;
        if(capture_current.serial!=capture_pending.serial) {
            capture_completed.overlap_logic_us=frame_logic_us;
            for(int i=0;i<LOGIC_SECTIONS;i++)capture_completed.overlap_section_us[i]=capture_current.logic_section_us[i];
        }
        capture_pending.serial=0;
    }
#endif
    pending_present=0;
    hang_mark(phase);
}

void gfx_prepare_frame(void)
{
    if(begin_prepared)return;
#if JGR_DIAGNOSTICS
    unsigned logic_done=sceKernelGetSystemTimeLow();
    if(logic_start){profile.logic_section_us[logic_current]+=logic_done-logic_start;
        if(capture_enabled)capture_current.logic_section_us[logic_current]+=logic_done-logic_start;}
    logic_start=0;frame_logic_us=frame_start ? logic_done-frame_start:0;
#endif
    gfx_wait_idle();
    begin_prepared=1;
}
void gfx_begin(void)
{
    gfx_prepare_frame();
    ge_submit_reset();chars_frame_begin();stage_frame_begin();
#if JGR_DIAGNOSTICS
    render_start=sceKernelGetSystemTimeLow();
    section_start=render_start;section_current=GFX_OTHER;
    if(!frame_start)frame_start=render_start;
#endif
    list_capacity=WORLD_LIST_BYTES;
    sceGuStart(GU_DIRECT,dlist);
    ge_writeback_begin(dlist);
}

void gfx_begin_room(void)
{
    gfx_begin(); /* fences the previous GE list before borrowing its tail */
    stage_forget_commands();
    list_capacity=sizeof dlist;
}

void gfx_end(void)
{
    gfx_end_vsync(1);
}
void gfx_end_vsync(int enabled)
{
#if JGR_DIAGNOSTICS
    unsigned submitted=sceKernelGetSystemTimeLow(),bytes=sceGuCheckList();
    profile.section_us[section_current]+=submitted-section_start;
    if(capture_enabled)capture_current.section_us[section_current]+=submitted-section_start;
#endif
    sceGuFinish();
    ge_writeback_end();
    pending_present=1;pending_vsync=enabled;
#if JGR_DIAGNOSTICS
    pending_frame_start=frame_start;
    profile.logic_us+=frame_logic_us;
    profile.submit_us+=submitted-render_start;
    if(bytes>profile.max_list_bytes){profile.max_list_bytes=bytes;profile.max_list_capacity=list_capacity;}
    if(capture_enabled){capture_current.logic_us=frame_logic_us;capture_current.submit_us=submitted-render_start;
        capture_current.list_bytes=bytes;capture_current.list_capacity=list_capacity;capture_pending=capture_current;}
    frame_start=0;
#endif
    begin_prepared=0;

}

void gfx_mode3d(void)
{
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(GU_CW);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_TEXTURE_2D);
}

void gfx_mode2d(void)
{
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);
    /* Source-material effects can leave additive or colour-factor blending
       selected. The HUD/letterbox pass owns its blend equation; black bars
       disappear if they inherit an additive scene object's equation. */
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDisable(GU_TEXTURE_2D);
}

#include "title_smoothing.inc"

void gfx_clear(unsigned int color)
{
    /* every frame must clear or the GE can hang on real hardware */
    sceGuClearColor(color);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

#define VFMT (GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)

void gfx_quad(float x, float y, float w, float h, unsigned int color)
{
    VertC *v = (VertC *)sceGuGetMemory(2 * sizeof(VertC));
    v[0].color = color; v[0].x = (short)x;       v[0].y = (short)y;       v[0].z = 0;
    v[1].color = color; v[1].x = (short)(x + w); v[1].y = (short)(y + h); v[1].z = 0;
    ge_submit_aux(GU_SPRITES, VFMT, 2, 0, v);
}

void gfx_colour_clamp(unsigned int maximum, unsigned int minimum, unsigned int overlay)
{
    /* PVR clamp registers BC/C0 (224fd6), expressed as two GE RGB
       min/max sprite passes. No readback, texture copy or CPU pixel loop.
       The caller has entered 2D; leave its ordinary alpha blend intact. */
    if ((maximum & 0xffffffu) != 0xffffffu) {
        sceGuBlendFunc(GU_MIN, GU_FIX, GU_FIX, 0xffffff, 0xffffff);
        gfx_quad(0, 0, SCR_W, SCR_H, maximum | 0xff000000u);
    }
    if (minimum & 0xffffffu) {
        sceGuBlendFunc(GU_MAX, GU_FIX, GU_FIX, 0xffffff, 0xffffff);
        gfx_quad(0, 0, SCR_W, SCR_H, minimum | 0xff000000u);
    }
    /* 0fd03c selects destination mode 10; 22a4c0 maps it to PVR
       factor ONE (TSP bits 26..28 = 1). The blue lift is additive. */
    if(overlay>>24) {
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffff);
        gfx_quad(0, 0, SCR_W, SCR_H, overlay);
    }
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}

void gfx_quad_grad(float x, float y, float w, float h,
                   unsigned int tl, unsigned int tr,
                   unsigned int bl, unsigned int br)
{
    VertC *v = (VertC *)sceGuGetMemory(4 * sizeof(VertC));
    v[0].color = tl; v[0].x = (short)x;       v[0].y = (short)y;       v[0].z = 0;
    v[1].color = tr; v[1].x = (short)(x + w); v[1].y = (short)y;       v[1].z = 0;
    v[2].color = bl; v[2].x = (short)x;       v[2].y = (short)(y + h); v[2].z = 0;
    v[3].color = br; v[3].x = (short)(x + w); v[3].y = (short)(y + h); v[3].z = 0;
    ge_submit_aux(GU_TRIANGLE_STRIP, VFMT, 4, 0, v);
}

VertC *gfx_alloc_verts(int n)
{
    return (VertC *)sceGuGetMemory(n * sizeof(VertC));
}

void gfx_draw_sprites(VertC *v, int n)
{
    ge_submit_aux(GU_SPRITES, VFMT, n, 0, v);
}
