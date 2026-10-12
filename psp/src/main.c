#include "diagnostics.h"
#include "portable_assets.h"
#include "savedata.h"
#include "source_data_bubble.h"
#include "source_data_numeric.h"
#include "source_data_pointers.h"
#include "source_data_startup.h"
/* Jet Grind Radio PSP — game shell: title/stage menu, level load/unload, play loop, HUD, self-test log. */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psppower.h>
#include <pspgu.h>
#include <pspgum.h>
#if JGR_PORTABLE_ASSETS
#include <pspdebug.h>
#endif
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <math.h>
#include "gfx.h"
#include "crash.h"
#include "hang.h"
#include "ge_submit.h"
#include "ge_writeback.h"
#include "render_bench.h"
#include "render_capture.h"
#include "ge_projection.h"
#include "font.h"
#include "stage.h"
#include "vfpu_profile.h"
#include "charanim.h"
#include "garage_cast.h"
#include "garage_ui.h"
#include "chapter_card.h"
#include "garage_light.h"
#include "garage_shadow.h"
#include "garage_pending.h"
#include "garage_rival.h"
#include "endingplay.h"
#include "ending_assets_check.h"
#include "movie_assets_check.h"
#include "save_prompt.h"
#include "failure_hud_draw.h"
#include "loading.h"
#include "pause_hud.h"
#include "pause_wipe.h"
#include "record_book.h"
#include "radio_ui.h"
#include "player.h"
#include "player_follow.h"
#include "result_camera.h"
#include "graffiti_arrow.h"
#include "set_collision.h"
#include "audio.h"
#include "gameplay_audio_preload.h"
#include "mission_enemy_preload.h"
#include "event_audio.h"
#include "audio_decode.h"
#include "stream.h"
#include "graffiti.h"
#include "citytags.h"
#include "spray_bubble.h"
#include "citysky.h"
#include "kogane_house.h"
#include "kogane_cover.h"
#include "shibuya_gates.h"
#include "street_props.h"
#include "city_openings.h"
#include "benten_train.h"
#include "benten_crane.h"
#include "grind_props.h"
#include "garage_exit.h"
#include "canplay.h"
#include "npc.h"
#include "npc_visibility.h"
#include "mission.h"
#include "stage_exit.h"
#include "level_exit.h"
#include "exit_fixture.h"
#include "sceneplay.h"
#include "finalarena.h"
#include "result_transition.h"
#include "finalfixture.h"
#include "citysprayfixture.h"
#include "passagefixture.h"
#include "rival_spray.h"
#include "pathmission.h"
#include "traffic_draw.h"
#include "pathaudio.h"
#include "ctrl.h"
#include "trial.h"
#include "tex2d.h"
#include "list2d.h"
#include "demo.h"
#include "title_route.h"
#include "challenge_rival.h"
#include "recruit_audio.h"
#include "recruit_spray.h"
#include "recruit_paint.h"
#include "story.h"
#include "recruit_result.h"
#include "zones.h"
#include "police.h"
#include "police_hud.h"
#include "bgm.h"
#include "gang.h"
#include "onishima_draw.h"
#include "enemycombat.h"
#include "group_data.h"
#include "hud.h"
#include "pause_map.h"
#include "practice_hud.h"
#include "viewer_controls.h"

PSP_MODULE_INFO("JGRPSP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
/* Level is static; reserve this for nested calls, not persistent game data.
   Returning the old 256 KiB stack's unused space preserves the PSP heap. */
PSP_MAIN_THREAD_STACK_SIZE_KB(64);
/* Negative heap size selects all available memory; its magnitude is NOT
   the reserve in this SDK. Set the separate threshold explicitly. The
   default 512 KiB left 444 KiB unused after the audio threads started;
   returning half to the heap lets traffic and full voice banks coexist. */
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(256);

/* Compile away fixture I/O in shipping builds, retaining explicit test builds. */
#if JGR_TEST_FIXTURES
#define fixture_fopen(name,mode) fopen((name),(mode))
#define fixture_open(name,flags,permissions) sceIoOpen((name),(flags),(permissions))
#else
#define fixture_fopen(name,mode) ((FILE *)0)
#define fixture_open(name,flags,permissions) (-1)
#endif

static volatile int running = 1;
static volatile int power_sleeping,power_resumed;
static SceUID logfd=-1;
static int spike_enabled;
static unsigned spike_log_us,spike_log_calls,spike_log_bytes,spike_log_errors,spike_profile_us;
#include "runtime_log.inc"
#include "runtime_persistence.inc"
#include "runtime_profile.inc"
/* Release health sampling is deliberately independent of performance timers.
   Query allocator/kernel state only here on the healthy main thread. */
static void runtime_release_health(void)
{
    static int thread=-1;
    if(thread<0)thread=sceKernelGetThreadId();
    struct mallinfo mi=mallinfo();
    unsigned free=sceKernelTotalFreeMemSize(),largest=sceKernelMaxFreeMemSize();
    int stack_free=sceKernelGetThreadStackFreeSize(thread);
    release_report_memory(hang_progress.frame,(unsigned)mi.fordblks,free,largest,stack_free);
    crash_memory_sample(hang_progress.frame,hang_progress.stage,hang_progress.state,
                        (unsigned)mi.fordblks,free,largest);
}
static void save_presentation(int saved)
{
#if JGR_PORTABLE_ASSETS
    char directory[JGR_PORTABLE_PATH_MAX];
    if(portable_assets_resolve(directory,"jgr_story.sav",0))return;
    char *name=strrchr(directory,'/');if(!name)return;*name=0;
    /* Startup restores the application working directory before game entry. */
    if(savedata_update(directory,"EBOOT.PBP",saved))
        runtime_log("SAVE_METADATA could not update XMB presentation errno=%d\n",errno);
#else
    (void)saved;
#endif
}
static int save_campaign(const char *path)
{
    int result=story_save(path);
    runtime_log("STORY_SAVE result=%d operation=%s errno=%d path=%s\n",result,story_save_operation(),story_save_errno(),path);
    if(!result)save_presentation(1);
    runtime_log_flush();return result;
}
static int exit_callback(int a, int b, void *c) { audio_note_exit();running = 0; return 0; }
static int power_callback(int unknown, int pwrflags, void *common)
{
    /* Callbacks publish requests only: no mixer lock, job wait or module
       loading here. Preserve music on/off and pause state across sleep. */
    if (pwrflags & (PSP_POWER_CB_SUSPENDING | PSP_POWER_CB_STANDBY)) {
        hang_power(1);
        power_sleeping=1;audio_note_suspend();stream_suspend();
    }
    if (pwrflags & PSP_POWER_CB_RESUME_COMPLETE) {
        hang_power(0);
        stream_resume();audio_note_resume();power_resumed=1;power_sleeping=0;
    }
    return 0;
}
static int callback_thread(SceSize args, void *argp)
{
    int cbid = sceKernelCreateCallback("exit_cb", exit_callback, 0); int pwr = sceKernelCreateCallback("power_cb", power_callback, 0);
    sceKernelRegisterExitCallback(cbid); scePowerRegisterCallback(0, pwr); sceKernelSleepThreadCB(); return 0;
}

/* ---- matrices ---- */
static void m_identity(float *m) { for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.f : 0.f; }
static void m_mul(float *r, const float *a, const float *b)
{ float t[16]; for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { t[i*4+j] = 0; for (int k = 0; k < 4; k++) t[i*4+j] += a[i*4+k] * b[k*4+j]; } memcpy(r, t, sizeof t); }
static void m_ortho(float *m, float l, float r, float b, float t, float zn, float zf)
{ for (int i = 0; i < 16; i++) m[i] = 0; m[0] = 2.f / (r - l); m[5] = 2.f / (t - b); m[10] = -2.f / (zf - zn); m[3] = -(r + l) / (r - l); m[7] = -(t + b) / (t - b); m[11] = -(zf + zn) / (zf - zn); m[15] = 1.f; }
static void m_perspective(float *m, float fovy, float aspect, float zn, float zf)
{ float f = 1.0f / tanf(fovy * 0.5f); m_identity(m); m[0] = f / aspect; m[5] = f; m[10] = (zf + zn) / (zn - zf); m[11] = 2 * zf * zn / (zn - zf); m[14] = -1; m[15] = 0; }
static void m_lookat(float *m, const float *eye, const float *f, const float *upv)
{
    float s[3] = { f[1]*upv[2]-f[2]*upv[1], f[2]*upv[0]-f[0]*upv[2], f[0]*upv[1]-f[1]*upv[0] }; float sl = sqrtf(s[0]*s[0]+s[1]*s[1]+s[2]*s[2]); s[0]/=sl; s[1]/=sl; s[2]/=sl;
    float u[3] = { s[1]*f[2]-s[2]*f[1], s[2]*f[0]-s[0]*f[2], s[0]*f[1]-s[1]*f[0] };
    m_identity(m); m[0]=s[0]; m[1]=s[1]; m[2]=s[2]; m[3]=-(s[0]*eye[0]+s[1]*eye[1]+s[2]*eye[2]);
    m[4]=u[0]; m[5]=u[1]; m[6]=u[2]; m[7]=-(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]); m[8]=-f[0]; m[9]=-f[1]; m[10]=-f[2]; m[11]=(f[0]*eye[0]+f[1]*eye[1]+f[2]*eye[2]);
}
static void frustum_planes(const float *c, float p[6][4])
{
    for (int i = 0; i < 4; i++) { p[0][i]=c[12+i]+c[i]; p[1][i]=c[12+i]-c[i]; p[2][i]=c[12+i]+c[4+i]; p[3][i]=c[12+i]-c[4+i]; p[4][i]=c[12+i]+c[8+i]; p[5][i]=c[12+i]-c[8+i]; }
    for (int k = 0; k < 6; k++) { float l = sqrtf(p[k][0]*p[k][0]+p[k][1]*p[k][1]+p[k][2]*p[k][2]); for (int i = 0; i < 4; i++) p[k][i] /= l; }
}
static void dump_shot(const char *name)   /* displayed frame -> raw (512 stride, 8888, both buffers) */
{
    gfx_wait_idle();
    SceUID sf = sceIoOpen(name, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (sf >= 0) { sceIoWrite(sf, (void *)0x44000000, 512 * 272 * 4); sceIoWrite(sf, (void *)(0x44000000 + 0x88000), 512 * 272 * 4); sceIoClose(sf); }
}
static unsigned render_bench_pixels(void)
{
    void *address=NULL;int stride=0,format=0;
    if(sceDisplayGetFrameBuf(&address,&stride,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)<0 ||
       !address || stride<SCR_W || format!=PSP_DISPLAY_PIXEL_FORMAT_8888)return 0;
    const volatile unsigned *p=(const volatile unsigned *)((uintptr_t)address|0x40000000u);
    unsigned hash=2166136261u;
    for(int y=0;y<SCR_H;y++)for(int x=0;x<SCR_W;x++)hash=(hash^p[y*stride+x])*16777619u;
    return hash;
}
/* Capture the exact displayed surface that render_bench_pixels hashes. This
   runs after timing has ended, without borrowing gameplay heap storage. */
static unsigned char render_capture_scratch[JGR_CAPTURE_CHUNK_BYTES] __attribute__((aligned(64)));
static int render_capture_write(int fd,const void *data,unsigned bytes)
{
    hang_mark(HANG_CAPTURE_WRITE);
    int result=sceIoWrite(fd,data,bytes);
    if(result>0 && (unsigned)result<=bytes)hang_capture_written((unsigned)result);
    hang_mark(HANG_CAPTURE_COPY);return result;
}
static int render_bench_dump(const char *name)
{
    void *address=NULL;int stride=0,format=0;
    if(sceDisplayGetFrameBuf(&address,&stride,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)<0 ||
       !address || stride!=512 || format!=PSP_DISPLAY_PIXEL_FORMAT_8888)return -1;
    hang_capture_start(512u*272u*4u);
    unsigned previous=hang_mark(HANG_CAPTURE_OPEN);
    SceUID fd=sceIoOpen(name,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
    if(fd<0){hang_mark(previous);return fd;}
    const unsigned char *pixels=(const unsigned char *)((uintptr_t)address|0x40000000u);
    hang_mark(HANG_CAPTURE_COPY);
    int result=jgr_capture_write(fd,pixels,512u*272u*4u,render_capture_scratch,
                                 sizeof render_capture_scratch,render_capture_write);
    hang_mark(HANG_CAPTURE_CLOSE);
    int closed=sceIoClose(fd);hang_mark(previous);
    return result<0 ? result:closed<0 ? closed:result;
}
static Chars loading_bank;
static LoadingHud loading_hud;
static unsigned loading_started,loading_ticks;
static unsigned loading_last,loading_frames,loading_max_gap;
static const char *loading_phase="begin";
static int loading_active;
static void loading_present(void)
{
    if(!loading_active)return;
    unsigned now=sceKernelGetSystemTimeLow(),ticks=(now-loading_started)/33333;
    unsigned gap=now-loading_last;
    if(loading_frames && gap>100000u)runtime_log("PERF_LOAD_GAP phase=%s gap_ms=%.3f\n",loading_phase,gap*.001f);
    if(loading_frames && gap>loading_max_gap)loading_max_gap=gap;
    loading_last=now;loading_frames++;
    while(loading_ticks<ticks){loading_tick(&loading_hud);loading_ticks++;}
    gfx_begin();gfx_clear(GCOLOR(0x37,0x64,0x0a,255));gfx_mode3d();
    loading_draw(&loading_bank,&loading_hud,player_camera_focal(0x3c71,640,1));gfx_end();
}
void jgr_loading_poll(void)
{
    if(loading_active && sceKernelGetSystemTimeLow()-loading_last>=33333u)
        loading_present();
}
static void loading_begin(void)
{
    runtime_log_safe_checkpoint("load_begin");
    loading_active=!loading_load(&loading_bank);loading_hud=(LoadingHud){{0,0}};
    loading_started=sceKernelGetSystemTimeLow();loading_ticks=0;
    loading_last=loading_started;loading_frames=loading_max_gap=0;
    if(!loading_active){printf("LOADING original model bank unavailable\n");return;}
    loading_present();
    /* Explicit display fixture only. Normal loads are never delayed. */
    FILE *f=fixture_fopen("loadinghudtest","r");if(f){
        fclose(f);
        for(int t=1;t<=101;t++){
            loading_tick(&loading_hud);
            gfx_begin();gfx_clear(GCOLOR(0x37,0x64,0x0a,255));gfx_mode3d();
            loading_draw(&loading_bank,&loading_hud,player_camera_focal(0x3c71,640,1));gfx_end();
            if(t==10 || t==40 || t==80){char path[48];snprintf(path,sizeof path,"shot_loading_%d.raw",t);dump_shot(path);}
        }
        runtime_log("LOADING_HUD 101 source frames triangles=%d wrap=%u,%u\n",loading_bank.drawn_tris,loading_hud.frame[0],loading_hud.frame[1]);
        loading_started=sceKernelGetSystemTimeLow();loading_ticks=0;loading_hud=(LoadingHud){{0,0}};
    }
}
static void loading_end(void)
{
    loading_active=0;
    runtime_log("PERF_LOAD frames=%u duration_ms=%.3f max_gap_ms=%.3f\n",loading_frames,
        (sceKernelGetSystemTimeLow()-loading_started)*.001f,loading_max_gap*.001f);
    chars_free(&loading_bank);
    runtime_release_health();
    runtime_log_safe_checkpoint("load_end");
}
static void draw_player(Chars *chars,int model,const Player *p)
{
    CHAR_WORK_BEGIN(work);
    float root[16];player_root_matrix(p,root);
    chars_recovery_flash(player_recovery_flash(p));
    if (p->pose_valid && p->pose_bank==chars && p->pose_model==model) {
        if(p->spray_variant && p->spray_bank) {
            chars_draw_one_pose((Chars *)p->spray_bank,0,root,p->pose,p->overlay_motion,p->overlay_frame,p->overlay_blend);
        } else {
            chars_draw_one_pose(chars,model,root,p->pose,p->overlay_motion,p->overlay_frame,p->overlay_blend);
        }
    } else {
        chars_draw_one_xf_blend(chars,model,root,p->pose_motion,p->pose_frame,p->pose_motion2,p->pose_frame2,
                               p->pose_blend,p->overlay_motion,p->overlay_frame,p->overlay_blend);
    }
    chars_recovery_flash(0.f);
    CHAR_WORK_END(CHAR_WORK_PLAYER,work);
}
/* CPU queue construction precedes the actor draw pass. Keep it visible
   separately; the ME's asynchronous execution is not this timer. */
static unsigned npc_prepare_us,gang_prepare_us,pose_submit_us;
/* Shared frame selection for preparation and drawing. Only draw commits
   an authored motion change; preparation never advances enemy state. */
static float gang_render_frame(const Gang *g,const GangMember *m,int motion)
{
    if(g->kind==4)return gang_riot_frame(m);
    float frame=(g->mode==7 && m->secondary_anim>=14 && m->secondary_anim<=16) ? m->secondary_frame:m->frame;
    if(m->motion!=motion && !gang_source_member(g) && g->mode!=1 && g->mode!=2 && g->mode!=7 && g->mode!=8 && g->mode!=4 && g->mode!=9 && g->mode!=6 && g->mode!=5)frame=0.f;
    return frame;
}
static int gang_model(int kind);
static int gang_detail_model(int model)
{return model==16 || model==17 || model==48 || model==18 || model==34;} /* officers, dogs, motorcycle/rider */
static int gang_motion(const Gang *,const GangMember *,int);
/* 06dd7c / 0c9d50 draw gate: 0 when the original would not draw the member,
   otherwise the opacity its additive material alpha would carry. */
static float gang_view_alpha(const Gang *g,const float root[16],const float view[16],int *model_mode)
{
    GangViewGate gate=gang_view_gate(g);
    *model_mode=1;
    if(!(gate.far>0.f))return 1.f;
    float p[3];
    for(int k=0;k<3;k++)p[k]=root[k*4]*gate.centre[0]+root[k*4+1]*gate.centre[1]+root[k*4+2]*gate.centre[2]+root[k*4+3];
    float depth=-(view[8]*p[0]+view[9]*p[1]+view[10]*p[2]+view[11]);
    int mode=source_fade_class(depth,gate.near,gate.fade,gate.far);
    /* The bank's solo skaters are the running pair selected by0dfe2c.
       Their distinct waiting trees remain on the existing near path. */
    *model_mode=g->mode==1 && g->state<=2 ? 1:mode;
    return source_fade_opacity(mode,depth,gate.fade,gate.far);
}
static void prepare_gang_poses(Chars *chars,Gang *gangs,int count,const float view[16])
{
    for(int i=0;i<count;i++) {
        Gang *g=gangs+i;int ch=gang_model(g->kind);
        if(!g->active || (unsigned)ch>=chars->h->n_chars || (g->mode==2 && ch==39))continue;
        if(gang_mover_hidden(g) || (g->mode==6 && (g->flags&0x10120)))continue;
        if(g->mode==4 && (g->flags&(0x120|0x1000000)))continue;
        if(g->mode==5 && (g->flags&0x10120))continue;
        for(int j=0;j<g->nmem;j++) {
            GangMember *m=g->m+j;
            if((gang_source_member(g) || g->kind==23 || g->kind==25 || g->mode==7) && (m->flags&0x120))continue;
            if((g->kind==5 || g->kind==23) && m->anim==20)continue;
            float root[16];gang_member_matrix(g,m,root);
            int view_mode;
            if(!(gang_view_alpha(g,root,view,&view_mode)>0.f))continue; /* beyond the source gate */
            int draw_model=chars_source_lod_model(chars,ch,view_mode);
            const JgcChar *model=chars->chars+draw_model;
            int visible=chars_actor_visible(chars,draw_model,root);
            if(!visible || (g->mode==7 && (m->flags&0x200000)))continue;
            int motion=gang_motion(g,m,ch);float frame=gang_render_frame(g,m,motion);
            if((gang_source_member(g) && model->nbones<=20) || g->mode==8) {
                chars_pose_prefetch_channels(chars,draw_model,motion,frame);
                if(gang_source_member(g)) {
                    if(m->police.blend>0 && m->police.old_motion>=0 && (unsigned)m->police.old_motion<model->nmotions)
                        chars_pose_prefetch_channels(chars,draw_model,m->police.old_motion,m->police.old_frame);
                    else if((g->kind==0 || g->kind==4 || g->type==3 || g->type==7 || m->police.bomb || m->police.fire || m->police.kairiki) && (m->anim==1 || (g->kind==4 && m->speed!=0 && gang_riot_overlay(m)>=0)) && m->police.gait>0)chars_pose_prefetch_channels(chars,draw_model,gang_police_gait_motion(m),frame);
                    if(g->kind==4 && m->speed!=0 && gang_riot_overlay(m)>=0)chars_pose_prefetch_channels(chars,draw_model,gang_riot_overlay(m),gang_riot_overlay_frame(m));
                    if(g->kind==2 && m->anim==8 && m->police.head)chars_pose_prefetch_channels(chars,draw_model,1,gang_dog_turn_frame(m));
                }
            } else if(g->mode==7 && m->secondary_anim>=17 && m->secondary_anim<=19) {
                chars_pose_prefetch_channels(chars,draw_model,motion,frame);
                chars_pose_prefetch_channels(chars,draw_model,m->secondary_anim-8,m->secondary_frame);
            } else chars_pose_prefetch(chars,draw_model,motion,frame);
        }
    }
}
static void m_ident(float *m) { for (int i = 0; i < 16; i++) m[i] = (i % 5) == 0 ? 1.f : 0.f; }
static void gu_set(int mode, const float *m) { ScePspFMatrix4 g; float *d = (float *)&g; for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) d[c*4+r] = m[r*4+c]; sceGuSetMatrix(mode, &g); }

/* ---- level ---- */
typedef struct { int id; const char *name; } LevelInfo;
static const LevelInfo LEVELS[] = { {1, "SHIBUYA-CHO"}, {2, "KOGANE-CHO"}, {3, "BENTEN-CHO"}, {5, "BANTAM STREET"}, {6, "GRIND SQUARE"}, {4, "FINAL SHOWDOWN"} };
#define NLEVELS 6
/* mission table from E_MISSIONDATA (tools/jgs_missions.py): stage index 0..6 -> stage id; missions pick their mission set */
typedef MissionRec Mission;
static Mission missions[160]; static int nmis = 0; static unsigned char mis_head[160];   /* head = first record of a JMDc chain (menu entry) */
static const int STAGE_OF_INDEX[7] = { 1, 2, 3, 4, 5, 6, 0 };
static char mispath[64] = "missions.bin";
static int missions_load(const char *path)
{ int n = missions_read(path, missions, 160); snprintf(mispath, sizeof mispath, "%s", path); if (n < 0) return n; nmis = n;
  memset(mis_head, 1, sizeof mis_head); for (int i = 0; i < n; i++) if (missions[i].next >= 0 && missions[i].next < n) mis_head[missions[i].next] = 0; return 0; }
static int mission_next(int cur, int stage_id, int dir)
{ if (nmis == 0) return -1; int i = cur < 0 ? (dir > 0 ? -1 : nmis) : cur; for (int k = 0; k < nmis; k++) { i = (i + dir + nmis) % nmis; if (mis_head[i] && STAGE_OF_INDEX[missions[i].st < 7 ? missions[i].st : 6] == stage_id) return i; } return -1; }
typedef struct {
    unsigned handle, flags, graffiti;
    int record, state, active, unresolved_children;
} GroupParent;
/* Love Trap starts 13 direct encounters: Onishima, nine waiting bikes and
   three spraying rivals. Keep every spawn path tied to the actual pool size.
   Four additional PSP Gang slots cost 11,184 static bytes, not thread stack. */
#define LEVEL_GANG_CAPACITY 16
typedef struct {
    int id, loaded; Stage st; Collision col; Graffiti gf; Npcs npcs; Player pl; MissionState ms; float mtick; Police police; Graphs graphs; int graphs_ok; unsigned char *groups; int ngroups, groups_len; Gang gangs[LEVEL_GANG_CAPACITY]; int ngangs;
    EnemyCombatWorld combat;
    uint64_t enemy_preload_kinds;int enemy_preload_ready,enemy_audio_preloaded,traffic_preload;
    GroupParent group_parent[64]; unsigned group_serial; GangClock group_clock;
    GfoWorld object_graffiti; unsigned char *object_graffiti_data,*skater_graffiti_data[16];
    unsigned short *object_graffiti_texture; unsigned mission_set_bank;
    ScenePlay scene;
    unsigned scene_music_kind,scene_music_mode;
    FinalArena final;
    FinalTagArt final_art;
    CityTags city_tags;
    CitySky sky;
    KoganeHouse *house;
    KoganeCover cover;
    Chars water_splash;unsigned water_sound,water_getup_sound;
    ShibuyaGates *gates;CityOpenings *openings;
    StreetProps *street_props;
    PathCampaign *traffic;unsigned char *traffic_data,*traffic_grip_data;Chars traffic_models;int traffic_models_attempted;
    BentenTrain *trains;BentenCrane *crane;GrindProps *grind_props;
    CtrlSet ctl; CtrlPlay play; CtrlFrameClock replay_clock; const Mission *mrec; int saved_pchar; int set_idx; int recruit_demo_tick; RecruitAudio recruit_audio; const RecruitDemo *recruit_demo;   /* recorded-input playback (kind 6 demos) and the current chain record */
    Trial trial; int done_ticks, play_tick, campaign_error, story_retired,result_scene,recorded,record_place; unsigned ctl_bank; float camtick;
    RecruitResult result; int result_confirm,result_wide_owned;ResultCamera result_camera;
    FailureHud failure;float failure_tick,failure_fade;int failure_action;unsigned failure_edge;
    StageExit stage_exit;LevelExit exit;
    PauseHud pause;PauseWipe pause_wipe;float pause_tick;unsigned pause_edge,pause_resume_tick;
    Chars pause_map_models;PauseMap pause_map;int pause_map_wide_owned;
    PracticeHud practice_hud;
    float render_forward[3];
    HealthHud health_hud;TimerHud timer_hud;unsigned health_hud_tick,result_hud_tick;CanHudPlay can_hud;Chars can_hud_model,health_models;
    HudNotice notice;int hud_wide_owned,message_serial;
    unsigned char *sob; int sob_ntypes; const ColTri *sob_tri[32]; int sob_n[32]; unsigned char *sobj; int nsobj;
    Stage sobst; int sobst_ok; struct { int type; float x, y, z, yaw; } sobrec[96]; int nsobrec;   /* set-object models (stageN_obj.jgs) and the placed records */   /* set-object colliders (stageN.sob) and the set's object records (setN_obj.bin) */   /* set +0x48 trial script cursor (player +0x610/+0x614/+0x618) */
    struct Rival { Player pl; CharPose tag_pose[20]; PlayerFollow follow; CtrlPlay play; CtrlFrameClock clock; float cam_yaw, ex, ez; int table, seg, slot, chr, active, started, completion; float arrow_frame,arrow_acc; int tagged,spray_cans; unsigned source_event,graffiti; } rv[4]; int nrv;   /* event-0xc playback skaters */
    PlayerFollow follow;
    Chars tag_arrows;int tag_arrows_error,tag_arrows_drawn;
    int err, hferr, gferr; float cam_yaw, cam_pitch, cam_eye[3]; int score, hits, last_tags, tricks; float stun, airtime, grindtime;
    Zones zones; int zerr, cur_zone; int zone_list[256]; unsigned char zone_spawned[256];   /* sector streaming: pedestrian lists per zone slot (events 6/7), active within one neighbour hop of the player's zone */
    TitleRoute rival_route[3];
    Demo demo; TitleRoute title_route; TitleSample title_sample; int demo_on, demo_cam_script, demo_tick_n, demo_evi; int dcam_on, dcam_code; float dcam_pos[3], dcam_focal;   /* PLAYDEMO title/attract demo + its camera timeline */
} Level;
#include "spike_runtime.inc"
static int traffic_test; /* optional diagnostic logging/inspection */
static int water_test,water_test_case;static unsigned water_test_ticks;
static int world_audio_test,world_audio_test_ticks;
static int ambient_audio_test,ambient_audio_index=-1,ambient_audio_ticks;
static int campaign_entry_record=-1,campaign_entry_set,campaign_fixture;
static int campaign_writable;
static int campaign_clear_fixture;
static Chars chars; static int cherr = 1; static int pchar = 4;
static int ending_stage_load,title_stage_load,ending_stage_model;
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_mission_cast.inc"
#else
#include "generated/source_data/mission_cast.inc"
#endif
static int gameplay_characters_load(const Mission *m,int full)
{
    /* All nine STAFF streams request character 16 (the selected skater).
       Movie casts, the staff sky and its spray variant own separate banks.
       Keep global model indices, but do not pin the full 51-model bank while
       the credits need three 512-square card textures. */
    if(ending_stage_load)full=0;
    unsigned long long mask=ending_stage_load ? 0:full ? (1ULL<<51)-1:
        (m && m->set<sizeof MISSION_CAST/sizeof *MISSION_CAST ? MISSION_CAST[m->set]:0);
    if(!full && !ending_stage_load && (!m || m->set>=64 || !(MISSION_CAST_VALID&(1ULL<<m->set)))){cherr=-5;runtime_log("GAMEPLAY_CHARACTERS missing cast set=%u\n",m ? m->set:~0u);return cherr;}
    mask|=1ULL<<(ending_stage_load ? ending_stage_model:pchar);
    if(title_stage_load)mask|=1ULL<<TITLE_CAST_MODEL;
    if(chars.base && !cherr && chars.resident_mask==mask)return 0;
    unsigned before=mallinfo().fordblks;
    chars_free(&chars);cherr=1;
    if(full) {
        cherr=chars_load_sections(&chars,"chars_compact.jgc");
        if(!cherr)chars.resident_mask=mask;
    } else {
        cherr=chars_load_roster(&chars,"chars_roster.bin",mask);
    }
    if(!cherr)chars.motion_sel=player_anim(0);
    if(!cherr && !full)for(unsigned i=0;i<chars.h->n_chars;i++)
        if(gang_detail_model(i))chars_prepare_lod(&chars,(int)i);
    unsigned count=0;for(unsigned i=0;i<51;i++)count+=(mask>>i)&1;
    runtime_log("GAMEPLAY_CHARACTERS load=%d set=%u models=%u mask=%08x%08x bytes=%u heap_before=%u heap_after=%u split=%d\n",
        cherr,m ? m->set:~0u,count,(unsigned)(mask>>32),(unsigned)mask,chars.resident_bytes,
        before,(unsigned)mallinfo().fordblks,chars.vertex_data!=NULL);
    return cherr;
}

static Chars spraychar;static int spraychar_model=-1,spraychar_error=-1;
static void bind_spray_character(Player *p,int model,int final)
{
    if(spraychar_model!=model) {
        p->spray_bank=NULL;chars_free(&spraychar);spraychar_model=model;
        char path[128];snprintf(path,sizeof path,"spraymesh%02d.jgc",model);
        spraychar_error=chars_load(&spraychar,path);
        if(!spraychar_error)spraychar_error=chars_share_motions(&spraychar,&chars,model);
        if(spraychar_error) {
            chars_free(&spraychar);snprintf(path,sizeof path,"spraychar%02d.jgc",model);
            spraychar_error=chars_load(&spraychar,path);
        }
        if(!spraychar_error && (spraychar.h->n_chars!=1 || spraychar.chars[0].nbones!=20 || spraychar.chars[0].nmotions!=176))spraychar_error=-5;
        if(spraychar_error)chars_free(&spraychar);
        printf("SPRAY mesh model=%d status=%d\n",model,spraychar_error);
    }
    if(!spraychar_error && final && !spraychar.lighting) {
        char path[128];snprintf(path,sizeof path,"spraychar%02d.bin",model);
        int err=chars_load_lighting(&spraychar,path);
        printf("SPRAY mesh lighting model=%d status=%d\n",model,err);
    }
    p->spray_bank=spraychar_error ? NULL:&spraychar;
}
static Chars people; static int people_err = 1;   /* stage-specific people1/2/3/5dat models, textures, and source motion tables */
static Chars cordon; static int cordon_err = 1;
#include "heliwreck.h"
static Chars heliwreck; static int heliwreck_err = 1;
static Tex2dSet skatetrails;
#include "skatefx_draw.h"
static Chars skatefx; static int skatefx_err = 1;
static Chars paintfx;static int paintfx_err=1;
static Chars impactfx;
static void draw_skate_effects(const Player *p)
{
    if(!paintfx_err && p->effects.spray.active)
        chars_draw_effect(&paintfx,0,p->effects.spray.matrix,p->effects.spray.frame,1.f);
    if(impactfx.h && p->effects.impact.active)
        chars_draw_effect(&impactfx,0,p->effects.impact.matrix,p->effects.impact.frame,1.f);
    skate_ribbons_draw(&p->effects,p->pose_model,&skatetrails);
    if (skatefx_err) return;
    const SkateEffects *fx=&p->effects;
    if(skatefx.h->n_chars>2)for(int i=0;i<16;i++)if(fx->water_sparks[i].active)
        chars_draw_effect(&skatefx,2,fx->water_sparks[i].matrix,fx->water_sparks[i].frame,1.f);
    for (int side=0;side<2;side++) {
        if (fx->grind[side].active) chars_draw_effect(&skatefx,0,fx->grind[side].matrix,fx->grind[side].frame,1.f);
        if (fx->trick[side].active) chars_draw_effect(&skatefx,0,fx->trick[side].matrix,fx->trick[side].frame,1.f);
        if (fx->wall[side].active) chars_draw_effect(&skatefx,1,fx->wall[side].matrix,fx->wall[side].frame,1.f);
    }
    for(int i=0;i<24;i++)if(fx->dash_sparks[i].active)
        chars_draw_effect(&skatefx,0,fx->dash_sparks[i].matrix,fx->dash_sparks[i].frame,1.f);
    for (unsigned i=0;i<32;i++) if (fx->tails[i].active)
        chars_draw_effect(&skatefx,1,fx->tails[i].matrix,fx->tails[i].frame,fx->tails[i].alpha);
}
static Chars cans; static int cans_err = 1;   /* original +1/+5 spray-can models and motions from PLAYER_EFFECT */
static Chars sprayhud;static int sprayhud_error=1;
static Chars finishhud;
static Tex2dSet spraydigits;
#if JGR_PORTABLE_ASSETS
#include "source_data_spray_digits.inc"
#else
#include "generated/source_data/spray_digits.inc"
#endif
static void spray_hud_number(float x,float y,int value)
{
    if(spraydigits.n!=1)return;
    unsigned digits[10],n=0;
    do{digits[n++]=value%10;value/=10;}while(value && n<10);
    /* 04fa80 uses the zero glyph's integer width for centring/advance. */
    int advance=(int)SPRAY_DIGITS[0][0];x-=(int)(n*advance)/2;
    while(n) {
        const float *d=SPRAY_DIGITS[digits[--n]];
        tex2d_draw_hud(spraydigits.t,HUD_CENTRE,x,y,d[0],d[1],0,0,0xffffffff,d[2],d[3],d[4],d[5]);
        x+=advance;
    }
}
static void spray_hud_draw(const Graffiti *g)
{
    if(sprayhud_error || !g->mg_active || g->hud.deleted || !g->hud.count)return;
    /* 04165c: default camera with horizontal angle 038e; model Z=-1000.9.
       Mode-7 emitter 0ba260 adds the row position AFTER perspective divide,
       so translating the mesh sideways in world space is not equivalent. */
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_main_c__x_0.inc"
#else

#include "generated/source_data/main_c__x_0.inc"

#endif

    float projection[16],view[16],root[16];m_ident(view);
    float focal=player_camera_focal(0x38e,640.f,1.f);
    /* Original mode 7 rebiases reciprocal depth by 1000 after projection.
       Reserve a narrow independent PSP depth interval for these models;
       the world interval loses the arrow faces to 16-bit Z quantization. */
    m_perspective(projection,2.f*atanf(240.f/focal),640.f/480.f,900.f,1100.f);
    gu_set(GU_VIEW,view);sceGuClear(GU_DEPTH_BUFFER_BIT);chars_spray_hud_lighting(1);
    for(int j=0;j<g->hud.count;j++) {
        const SprayHudRow *r=g->hud.rows+j;unsigned cue=g->hud_cues[j];
        if(!cue || cue>10 || r->scale<=0)continue;
        projection[0]=focal/320.f;
        projection[2]=-(x[g->hud.count-1][j]-320.f)/320.f;
        hud_projection(projection,HUD_CENTRE);
        projection[6]=(360.f-282.f+r->y+r->z)/240.f;
        gu_set(GU_PROJECTION,projection);m_ident(root);
        root[0]=root[5]=root[10]=r->scale;root[11]=-1000.90002f;
        chars_draw_effect(&sprayhud,g->hud.mode*10+cue-1,root,r->frame,1.f);
    }
    chars_spray_hud_lighting(0);gfx_mode2d();
    if(!g->mg_wait || g->hud.mode)for(int j=0;j<g->hud.count;j++) {
        unsigned cue=g->hud_cues[j];int points=cue<=2 ? 100:cue<=6 ? 200:400;
        spray_hud_number(x[g->hud.count-1][j],360.f+g->hud.rows[j].z,points);
    }
}
static PoliceHud hudpol; static int hudpol_err = 1;
#include "countdown_hud.h"
static Chars countdown_models;

/* These banks are used only by live gameplay HUDs, never the title replay.
   Load once in a level transition, before its large collision allocations. */
static void gameplay_hud_load(void)
{
    static int loaded;
    if(loaded)return;
    if(chars_load(&finishhud,"finishhud.jgc"))chars_free(&finishhud);
    sprayhud_error=chars_load(&sprayhud,"sprayhud.jgc");
    if(sprayhud_error){chars_free(&sprayhud);}
    if(tex2d_load(&spraydigits,"spraydigits.bin")){
        tex2d_free(&spraydigits);
    }
    hudpol_err = police_hud_load(&hudpol,"policehud.jgc","policehud.bin");
    if(chars_load(&countdown_models,"countdown.jgc")){
        chars_free(&countdown_models);printf("HUD countdown models unavailable\n");
    }
    loaded=1;
    runtime_log("GAMEPLAY_HUD finish=%d spray=%d digits=%d police=%d countdown=%d\n",
        !!finishhud.h,sprayhud_error,spraydigits.n,hudpol_err,!!countdown_models.h);
}

static FailureModels failure_models;
static Chars pause_models;static float pause_volume=.5f; /* 0b23c4(8) */
/* chars.jgc model order = afs/PLAYER file order (bis, code, conpo, corn, ereki, goji, gum, nat, plug, pots, reco, rival_lshocker, rival_ntank,
   rival_pjam, rival_snpjam, yoyo); the game's character ids come from the name table 0x8c10df40 (0 BEAT(EREKI), 1 GUM, 2 CORN=Tab, 3 COMBO,
   4 CODE, 5 NUTS, 6 RECO, 7 BIS, 8 YO_YO, 9 PLUG=Cube, 13 GOJI, 14 POTS) - so the default skater is model 4 (ereki = Beat) */
static const char *SKATERS[16] = {"BIS","CODE","COMBO","TAB","BEAT","GOJI","GUM","NUTS","CUBE","POTS","RECO","LOVE SHOCKER","NOISE TANK","POISON JAM","POISON JAM 2","YO-YO"};
static const int CHAR_ID_OF_MODEL[16] = { 7, 4, 3, 2, 0, 13, 1, 5, 9, 14, 6, 11, 12, 10, 15, 8 };

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_main_c__char_health_0.inc"
#else

#include "generated/source_data/main_c__CHAR_HEALTH_0.inc"

#endif
   /* character record word 0 by id (table 0x8c182884) */

/* Group type -> source archive bank.  This is the creator dispatch table at
   0x8c1b4da4 followed through each creator's private loader; it is not the
   old guessed rival-character table.  Alternate model roots are packed at
   the end of chars.jgc (48 Kidoutai-B, 49 Bomb type 5, 50 Fire type 9). */
static int gang_model(int kind)
{
    switch (kind) {
    case 0: return 16;                                 /* Police: FUN_8c075f64 */
    case 1: return 17;                                 /* RiotPolice root 0: FUN_8c07926c */
    case 2: return 18;                                 /* Dog: FUN_8c0946d0 */
    case 3: return 30;                                 /* Killer: FUN_8c07ad7c */
    case 4: return 48;                                 /* RiotPolice root 2: FUN_8c0798f4 */
    case 5: return 49;                                 /* Bomb root 2: FUN_8c07b474 */
    case 6: return 31;                                 /* Kairiki: FUN_8c08cff8 */
    case 8: return 46;                                 /* Rocket type 8 detailed root 0x8ca0b170 */
    case 7: return 27;                                 /* para */
    case 9: return 50;                                 /* Fire root 2: FUN_8c07d854 */
    case 21: return 40;                                /* Taban: FUN_8c0f92b8 */
    case 26: return 16;                                /* formation creator: Keikan when pose == 0 */
    case 10: return 29;                                /* Tank: FUN_8c0a16f4 / root 0x8ca0549c */
    case 11: return 26;                                /* patcar: FUN_8c07f840 / table 0x8c1b620c */
    case 16: return 38;                                /* sniper: FUN_8c09bf24 / 0x8c1b60a0 */
    case 12: return 34;                                /* bike: FUN_8c083200 / table 0x8c1b62bc */
    case 27: return 17;                                /* formation creator: Kidoutai when pose == 0 */
    case 13: return 35;                                /* helicopter: FUN_8c0a5ab0 / 0x8c1b632c */
    case 18: return 42;                                /* Rival1: FUN_8c0df420 / 0x8c1b659c */
    case 19: return 43;                                /* Rival2: FUN_8c0df4a4 / 0x8c1b65a8 */
    case 14: return 36;                                /* Benz: FUN_8c0acb48 / table 0x8c1b6408 */
    case 25: return 33;                                /* Fire root 0: FUN_8c07d854 */
    case 15: case 32: return 37;                       /* attack helicopter: FUN_8c0a5bf4 / 0x8c1b647c */
    case 17: return 39;                                /* Onishima: FUN_8c08594c / 0x8c1b60ec */
    case 22: return 41;                                /* Denki: FUN_8c0fd120 / 0x8c1b665c */
    case 20: return 44;                                /* Rival3: FUN_8c0df528 / 0x8c1b65b4 */
    case 23: return 32;                                /* bomb */
    case 24: return 28;                                /* Rocket type 24 detailed root 0x8ca04ad0 */
    case 28: return 27;                                /* para (creator FUN_8c0af04c) */
    case 29: return 30;                                /* killer (creator FUN_8c0ae97c) */
    case 30: return 29;                                /* tank (creator FUN_8c0a1560) */
    default: return 22;                                /* rival_lshocker */
    }
}

static int gang_motion(const Gang *g, const GangMember *member, int model)
{
    if(gang_source_member(g) && member)return gang_police_motion(member);
    /* PLAY-format rivals share the game's 176-entry motion table: steady
       idle is animation 87 and logical skate is animation 70.  ENEMY banks
       have family-specific state tables; until each AI state is transcribed,
       keep their bank's own default clip instead of the old arbitrary 0/1
       switch (index 1 is a two-frame transition for several families). */
    static const signed char common_semantic[20] = {
        11,23,8,20,12,13,14,15,9,0,18,19,5,16,17,4,21,22,19,16
    };  /* handlers 0x8c1b77e0: FUN_8c076764..FUN_8c0773d4 */

#if JGR_PORTABLE_ASSETS
#include "source_data_main_enemy_maps.inc"
#else

#include "generated/source_data/main_enemy_maps.inc"

#endif

#if JGR_PORTABLE_ASSETS
#include "source_data_main_remaining_enemy_maps.inc"
#else
#include "generated/source_data/main_remaining_enemy_maps.inc"
#endif

    const signed char *table = 0; int table_count = 0;
    int anim = member ? member->anim : 8;
    int semantic = anim >= 0 && anim < 20 ? common_semantic[anim] : 9;
    if (anim == 8 && g->speed > 0.05f && g->kind!=0) semantic = 10;
    /* FUN_8c076764 changes to FUN_8c0768e8 as soon as the member has
       non-zero displacement.  This selects slot 23 while travelling. */
    if (anim == 0 && g->speed > 0.05f && g->kind!=0) semantic = 23;
    /* Kairiki's custom state-4 handler (FUN_8c08ccec) selects semantic 25
       or 26 before entering its states 20/21. */
    if (g->kind == 6 && (anim == 20 || anim == 21)) semantic = anim + 5;
    switch (g->kind) {
    case 0: table = keikan; table_count = 37; break;
    case 1: case 4: case 27: table = kidoutai; table_count = 36; break;
    case 2: table = inu; table_count = 14; break;
    case 3: case 29: table = killer; table_count = 33; break;
    case 5: case 23: table = bomb; table_count = 26; break;
    case 6: table = kairiki; table_count = 29; break;
    case 9: case 25: table = fire; table_count = 35; break;
    }
    if (table && semantic >= 0 && semantic < table_count) {
        if (table[semantic] >= 0) return table[semantic];
        /* FUN_8c076764 explicitly falls back from absent slot 11 to 9. */
        if (anim == 0 && semantic == 11 && table_count > 9 && table[9] >= 0)
            return table[9];
    }
    if (model <= 15 || (model >= 22 && model <= 25)) return player_anim(g->speed > 0.05f ? 6 : 0);
    if (model == 27 && (g->kind == 7 || g->kind == 28)) {
        /* Para creator FUN_8c07c228 uses the private semantic table at
           0x8c1b5e8c. States 20..24 select semantic slots 25..29;
           ordinary state 1 selects idle/skate by speed. */
        if (g->unit_state >= 20 && g->unit_state <= 24)
            return binding_para[g->unit_state+5];
        return binding_para[g->speed > 0.05f ? 23:9];
    }
    if (g->mode == 2 && model == 39) return member->anim; /* 088334 private controller */
    if (g->mode == 1 && g->kind >= 18 && g->kind <= 20) {
        /* FUN_8c0dec74 selects semantic clips 0 idle, 1 skate, 2 contact.
           Rival3 stores its last two raw headers in the opposite order. */
        int state = member->solo.old_anim >= 0 && member->solo.old_anim < 3 ? member->solo.old_anim : 0;
        return binding_rivals[g->kind - 18][state];
    }
    if (g->mode == 3 && g->kind == 11) {   /* state table 0x8c1b8c28; semantic table 0x8c1b620c */
        static const signed char state_semantic[17] = { 11,11,0,1,2,3,4,5,6,7,8,9,10,11,12,13,11 };
        int state = g->unit_state >= 0 && g->unit_state < 17 ? state_semantic[g->unit_state] : 11;
        return binding_patcar[state];
    }
    if (g->mode == 3 && g->kind == 12) {   /* state table 0x8c1b8f30; semantic table 0x8c1b62bc */
        static const signed char state_motion[17] = { 8,8,0,1,2,3,4,5,6,7,8,10,11,12,13,14,15 };
        return g->unit_state >= 0 && g->unit_state < 17 ? state_motion[g->unit_state] : 8;
    }
    if (g->mode == 3 && g->kind == 14) {   /* state table 0x8c1c9a50; semantic table 0x8c1b6408 */
        static const signed char state_semantic[17] = { 11,11,0,1,2,3,4,5,6,7,8,9,10,11,12,13,11 };
        int state = g->unit_state >= 0 && g->unit_state < 17 ? state_semantic[g->unit_state] : 11;
        return binding_benz[state];
    }
    if (g->mode == 4) { /* Four-object helicopter roots use their own table. */
        const signed char *motion=g->kind==13 ? binding_heri:binding_bheri;
        int state=g->unit_state>=1 && g->unit_state<=3 ? g->unit_state+1:0;
        return motion[state];
    }
    if (g->mode == 5) {   /* Sniper semantic table 0x8c1b60a0 -> raw local motions */
        return sniper_motion(g->action,g->unit_state,g->m[0].aim);
    }
    if (g->mode == 6) return member && member->anim >= 0 && member->anim < 5 ? member->anim : 0;   /* Denki table is local 0..4 */
    if (g->mode == 7) {   /* handler table 0x8c1c75b0 -> semantic table 0x8c1b5f54 */
        static const signed char state_motion[21] = { 0,1,1,3,5,12,12,13,14,15,16,18,20,-1,6,7,8,9,10,11,-1 };
        int state = member ? member->anim : g->unit_state;
        /* Secondary states 14..16 suppress the primary source dispatcher;
           later secondary states are true blends and keep the primary here. */
        if (member && member->secondary_anim >= 14 && member->secondary_anim <= 16)
            state = member->secondary_anim;
        if (state < 0 || state >= 21) state = 1;
        return state_motion[state] >= 0 ? state_motion[state] : 1;
    }
    if (g->mode == 8) {   /* FUN_8c0a09ec animation-state table 0x8c1c8e58; semantic table 0x8c1b6160 maps identically to raw clips */
        static const signed char state_motion[8] = { 18,7,5,11,15,16,17,0 };
        int state = member ? member->anim : g->unit_state;
        return state >= 0 && state < 8 ? state_motion[state] : 18;
    }
    if (g->mode == 9) return gang_taban_motion(g);
    return 0;
}

static const char *lvlpath(char *buf, int n, const char *name, int id, const char *ext)
{ snprintf(buf, n, "%s%d.%s", name, id, ext); return buf; }

/* highest surface under (x,z) below ytop (replaces the old heightfield query) */
static int ground_at(const Collision *c, float x, float z, float ytop, float *g)
{ ColHit h; if (!col_ground(c, x, ytop, z, 0.f, 3000.f, &h)) return -1; *g = h.p[1]; return 0; }

/* FUN_8c090ff0 rejects a set object against the view before submitting its
 * model.  The JGS bounds are in object-local space, so rotate their centre by
 * the placement's Ninja Y rotation and translate it into the world first. */
static int setobj_visible(const Stage *s, int oi, float x, float y, float z,
                          float yaw, const float planes[6][4])
{
    const JgsObj *o = &s->obj[oi];
    float c = cosf(yaw), sn = sinf(yaw);
    float wx = c * o->cx + sn * o->cz + x;
    float wy = o->cy + y;
    float wz = -sn * o->cx + c * o->cz + z;
    for (int i = 0; i < 6; i++)
        if (planes[i][0] * wx + planes[i][1] * wy +
            planes[i][2] * wz + planes[i][3] < -o->r) return 0;
    return 1;
}

/* Use the same placement, LOD and visibility gates as the prop draw below.
   Roof selection and bus preparation are measured with the pedestrian
   checks, so the benchmark includes the full cost of this feature. */
static void prepare_ped_proxies(Level *level,const float eye[3],
                               const float planes[6][4],const ObjectView *range,
                               int draw_props)
{
    if(npcs_occlusion_enabled()!=NPC_OCCLUSION_PROXIES)return;
    unsigned started=JGR_PROFILE_CLOCK();
    const Stage *city=level->err ? NULL:&level->st;
    stage_ped_proxy_prepare(city,eye);
    if(city && draw_props && level->sobst_ok)for(int i=0;i<level->nsobrec;i++) {
        int nt=(int)level->sobst.h->n_obj/2,model=level->sobrec[i].type;
        if(model<0 || model>=nt)continue;
        float x=level->sobrec[i].x,y=level->sobrec[i].y,z=level->sobrec[i].z,yaw=level->sobrec[i].yaw;
        if(!object_prop_visible(range,x,y,z))continue;
        float dx=x-eye[0],dy=y-eye[1],dz=z-eye[2];
        if(dx*dx+dy*dy+dz*dz>=90000.f)model+=nt;
        if(!setobj_visible(&level->sobst,model,x,y,z,yaw,planes))continue;
        stage_ped_proxy_add_object(city,&level->sobst,model,x,y,z,yaw);
    }
    npcs_occlusion_preparation_us(JGR_PROFILE_CLOCK()-started);
}

/* Spray-can task FUN_8c04dae8/FUN_8c04ddf4.  The effect bank supplies a
 * distinct animated near/far model for each can value. FUN_8c04dec0 switches
 * them at squared camera distance 4900 (70 units); its draw path also rejects
 * the local bounding sphere against the view. */
static void can_draw(const Can *cn, Chars *bank, Chars *health, const float eye[3],
                  const float planes[6][4],const ObjectView *objects)
{
    if (!cn->active || cn->taken || cn->scale <= 0.f) return;
    if(!object_pickup_visible(objects,cn->x,cn->y,cn->z))return;
    if(cn->kind>=2) {
        /* 0db6ec: health uses its full original mesh at every distance. */
        if(health->h){float xf[16];m_ident(xf);xf[3]=cn->x;xf[7]=cn->y;xf[11]=cn->z;
            chars_draw_one_xf(health,cn->kind-2,xf,0,cn->frame);}
        return;
    }
    float r = cn->kind ? 6.1f : 3.5f, cy = cn->y + (cn->kind ? 2.f : 1.1f);
    int vis = 1;
    for (int p = 0; p < 6 && vis; p++)
        if (planes[p][0] * cn->x + planes[p][1] * cy +
            planes[p][2] * cn->z + planes[p][3] < -r) vis = 0;
    if (!vis) return;
    float dx = cn->x - eye[0], dy = cn->y - eye[1], dz = cn->z - eye[2];
    int near = dx * dx + dy * dy + dz * dz <= 4900.f;
    int ci = cn->kind ? (near ? 3 : 2) : (near ? 1 : 0);
    float xf[16]; m_ident(xf); xf[0] = xf[5] = xf[10] = cn->scale;
    xf[3] = cn->x; xf[7] = cn->y; xf[11] = cn->z;
    chars_draw_one_xf(bank, ci, xf, 0, cn->frame);
}
static void cans_draw(const Graffiti *g, Chars *bank, Chars *health, const float eye[3],const float planes[6][4],const ObjectView *objects)
{
    for(int i=0;i<g->ncans;i++)can_draw(&g->cans[i],bank,health,eye,planes,objects);
    for(const CanDrop *d=g->can_drops;d;d=d->next) {
        if(d->flight.state==3) {
            can_draw(&d->can,bank,health,eye,planes,objects);
            continue;
        }
        int visible=1;const float *p=d->flight.pos;
        for(int j=0;j<6;j++)if(planes[j][0]*p[0]+planes[j][1]*p[1]+planes[j][2]*p[2]+planes[j][3]< -10.f)visible=0;
        if(!visible)continue;
        const float scale[3]={1,1,1};float matrix[16];
        player_scene_matrix(p,d->flight.angle,scale,matrix);
        /* 0df000: the near model, unanimated; 0df098 spins its XYZ root. */
        if(d->can.kind<2)chars_draw_one_rest(bank,d->can.kind?3:1,matrix);
        /* 0df000 uses slot 18 for BOTH health sizes while airborne. */
        else if(health->h)chars_draw_one_rest(health,1,matrix);
    }
}
static int player_audio_selection=-1;
static int world_audio_selection=-1;
static void level_player_spatial(void *context)
{
    Level *l=context;const SceneCamera *view=&l->follow.view;float points[2][3];
    if(!l->demo_on && l->play.active && l->title_route.image)view=&l->title_sample.camera;
    if(!view->active || !player_sound_points(&l->pl,points))return;
    int yaw=player_bam_atan2(view->forward[0],view->forward[2]);if(yaw<0)yaw+=65536;
    audio_player_position(points[0],points[1],view->pos,yaw,player_bam_atan2);
}
static void level_player_sound(void *context,int kind,int program)
{
    Level *level=context;
    /* Stop requests and direct SYS cues need no new bone sample. The
       per-tick callback still refreshes every surviving spatial owner. */
    if(kind!=PLAYER_SOUND_RESET && kind!=PLAYER_SOUND_SYSTEM && program>=0)level_player_spatial(context);
    int error=kind==PLAYER_SOUND_SYSTEM?audio_menu_sound(program):audio_player_sound(kind,program);
    /* Event-only diagnostics: loops update every animation tick, so do not
       flood the PSP filesystem with their steady-state requests. */
    if(kind==PLAYER_SOUND_VOICE || kind==PLAYER_SOUND_SYSTEM || (error && program>=0))
        runtime_log("PLAYER_AUDIO tick=%u motion=%d kind=%d program=%d error=%d seed=%u\n",
            level->pl.logic_tick,level->pl.logical_motion,kind,program,error,level->pl.rnd);
}
static void level_player_audio_bound(Level *level,int error,unsigned available)
{
    Player *pl=&level->pl;player_audio_selection=pl->character_id;
    runtime_log("PLAYER_AUDIO load character=%d error=%d heap_before=%u heap_after=%u\n",
                player_audio_selection,error,available,(unsigned)mallinfo().fordblks);
    /* SYS stroke/completion cues use the independently loaded menu bank.
       A PLAY bank error must not silently disconnect those events too. */
    pl->sound_event=level_player_sound;pl->sound_context=level;
    pl->sound_spatial=level_player_spatial;
}
static void level_bind_player_audio(Level *level)
{
    unsigned available=mallinfo().fordblks;
    int error=audio_load_player(level->pl.character_id);
    level_player_audio_bound(level,error,available);
}
static void level_collect_gameplay_audio(Level *level)
{
    GameplayAudioPreload info;
    if(!gameplay_audio_preload_finish(&info))return;
    if(!info.world_error && info.stage==level->id && info.stage!=4)world_audio_selection=info.stage;
    if(!info.player_error && info.character==level->pl.character_id)
        level_player_audio_bound(level,0,(unsigned)mallinfo().fordblks);
    if(!info.enemy_error && info.kinds==level->enemy_preload_kinds)level->enemy_audio_preloaded=1;
}
static void traffic_release(Level *L,int keep_models)
{
    L->pl.car_pull_query=NULL;L->pl.car_pull_context=NULL;
    memset(&L->pl.car_pull,0,sizeof L->pl.car_pull);
    L->pl.after_tick=NULL;L->pl.after_tick_context=NULL;
    if(L->traffic){pathmission_detach(L->traffic);free(L->traffic);L->traffic=NULL;}
    free(L->traffic_data);L->traffic_data=NULL;
    free(L->traffic_grip_data);L->traffic_grip_data=NULL;
    if(!keep_models){chars_free(&L->traffic_models);L->traffic_models_attempted=0;}
}
static void traffic_free(Level *L){traffic_release(L,0);}
static void level_unload(Level *L)
{
    gameplay_audio_preload_finish(NULL);
    result_camera_free(&L->result_camera);L->follow.result_tracks=NULL;
    failure_models_free(&failure_models);
    chars_free(&pause_models);
    audio_free_world();world_audio_selection=-1;
    audio_free_enemies();L->enemy_preload_ready=L->enemy_audio_preloaded=0;L->enemy_preload_kinds=0;L->traffic_preload=0;
    audio_free_player();player_audio_selection=-1;
    audio_free_hud_voice();
    audio_countdown_voice(-1);
    if (!L->loaded) return;
    if(!L->gferr)L->score+=graffiti_score_retire(&L->gf);
    demo_free(&L->demo);title_route_free(&L->title_route);
    for(int i=0;i<3;i++)title_route_free(&L->rival_route[i]);
    chars_free(&L->tag_arrows);
    sceneplay_free(&L->scene);
    if(L->result_wide_owned){font_real_free_wide();L->result_wide_owned=0;}
    if(L->hud_wide_owned){font_real_free_wide();L->hud_wide_owned=0;}
    audio_free_message();
    chars_free(&L->pause_map_models);
    practice_hud_free(&L->practice_hud);
    if(L->pause_map_wide_owned){font_real_free_wide();L->pause_map_wide_owned=0;}
    can_hud_play_free(&L->can_hud);chars_free(&L->can_hud_model);chars_free(&L->health_models);
    chars_free_lighting(&chars);
    L->pl.spray_bank=NULL;chars_free(&spraychar);spraychar_model=-1;spraychar_error=-1;
    chars_free(&heliwreck);heliwreck_err=1;enemycombat_free(&L->combat);
    finalarena_free(&L->final);
    audio_free_final_attacks();
    finaltags_art_free(&L->final_art);
    citytags_free(&L->city_tags);
    citysky_free(&L->sky);
    kogane_house_free(&L->house);
    kogane_cover_free(&L->cover);
    chars_free(&L->water_splash);
    audio_world_stop(L->water_sound);audio_world_stop(L->water_getup_sound);
    L->water_sound=L->water_getup_sound=0;
    L->pl.surface_contact=NULL;L->pl.surface_contact_context=NULL;
    shibuya_gates_free(&L->gates);city_openings_free(&L->openings);benten_train_free(&L->trains);benten_crane_free(&L->crane);grind_props_free(&L->grind_props);
    street_props_free(&L->street_props);
    traffic_free(L);enemycombat_free(&L->combat);
    /* A later set-object allocation failure can set hferr on an owned
       world. Always release its storage, including shared geometry/maps. */
    col_free(&L->col);
    if (!L->err) stage_free(&L->st); if (L->sobst_ok) stage_free(&L->sobst); if (!L->zerr) zones_free(&L->zones); L->zerr = 1; if (!L->gferr) graffiti_free(&L->gf); npcs_free(&L->npcs); if (!people_err) { chars_free(&people); people_err = 1; } mission_free(&L->ms); police_free(&L->police); graphs_free(&L->graphs); L->graphs_ok = 0; free(L->groups); L->groups = 0; L->ngangs = 0;
    free(L->object_graffiti_data);L->object_graffiti_data=NULL;
    for(int i=0;i<16;i++){free(L->skater_graffiti_data[i]);L->skater_graffiti_data[i]=NULL;}
    free(L->object_graffiti_texture);L->object_graffiti_texture=NULL;
    memset(&L->object_graffiti,0,sizeof L->object_graffiti);
    ctrl_free(&L->ctl); if (L->saved_pchar >= 0) pchar = L->saved_pchar; free(L->sob); free(L->sobj);
    memset(L, 0, sizeof *L); L->saved_pchar = -1;
}
#define CAM_LEASH 43.f
static const int MODEL_OF_CHAR[16] = { 4, 6, 3, 2, 1, 7, 10, 0, 15, 8, 13, 11, 12, 5, 9, 14 };   /* inverse of CHAR_ID_OF_MODEL */
/* start one JMDc chain record: mission image, player at the record start (+0x70 / +0x80); kind 6 (FUN_8c0184f0) = the set's
   first controller stream plays on its own character (mode 4, FUN_8c0273fc): the skater is teleported to the stream's spawn
   record (FUN_8c01aeb0: position, yaw, speed) and driven by the recorded input until the 0x80 end record */
static int rival_spawn(void *ctx,int slot,int table,unsigned event);
static void rival_delete(void *ctx,unsigned event);
static unsigned char *read_file(const char *path, int *len)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777); if (fd < 0) return 0;
    int n = sceIoLseek(fd, 0, PSP_SEEK_END); sceIoLseek(fd, 0, PSP_SEEK_SET); unsigned char *b = malloc(n + 4); if (!b) { sceIoClose(fd); return 0; }
    int done=0;
    while(done<n) {
        int chunk=n-done;if(chunk>65536)chunk=65536;
        int got=sceIoRead(fd,b+done,chunk);
        if(got<=0){sceIoClose(fd);free(b);return NULL;}
        done+=got;jgr_loading_poll();
    }
    sceIoClose(fd); *len = n; return b;
}
static void traffic_released(void *context,unsigned event,unsigned handle)
{Level *L=context;mission_path_released(&L->ms,event,handle);}
static int traffic_load(Level *L)
{
    if(L->traffic)return 0;
    if(L->hferr || L->id==4)return -1;
    char file[128];int length=0;
    snprintf(file,sizeof file,"traffic%d.bin",L->id);L->traffic_data=read_file(file,&length);
    L->traffic=calloc(1,sizeof *L->traffic);
    int error=!L->traffic || length<32 ? -1:pathworld_bind(&L->traffic->world,L->traffic_data,length,&L->col);
    if(!error && L->traffic->world.stage!=(unsigned)L->id)error=-2;
    if(!error) {
        length=0;snprintf(file,sizeof file,"traffic%d_grips.bin",L->id);
        L->traffic_grip_data=read_file(file,&length);
        error=pathworld_bind_grips(&L->traffic->world,L->traffic_grip_data,length);
    }
    if(error) {
        /* An optional load can fail before any vehicle exists. Retire its
           partial bank without disconnecting the level's simulation tail;
           a later authored spawn can retry through this same loader. */
        void (*after_tick)(void *)=L->pl.after_tick;
        void *after_context=L->pl.after_tick_context;
        traffic_free(L);
        L->pl.after_tick=after_tick;L->pl.after_tick_context=after_context;
    }
    else {
        L->traffic->world.defer_bodies=1;
        L->traffic->world.context=L;L->traffic->world.released=traffic_released;
        pathaudio_bind(&L->traffic->world);
        L->pl.car_pull_query=pathworld_grip_query;L->pl.car_pull_context=&L->traffic->world;
    }
    runtime_log("TRAFFIC stage=%d status=%d car-pull=%s\n",L->id,error,error ? "unavailable":"bound");
    return error;
}
/* Preparation follows this mission's opcode 9 references, including dormant
   events. The callback retains its original fallback if preparation failed. */
static unsigned traffic_spawn(void *context,unsigned event,const unsigned args[7])
{
    Level *L=context;if(traffic_load(L)){L->campaign_error=4;return 0;}
    return pathworld_spawn(&L->traffic->world,event,args);
}
static void traffic_control(void *context,unsigned handle,int opcode)
{Level *L=context;if(L->traffic)pathworld_control(&L->traffic->world,handle,opcode);}
static void traffic_reset(void *context)
{
    Level *L=context;
    if(L->traffic){pathworld_reset(&L->traffic->world);L->traffic->player_contacts.valid=0;}
    memset(&L->pl.car_pull,0,sizeof L->pl.car_pull);
}
static int traffic_prepare_models(Level *L,int optional)
{
    if(!L->traffic || (!optional && !L->traffic->world.count))return -1;
    if(L->traffic_models_attempted)return L->traffic_models.h ? 0:-1;
    L->traffic_models_attempted=1;char file[128];
    snprintf(file,sizeof file,"traffic%d.jgc",L->id);int error=chars_load(&L->traffic_models,file);
    if(error){chars_free(&L->traffic_models);}
    runtime_log("TRAFFIC models stage=%d error=%d\n",L->id,error);
    if(error) {
        if(optional){L->traffic_models_attempted=0;}
        else L->traffic->world.error=-8;
    }
    return error;
}
static void traffic_load_models(Level *L){(void)traffic_prepare_models(L,0);}
static void level_prepare_enemy_resources(Level *L)
{
    if(L->enemy_preload_ready)return;
    L->enemy_preload_ready=1;
    uint64_t kinds=L->enemy_preload_kinds;
    unsigned before=mallinfo().fordblks;int projectiles=0;
    for(int type=0;type<64;type++)if((kinds&(UINT64_C(1)<<type)) && enemycombat_type_uses_projectiles(type)) {
        projectiles=enemycombat_prepare_projectiles(&L->combat) ? 1:-1;break;
    }
    int sound=L->enemy_audio_preloaded ? 0:audio_preload_enemy_attacks(kinds);
    /* Keep room for the largest compressed radio line and small UI work.
       Hold a real reserve rather
       than assuming the current malloc arena is the entire PSP heap. Each
       optional loader keeps its original lazy retry on allocation failure. */
    void *reserve=malloc(512*1024);
    int traffic=0,helicopter=0,health=0,have_reserve=reserve!=NULL;
    unsigned graphics=0;
    if(reserve) {
        graphics=enemycombat_prepare_graphics(&L->combat,kinds);
        if(L->traffic_preload)traffic=traffic_load(L) || traffic_prepare_models(L,1) ? -1:1;
        if(kinds&((UINT64_C(1)<<13)|(UINT64_C(1)<<15)|(UINT64_C(1)<<32))) {
            if(heliwreck_err==1) {
                int error=heliwreck_load(&heliwreck,"heliwreck.jgc");
                if(!error)heliwreck_err=0;
            }
            helicopter=!heliwreck_err ? 1:-1;
        }
        if(kinds&((UINT64_C(1)<<18)|(UINT64_C(1)<<19)|(UINT64_C(1)<<20))) {
            if(!L->health_models.h)chars_load(&L->health_models,"healthpickups.jgc");
            health=L->health_models.h ? 1:-1;
        }
        free(reserve);
    }
    AudioEnemyPreloadInfo info;audio_enemy_preload_info(&info);
    runtime_log("ENEMY_PRELOAD kinds=%08x:%08x projectiles=%d model_error=%d audio=%d pins=%u pin_bytes=%u sequence_bytes=%u planned=%u missing=%u failures=%u last_error=%d heap_before=%u heap_after=%u\n",
        (unsigned)(kinds>>32),(unsigned)kinds,projectiles,L->combat.error,sound,
        info.pinned_samples,info.pinned_bytes,info.sequence_bytes,info.planned_cues,info.missing_cues,info.failures,info.last_error,
        before,(unsigned)mallinfo().fordblks);
    if(kinds || L->traffic_preload)runtime_log("SCENERY_PRELOAD graphics=%x wanted=%x traffic=%d helicopter=%d health=%d reserve=%d\n",graphics,enemycombat_graphics_for_kinds(kinds),traffic,helicopter,health,have_reserve);
}
/* Collect audio prepared during EVENT, then finish rendering residency
   after its temporary banks are released. Failed background loads retry
   here; optional enemy prewarming follows required HUD/player/world banks. */
static int level_prepare_gameplay_resources(Level *L)
{
    level_collect_gameplay_audio(L);
    if(!font_real_wide_ready()) {
        int error=font_real_load_wide("fontwide.bin");
        runtime_log("HUD gameplay atlas error=%d heap=%u\n",error,(unsigned)mallinfo().fordblks);
        if(error){L->campaign_error=10;return 0;}L->hud_wide_owned=1;
    }
    if(L->id!=4 && world_audio_selection!=L->id) {
        world_audio_selection=L->id;unsigned before=mallinfo().fordblks;
        int error=audio_load_world(L->id);
        runtime_log("WORLD_AUDIO load stage=%d error=%d heap_before=%u heap_after=%u\n",L->id,error,before,(unsigned)mallinfo().fordblks);
    }
    traffic_load_models(L);
    if(player_audio_selection!=L->pl.character_id)level_bind_player_audio(L);
    /* These are ordinary gameplay visuals, previously loaded during the
       first draw. Give them precedence over optional encounter residency. */
    if(paintfx_err==1) {
        paintfx_err=chars_load(&paintfx,"paintspray.jgc");
        runtime_log("PAINT_MIST load=%d\n",paintfx_err);
    }
    if(!L->tag_arrows.h && !L->tag_arrows_error) {
        L->tag_arrows_error=chars_load(&L->tag_arrows,"tagarrows.jgc");
        runtime_log("GRAFFITI_ARROWS load=%d models=%u\n",L->tag_arrows_error,L->tag_arrows.h ? L->tag_arrows.h->n_chars:0);
    }
    level_prepare_enemy_resources(L);
    return 1;
}
static void level_surface_contact(void *ctx,int triangle)
{Level *L=ctx;shibuya_gates_contact(L->gates,triangle);city_openings_surface(L->openings,triangle);}
static int level_final_tick(void *ctx)
{
    Level *L=ctx;const SceneCamera *view=&L->follow.view;
    /* Final Groove replaces traffic_tick, which normally publishes this
       listener. Use the preceding source camera just as the city does. */
    if(view->active){int yaw=player_bam_atan2(view->forward[0],view->forward[2]);if(yaw<0)yaw+=65536;
        audio_world_listener(view->pos,yaw,player_bam_atan2);}
    return finalarena_tick(&L->final);
}
static void level_bind_final_player(Level *L)
{
    L->pl.before_tick=level_final_tick;L->pl.before_tick_context=L;
    L->pl.after_tick=finalarena_after_tick;L->pl.after_tick_context=&L->final;
    L->pl.stage_fall_recovery=1;L->final.player=&L->pl;
}
static int traffic_tick(void *ctx)
{
    Level *L=ctx;float feet[3]={L->pl.x,L->pl.y,L->pl.z};
    L->pl.water_enabled=L->id==2 && !L->zerr;
    L->pl.water_area=L->zerr ? -1:zone_area(&L->zones,L->cur_zone);
    L->pl.water_sector=L->cur_zone;
    if(L->water_sound && (!L->pl.water.splash || L->pl.reset_query || audio_world_finished(L->water_sound))) {
        audio_world_stop(L->water_sound);L->water_sound=0;
    }
    if(L->water_getup_sound && (!L->pl.water.phase || L->pl.reset_query || audio_world_finished(L->water_getup_sound))) {
        audio_world_stop(L->water_getup_sound);L->water_getup_sound=0;
    }
    if(L->traffic && L->pl.reset_query)L->traffic->player_contacts.valid=0;
    audio_world_tick();
    street_props_tick(L->street_props,&L->pl,L->zerr ? NULL:&L->zones,L->cur_zone);
    kogane_house_tick(L->house,&L->pl,L->zerr ? NULL:&L->zones,L->cur_zone);
    int gates_error=shibuya_gates_tick(L->gates,&L->pl,L->zerr ? NULL:&L->zones,L->cur_zone);
    if(gates_error)return gates_error;
    int opening_error=city_openings_tick(L->openings,&L->pl,L->zerr ? NULL:&L->zones,L->cur_zone);
    if(opening_error)return opening_error;
    int crane_error=benten_crane_tick(L->crane,&L->pl,L->zerr ? NULL:&L->zones,L->cur_zone);
    if(crane_error)return crane_error;
    int grind_error=grind_props_tick(L->grind_props,&L->pl);
    if(grind_error)return grind_error;
    int train_error=benten_train_tick(L->trains,&L->pl,L->cur_zone);
    if(train_error)return train_error;
    const SceneCamera *view=&L->follow.view;
    if(view->active){int yaw=player_bam_atan2(view->forward[0],view->forward[2]);if(yaw<0)yaw+=65536;
        audio_world_listener(view->pos,yaw,player_bam_atan2);}
    if(!L->traffic)return 0;
    PathWorld *world=&L->traffic->world;
    int error=world->error ? world->error:pathworld_tick(world,feet,L->cam_eye);
    if(L->pl.car_pull.contact.handle && !pathworld_find(world,L->pl.car_pull.contact.handle))
        L->pl.car_pull.contact.handle=0;
    return error;
}
static void level_trial_event(void *,int);
static void traffic_after_tick(void *ctx)
{
    Level *L=ctx;
    /* Passage wraps relocate the player and contact history together.
       Keep exit samples in that same coordinate space, rather than
       sweeping an imaginary segment through the intervening city. */
    const float *translation=L->pl.portal_translation;
    if(translation[0] || translation[1] || translation[2]) {
        stage_exit_translate(&L->stage_exit,translation);
        for(int k=0;k<3;k++)L->ms.quad_previous[k]+=translation[k];
    }
    if(L->mrec && L->pl.water.phase==2) {
        /* Water recovery relocates to its authored safe point. That is
           not movement through all boundaries between the two places. */
        const float position[3]={L->pl.x,L->pl.y,L->pl.z};
        stage_exit_reset(&L->stage_exit,L->mrec->st,L->mrec->mid,position);
        memcpy(L->ms.quad_previous,position,sizeof position);
    }
    if(L->pl.water.splash_started)L->water_sound=audio_world_sound(36,L->pl.water.splash_position);
    if(L->pl.water.recover_started){float points[2][3];
        if(player_sound_points(&L->pl,points))L->water_getup_sound=audio_world_sound(40,points[0]);}
    if(water_test_case && water_test_ticks<530) {
        water_test_ticks++;WaterRecovery *w=&L->pl.water;
        runtime_log("WATER_TEST tick=%u case=%d xyz=%.9g,%.9g,%.9g phase=%d countdown=%d alpha=%.9g health=%d motion=%d frame=%.9g splash=%d area=%d sector=%d error=%d writable=%d\n",
            water_test_ticks,water_test_case,L->pl.x,L->pl.y,L->pl.z,w->phase,w->countdown,w->alpha,L->pl.health,
            L->pl.logical_motion,L->pl.frame,w->splash,L->pl.water_area,L->cur_zone,L->campaign_error,campaign_writable);
    }
    kogane_house_contacts(L->house,&L->pl);city_openings_contacts(L->openings,&L->pl,0);grind_props_contacts(L->grind_props,&L->pl);
    {unsigned phase=hang_mark(HANG_STREET_PROP_CONTACTS);
        street_props_contacts(L->street_props,&L->pl);hang_mark(phase);}
    int train_mode,train_yaw;
    if(benten_train_contacts(L->trains,&L->pl,&train_mode,&train_yaw) && player_receive_vehicle(&L->pl,train_mode,train_yaw) && !L->gferr){
        if(graffiti_interrupt(&L->gf)&GRAFFITI_TAG)level_trial_event(L,TRIAL_TAG);
        L->score+=L->gf.score;L->gf.score=0;L->last_tags=L->gf.tagged;
    }
    if(!L->traffic)return;
    PathPlayerContacts *s=&L->traffic->player_contacts;
    if(L->pl.damage_invincible || L->pl.reset_query){s->valid=0;return;}
    pathplayer_sample(s,&L->traffic->world,&L->pl);
    int response=s->contacts && player_receive_vehicle(&L->pl,s->mode,s->yaw);
    if(response && !L->gferr) {
        if(graffiti_interrupt(&L->gf)&GRAFFITI_TAG)level_trial_event(L,TRIAL_TAG);
        L->score+=L->gf.score;L->gf.score=0;L->last_tags=L->gf.tagged;
    }
    if(response || (traffic_test && s->samples && !(s->samples%60)))runtime_log(
        "TRAFFIC_PLAYER samples=%u contacts=%u limbs=%x handle=%u group=%u mode=%d yaw=%d speed=%f response=%d health=%d\n",
        s->samples,s->contacts,s->limbs,s->handle,s->group,s->mode,s->yaw,s->speed,response,L->pl.health);
}
static void level_npc_sound(void *ctx,Ped *p,int action)
{
    (void)ctx;
    if(p->sound && (action==NPC_SOUND_DELETE || audio_world_finished(p->sound))){audio_world_stop(p->sound);p->sound=0;}
    float position[3]={p->x,p->y,p->z};
    if(action==NPC_SOUND_REACTION && !p->sound){
        p->sound=audio_world_npc(p->type,position);
        if(world_audio_test)runtime_log("WORLD_NPC type=%d token=%u\n",p->type,p->sound);
    }
    else if(action==NPC_SOUND_AMBIENT && !p->sound){
        p->sound=audio_world_sound(p->ambient_reference,position);
        runtime_log("WORLD_AMBIENT type=%d route=%d reference=%d token=%u\n",p->type,p->route,p->ambient_reference,p->sound);
    }
    else if(action==NPC_SOUND_UPDATE && p->sound)audio_world_position(p->sound,position);
}
/* set-placed objects (JMSc +0x34 lists per zone -> FUN_8c090ff0 task 0x8a9 objects with the stage's per-type type-6 collider meshes):
   place the current chained set's records as world collision (rotation = Ninja Y rotation by the record's BAM yaw). Zone gating
   (spawn on zone entry, FUN_8c030af8) is not modelled: every record of the set is placed at once. */
static void setobj_place(Level *L, const Mission *m, int set_idx)
{
    if (!L->hferr) col_reset_extra(&L->col);
    if (!L->sob) { char p[64]; int len = 0; snprintf(p, sizeof p, "stage%d.sob", L->id); L->sob = read_file(p, &len);
        if (L->sob && len >= 8 && !memcmp(L->sob, "SOB1", 4)) { unsigned char *q = L->sob + 8; int nt = *(int *)(L->sob + 4); if (nt > 32) nt = 32; L->sob_ntypes = nt;
            for (int t = 0; t < nt && q + 4 <= L->sob + len; t++) { int n = *(int *)q; q += 4; L->sob_tri[t] = (const ColTri *)q; L->sob_n[t] = n; q += n * sizeof(ColTri); } }
        else { free(L->sob); L->sob = 0; } }
    if (L->sobj) { free(L->sobj); L->sobj = 0; }
    { char p[64]; int len = 0; snprintf(p, sizeof p, "set%u_obj.bin", m->set); L->sobj = read_file(p, &len);
      if (!L->sobj || len < 8 || memcmp(L->sobj, "SOB1", 4)) { free(L->sobj); L->sobj = 0; return; } }
    int nsets = *(int *)(L->sobj + 4); unsigned char *q = L->sobj + 8; L->nsobj = 0;
    for (int si = 0; si < nsets; si++) {
        int n = *(int *)q; q += 4;
        if (si != (set_idx < nsets ? set_idx : 0)) { q += n * 24; continue; }
        for (int i = 0; i < n; i++, q += 24) {
            unsigned ty = *(unsigned *)(q + 4); float x, y, z, yaw; memcpy(&x, q + 8, 4); memcpy(&y, q + 12, 4); memcpy(&z, q + 16, 4); memcpy(&yaw, q + 20, 4);
            if (L->hferr || !L->sob || (int)ty >= L->sob_ntypes || L->sob_n[ty] <= 0) continue;
            int err=set_collision_append(&L->col,L->sob_tri[ty],L->sob_n[ty],x,y,z,yaw);
            if(err){L->hferr=err;printf("SET collision append failed: %d\n",err);}
            else L->nsobj++;
        }
        /* keep the records for drawing (all types, even without a collider) */
        q -= n * 24; L->nsobrec = 0;
        for (int i = 0; i < n && L->nsobrec < 96; i++, q += 24) { unsigned ty = *(unsigned *)(q + 4); float x, y, z, yaw; memcpy(&x, q + 8, 4); memcpy(&y, q + 12, 4); memcpy(&z, q + 16, 4); memcpy(&yaw, q + 20, 4);
            L->sobrec[L->nsobrec].type = ty; L->sobrec[L->nsobrec].x = x; L->sobrec[L->nsobrec].y = y; L->sobrec[L->nsobrec].z = z; L->sobrec[L->nsobrec].yaw = yaw; L->nsobrec++; }
    }
    /* This operation is synchronous; no contact consumer runs between
       appends. Batch the grid work without changing placement order. */
    if(L->nsobj){int err=col_rebuild(&L->col);if(err){L->hferr=err;printf("SET collision grid failed: %d\n",err);}}
    runtime_log("SET_COLLISION bank=%u set=%d objects=%d triangles=%d error=%d grid_bytes=%u heap=%u\n",
        m->set,set_idx,L->nsobj,L->col.ntri,L->hferr,
        L->col.cellstart_cap*(unsigned)sizeof(int)+L->col.cellidx_bytes,(unsigned)mallinfo().fordblks);
}
static void zone_eval(Level *L)   /* 05a038 / 030934: only authored neighbour lists. */
{
    if (L->zerr) return;
    for (int z = 0; z < L->zones.nzones; z++) {
        int want = L->zone_list[z] >= 0 && zone_neighbour(&L->zones, L->cur_zone, z);
        if (want && !L->zone_spawned[z]) { npcs_set_list(&L->npcs, L->zone_list[z], 1); L->zone_spawned[z] = 1; }
        else if (!want && L->zone_spawned[z]) { npcs_set_list(&L->npcs, L->zone_list[z], 0); L->zone_spawned[z] = 0; }
    }
}
static void zone_assign(void *ctx, int list, int zone, int on)   /* FUN_8c05a038 / FUN_8c05a0e8 */
{
    Level *L = ctx; if (zone < 0 || zone >= 256) return;
    if (L->zerr) { npcs_set_list(&L->npcs, list, on); return; }
    if (on) { if (L->zone_spawned[zone] && L->zone_list[zone] != list) { npcs_set_list(&L->npcs, L->zone_list[zone], 0); L->zone_spawned[zone] = 0; } L->zone_list[zone] = list; }
    else { if (L->zone_spawned[zone]) { npcs_set_list(&L->npcs, L->zone_list[zone], 0); L->zone_spawned[zone] = 0; } L->zone_list[zone] = -1; }
    zone_eval(L);
}
/* police manager hooks (task 0x9fe): events 0x1f/0x20 load / clear the set's route record, 0x17/0x18 set the alert level */
/* Rival records (mission-set +0x18, tools/jgs_groups.py): events 2/3 dispatch
   through the source creator table 0x8c1b4da4.  Dedicated/route actors run
   in gang.c; the five anchor/member creators 26..30 use all GRP1 subrecords
   through npcs_load_groups (their full leader AI remains unfinished). */
static Level *g_level;
static unsigned grd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static float grdf(const unsigned char *p) { unsigned u = grd32(p); float f; memcpy(&f, &u, 4); return f; }
static int group_record_size(const Level *L,int rec)
{
    GroupRecord record;
    return L->groups_len<0 || group_record_read(L->groups,(unsigned)L->groups_len,rec,&record) ? -1:(int)record.count;
}
static int gangs_spawn_record(Level *L, int rec)
{
    int handled = 0;
    if (group_record_size(L,rec)<0) return 0;
    const unsigned char *q = L->groups + 8;
    for (int r = 0; r <= rec; r++) {
        int nsub = grd32(q); q += 4;
        for (int si = 0; si < nsub; si++) {
            int typ = grd32(q), trig = grd32(q + 4); float quad[8]; for (int k = 0; k < 8; k++) quad[k] = grdf(q + 8 + 4 * k); int nent = grd32(q + 40); const unsigned char *ent = q + 44; q = ent + 28 * nent;
            if (r != rec || nent < 1) continue;
            if(typ==31) {
                handled=1;if(L->ngangs>=LEVEL_GANG_CAPACITY)continue;
                gang_spawn_mines(&L->gangs[L->ngangs++],ent,nent);
                enemycombat_prepare_denki(&L->combat); /* shared explosion bank */
                printf("gang: record %d sub %d type 31 mines %d\n",rec,si,nent);
                continue;
            }
            if (typ == 11 || typ == 12 || typ == 14) {
                handled = 1; if (L->ngangs >= LEVEL_GANG_CAPACITY) continue;
                float wp[GANG_MAX_WAYPOINTS][3], speed[GANG_MAX_WAYPOINTS]; int action[GANG_MAX_WAYPOINTS]; int nwp = nent > GANG_MAX_WAYPOINTS ? GANG_MAX_WAYPOINTS : nent; for (int k = 0; k < nwp; k++) { wp[k][0] = grdf(ent + 28 * k); wp[k][1] = grdf(ent + 28 * k + 4); wp[k][2] = grdf(ent + 28 * k + 8); speed[k] = grdf(ent + 28 * k + 12); action[k] = (int)grd32(ent + 28 * k + 24); }
                gang_spawn_mover(&L->gangs[L->ngangs++], typ, wp, speed, action, nwp, trig, quad);
                printf("gang: record %d sub %d type %d mover %d waypoints from %.0f,%.0f,%.0f speed %.3f action %d\n", rec, si, typ, nwp, wp[0][0], wp[0][1], wp[0][2], speed[0], action[0]);
                continue;
            }
            if (typ == 8 || typ == 24) {
                handled = 1; if (L->ngangs >= LEVEL_GANG_CAPACITY) continue;
                float wp[GANG_MAX_WAYPOINTS][3], speed[GANG_MAX_WAYPOINTS], route_attr[GANG_MAX_WAYPOINTS]; int flags[GANG_MAX_WAYPOINTS]; int nwp = nent > GANG_MAX_WAYPOINTS ? GANG_MAX_WAYPOINTS : nent; for (int k = 0; k < nwp; k++) { wp[k][0] = grdf(ent + 28 * k); wp[k][1] = grdf(ent + 28 * k + 4); wp[k][2] = grdf(ent + 28 * k + 8); speed[k] = grdf(ent + 28 * k + 12); route_attr[k] = grdf(ent + 28 * k + 16); flags[k] = (int)grd32(ent + 28 * k + 24); }
                gang_spawn_rocket(&L->gangs[L->ngangs++], typ, wp, speed, route_attr, flags, nwp, trig, quad);
                printf("gang: record %d sub %d type %d Rocket x%d %d waypoints from %.0f,%.0f,%.0f speed %.3f attr %.3f flags %x\n", rec, si, typ, ((unsigned)flags[0] >> 16) & 0xffff, nwp, wp[0][0], wp[0][1], wp[0][2], speed[0], route_attr[0], flags[0]);
                continue;
            }
            if (typ == 10) {
                handled = 1; if (L->ngangs >= LEVEL_GANG_CAPACITY) continue;
                float wp[GANG_MAX_WAYPOINTS][3], speed[GANG_MAX_WAYPOINTS], route_attr[GANG_MAX_WAYPOINTS]; int flags[GANG_MAX_WAYPOINTS]; int nwp = nent > GANG_MAX_WAYPOINTS ? GANG_MAX_WAYPOINTS : nent; for (int k = 0; k < nwp; k++) { wp[k][0] = grdf(ent + 28 * k); wp[k][1] = grdf(ent + 28 * k + 4); wp[k][2] = grdf(ent + 28 * k + 8); speed[k] = grdf(ent + 28 * k + 12); route_attr[k] = grdf(ent + 28 * k + 16); flags[k] = (int)grd32(ent + 28 * k + 24); }
                gang_spawn_tank(&L->gangs[L->ngangs++], wp, speed, route_attr, flags, nwp, trig, quad);
                printf("gang: record %d sub %d type 10 Tank x%d %d waypoints from %.0f,%.0f,%.0f speed %.3f attr %.3f flags %x\n", rec, si, ((unsigned)flags[0] >> 16) & 0xffff, nwp, wp[0][0], wp[0][1], wp[0][2], speed[0], route_attr[0], flags[0]);
                continue;
            }
            if (typ == 13 || typ == 15 || typ == 32) {
                handled = 1; if (L->ngangs >= LEVEL_GANG_CAPACITY) continue;
                float wp[GANG_MAX_WAYPOINTS][3], speed[GANG_MAX_WAYPOINTS], route_attr[GANG_MAX_WAYPOINTS]; int flags[GANG_MAX_WAYPOINTS]; int nwp = nent > GANG_MAX_WAYPOINTS ? GANG_MAX_WAYPOINTS : nent; for (int k = 0; k < nwp; k++) { wp[k][0] = grdf(ent + 28 * k); wp[k][1] = grdf(ent + 28 * k + 4); wp[k][2] = grdf(ent + 28 * k + 8); speed[k] = grdf(ent + 28 * k + 12); route_attr[k] = grdf(ent + 28 * k + 16); flags[k] = (int)grd32(ent + 28 * k + 24); }
                gang_spawn_heli(&L->gangs[L->ngangs++], typ, wp, speed, route_attr, flags, nwp, trig, quad);
                /* PSP residency adapter: reserve this shared bank only when
                   an authored helicopter child exists, not during every
                   unrelated level intro. Release at mission/level changes. */
                if(heliwreck_err==1) {
                    heliwreck_err=heliwreck_load(&heliwreck,"heliwreck.jgc");
                    printf("heliwreck bank %s\n",heliwreck_err ? "MISSING":"loaded");
                }
                printf("gang: record %d sub %d type %d helicopter %d waypoints from %.0f,%.0f,%.0f speed %.3f attr %.3f flags %x\n", rec, si, typ, nwp, wp[0][0], wp[0][1], wp[0][2], speed[0], route_attr[0], flags[0]);
                continue;
            }
            if (typ == 16) {
                handled = 1; if (L->ngangs >= LEVEL_GANG_CAPACITY) continue;
                float pos[3] = { grdf(ent), grdf(ent + 4), grdf(ent + 8) }; float yaw = grdf(ent + 16); int rank = (int)grd32(ent + 24);
                gang_spawn_sniper(&L->gangs[L->ngangs++], pos, yaw, rank, trig, quad);
                L->gangs[L->ngangs-1].m[0].pitch=grdf(ent+12);
                L->gangs[L->ngangs-1].m[0].roll=grdf(ent+20);
                L->gangs[L->ngangs-1].m[0].sniper.angles[0]=(int)lrintf(grdf(ent+12)*(65536.f/6.28318530718f));
                L->gangs[L->ngangs-1].m[0].sniper.angles[2]=(int)lrintf(grdf(ent+20)*(65536.f/6.28318530718f));
                L->gangs[L->ngangs-1].dir[0]*=cosf(grdf(ent+12));
                L->gangs[L->ngangs-1].dir[1]=sinf(grdf(ent+12));
                L->gangs[L->ngangs-1].dir[2]*=cosf(grdf(ent+12));
                printf("gang: record %d sub %d type 16 Sniper rank %d at %.0f,%.0f,%.0f\n", rec, si, rank, pos[0], pos[1], pos[2]);
                continue;
            }
            if (typ == 22) {
                handled = 1; if (L->ngangs >= LEVEL_GANG_CAPACITY) continue;
                float pos[3] = { grdf(ent), grdf(ent + 4), grdf(ent + 8) }; float yaw = grdf(ent + 16);
                gang_spawn_denki(&L->gangs[L->ngangs++], pos, yaw, trig, quad);
                enemycombat_prepare_denki(&L->combat);
                printf("gang: record %d sub %d type 22 Denki at %.0f,%.0f,%.0f\n", rec, si, pos[0], pos[1], pos[2]);
                continue;
            }
            if (typ == 17) {
                handled = 1; if (L->ngangs >= LEVEL_GANG_CAPACITY) continue;
                float wp[GANG_MAX_WAYPOINTS][3]; int nwp = nent > GANG_MAX_WAYPOINTS ? GANG_MAX_WAYPOINTS : nent; for (int k = 0; k < nwp; k++) { wp[k][0] = grdf(ent + 28 * k); wp[k][1] = grdf(ent + 28 * k + 4); wp[k][2] = grdf(ent + 28 * k + 8); }
                float yaw = (int)grd32(ent + 24) * (6.2831853f / 65536.f);
                gang_spawn_patrol(&L->gangs[L->ngangs++], typ, wp, nwp, yaw, trig, quad);
                printf("gang: record %d sub %d type %d patrol %d waypoints from %.0f,%.0f,%.0f\n", rec, si, typ, nwp, wp[0][0], wp[0][1], wp[0][2]);
                continue;
            }
            if (typ == 21) {
                handled = 1; if (L->ngangs >= LEVEL_GANG_CAPACITY) continue;
                float pos[3] = { grdf(ent), grdf(ent + 4), grdf(ent + 8) };
                float yaw = (int)grd32(ent + 24) * (6.2831853f / 65536.f);
                const GGraph *graph = L->graphs_ok ? graphs_pick(&L->graphs, L->cur_zone >= 0 ? zone_area(&L->zones, L->cur_zone) : 0, &L->pl.x) : 0;
                gang_spawn_taban(&L->gangs[L->ngangs++], pos, yaw, graph, trig, quad);
                printf("gang: record %d sub %d type 21 Taban at %.0f,%.0f,%.0f heading %d graph %p\n", rec, si, pos[0], pos[1], pos[2], (int)grd32(ent + 24), (void *)graph);
                continue;
            }
            if (typ == 18 || typ == 19 || typ == 20) {
                handled = 1; if (L->ngangs >= LEVEL_GANG_CAPACITY) continue;
                float pos[3] = { grdf(ent), grdf(ent + 4), grdf(ent + 8) }; float yaw = grdf(ent + 16);
                /* 0df39c copies the authored angles directly. These are
                   ENEMY rivals, not PLAY actors with a travel-axis offset. */
                Gang *g=&L->gangs[L->ngangs++];
                gang_spawn_solo(g, typ, pos, yaw, nent, trig, quad);
                g->m[0].pitch=grdf(ent+12);g->m[0].roll=grdf(ent+20);
                g->nwp=nent-1;
                if(g->nwp>GANG_MAX_WAYPOINTS)g->nwp=GANG_MAX_WAYPOINTS;
                for(int j=0;j<g->nwp;j++) {
                    const unsigned char *drop=ent+(j+1)*28;
                    for(int k=0;k<3;k++)g->wp[j][k]=grdf(drop+k*4);
                    g->wp_action[j]=(int)grd32(drop+24);
                }
                printf("gang: record %d sub %d type %d solo at %.0f,%.0f,%.0f entries %d\n", rec, si, typ, pos[0], pos[1], pos[2], nent);
                continue;
            }
            if (typ == 0 || typ == 1 || typ == 2 || typ == 3 || typ == 4 || typ == 5 || typ == 6 || typ == 7 || typ == 9 || typ == 23 || typ == 25) {
                handled = 1; if (L->ngangs >= LEVEL_GANG_CAPACITY) continue;
                float pos[3] = { grdf(ent), grdf(ent + 4), grdf(ent + 8) }; float yaw = grdf(ent + 16); int cnt = (int)grd32(ent + 24); if (cnt < 1) cnt = 1; if (typ == 6) cnt = 1;
                const GGraph *g = L->graphs_ok ? graphs_pick(&L->graphs, L->cur_zone >= 0 ? zone_area(&L->zones, L->cur_zone) : 0, &L->pl.x) : 0;
                /* 070bc4 copies the owner angle; gas and Para creators
                   apply the half-turn to their formation offsets. */
                gang_spawn(&L->gangs[L->ngangs++], typ, typ, pos, (typ==3 || typ==7 || typ==4 || typ==5 || typ==23 || typ==6 || typ==9 || typ==25)?yaw:yaw+3.14159265f, cnt, g, trig, quad);
                if(typ==23)enemycombat_prepare_flames(&L->combat);
                if(typ==6 || typ==9 || typ==25)enemycombat_prepare_burning(&L->combat);
                printf("gang: record %d sub %d type %d x%d at %.0f,%.0f,%.0f graph %p\n", rec, si, typ, cnt, pos[0], pos[1], pos[2], (void *)g);
            }
        }
    }
    return handled;
}
static GroupParent *group_parent_find(Level *L,unsigned handle)
{
    for(unsigned i=0;i<64;i++)if(L->group_parent[i].active && L->group_parent[i].handle==handle)return &L->group_parent[i];
    return NULL;
}
static unsigned group_parent_word(void *ctx,unsigned handle,unsigned offset)
{
    Level *L=ctx;GroupParent *p=group_parent_find(L,handle);
    return p && offset==0x6e ? p->flags:gfo_word(&L->object_graffiti,handle,offset);
}
static unsigned group_parent_graffiti(void *ctx,unsigned handle,int descriptor)
{
    Level *L=ctx;GroupParent *p=group_parent_find(L,handle);if(!p || descriptor<0)return 0;
    p->graffiti=gfo_create(&L->object_graffiti,handle,L->mission_set_bank,L->set_idx,descriptor);
    printf("GROUP graffiti parent=%08x object=%08x bank=%u chain=%d descriptor=%d\n",handle,p->graffiti,L->mission_set_bank,L->set_idx,descriptor);
    return p->graffiti;
}
static unsigned group_parent_spawn(void *ctx,int record,unsigned event)
{
    Level *L=ctx;(void)event;
    if(group_record_size(L,record)<0)return 0;
    for(unsigned i=0;i<64;i++)if(!L->group_parent[i].active) {
        GroupParent *p=&L->group_parent[i];memset(p,0,sizeof *p);
        p->active=1;p->record=record;p->handle=0x10000000u|(++L->group_serial&0x0fffffffu);
        printf("GROUP create event=%08x parent=%08x record=%d\n",event,p->handle,record);
        return p->handle;
    }
    printf("GROUP parent capacity exceeded\n");return 0;
}
static void group_parent_delete(void *ctx,unsigned handle)
{
    Level *L=ctx;GroupParent *p=group_parent_find(L,handle);if(!p)return;
    for(int i=0;i<L->ngangs;i++)if(L->gangs[i].parent_handle==handle)L->gangs[i].active=0;
    for(int i=0;i<L->npcs.count;i++)if(L->npcs.p[i].group_owner==handle){L->npcs.p[i].active=0;L->npcs.p[i].group_owner=0;}
    npcs_group_delete(&L->npcs,handle);
    gfo_delete_parent(&L->object_graffiti,handle); /* 070428 -> 0c4a9c */
    p->active=0;
    printf("GROUP delete parent=%08x record=%d\n",handle,p->record);
}
static void group_parent_signal(void *ctx,unsigned handle,int leave)
{
    Level *L=ctx;GroupParent *p=group_parent_find(L,handle);if(!p)return;
    if(leave)p->state=2; /* 06f9f4, unlike forced activation 06fa1c */
    printf("GROUP %s parent=%08x record=%d\n",leave ? "depart":"force",handle,p->record);
    for(int i=0;i<L->ngangs;i++)if(L->gangs[i].active && L->gangs[i].parent_handle==handle)
        L->gangs[i].flags|=leave ? 0x10:0x1000;
    npcs_group_signal(&L->npcs,handle,leave);
}
static void group_parents_tick(Level *L)
{
    for(unsigned i=0;i<64;i++) {
        GroupParent *p=&L->group_parent[i];if(!p->active || p->state<0)continue;
        /* 070454 attaches on the update AFTER children are created, only
           to the first child in its append-ordered chain (0b375c). */
        if(p->state==1 && p->graffiti)for(int j=0;j<L->ngangs;j++) {
            Gang *g=L->gangs+j;if(!g->active || g->parent_handle!=p->handle)continue;
            if(!g->graffiti_handle) {
                GfoObject *o=gfo_find(&L->object_graffiti,p->graffiti);
                if(o){g->graffiti_handle=o->handle;g->graffiti=&o->progress;}
            }
            break;
        }
        if(!p->state) {
            /* Source 070454 creates children on its first update. PSP banks
               are already resident; asynchronous DC asset-loader tasks are
               not simulated. Reclaim dead slots before a later spawn. */
            int live=0;for(int j=0;j<L->ngangs;j++)if(L->gangs[j].active)L->gangs[live++]=L->gangs[j];
            L->ngangs=live;
            GroupRecord record;
            if(group_record_read(L->groups,(unsigned)L->groups_len,p->record,&record) || !record.supported) {
                p->state=-1;runtime_log("GROUP_ERROR record=%d data/unsupported\n",p->record);continue;
            }
            if(live>LEVEL_GANG_CAPACITY || record.direct>(unsigned)(LEVEL_GANG_CAPACITY-live)) {
                p->state=-1;
                runtime_log("GROUP_ERROR record=%d required=%u live=%d capacity=%d\n",
                    p->record,record.direct,live,LEVEL_GANG_CAPACITY);continue;
            }
            int conflict=0,available=0;
            for(int j=0;j<L->npcs.count;j++)if(L->npcs.p[j].list==1000+p->record) {
                available++;if(L->npcs.p[j].group_owner)conflict=1;
            }
            if(conflict || available!=(int)record.members) {
                p->state=-1;printf("GROUP record=%d member ownership/data error\n",p->record);continue;
            }
            gangs_spawn_record(L,p->record);int children=0;
            for(int j=live;j<L->ngangs;j++) {
                L->gangs[j].parent_handle=p->handle;L->gangs[j].parent_flags=&p->flags;children++;
                if(L->gangs[j].mode==1) {
                    const Gang *g=L->gangs+j;
                    runtime_log("GROUP_RIVAL record=%d slot=%d kind=%d state=%d animation=%d pos=%.6f,%.6f,%.6f yaw=%.6f\n",
                        p->record,j,g->kind,g->state,g->unit_state,g->pos[0],g->pos[1],g->pos[2],g->m[0].yaw);
                }
            }
            if(record.fallback) {
                p->unresolved_children=npcs_group_spawn(&L->npcs,p->record,p->handle,&p->flags);
                if(p->unresolved_children!=(int)record.fallback) {
                    p->state=-1;printf("GROUP record=%d squad ownership/data error\n",p->record);continue;
                }
            }
            /* Capacity/unsupported data is not a completed encounter. */
            p->state=children || p->unresolved_children ? 1:-1;
            if(p->state<0)printf("GROUP record=%d has no available child implementation\n",p->record);
            else printf("GROUP children parent=%08x record=%d count=%d\n",p->handle,p->record,children);
        } else {
            p->unresolved_children=npcs_group_active(&L->npcs,p->handle);
            int children=p->unresolved_children;
            for(int j=0;j<L->ngangs;j++)children+=L->gangs[j].active && L->gangs[j].parent_handle==p->handle;
            gang_parent_finish(&p->flags,p->state,children);
        }
    }
}
static void group_graffiti_pose(Level *L,Gang *g,int order)
{
    GfoObject *o=gfo_find(&L->object_graffiti,g->graffiti_handle);
    if(!o || !g->active || !g->nmem)return;
    for(int j=0;j<g->nmem;j++) {
        GangMember *member=g->m+j;
        unsigned policy=gang_graffiti_member_tick(g,member);
        if(policy&GANG_TAG_REGISTER) {
            float root[16],matrix[16];gang_member_matrix(g,member,root);
            for(int r=0;r<4;r++)for(int c=0;c<4;c++)matrix[c*4+r]=root[r*4+c];
            gfo_transform(o,matrix);o->registered=order*GANG_MAX_MEMBERS+j+1;
        }
        /* Shared GF, last eligible task supplies the selection volume;
           rendering below evaluates a separate pose for EVERY member. */
        o->visible|=(policy&GANG_TAG_DRAW)!=0;
    }
}
static void rival_list_cb(void *npcs, int list, int on) { if (on && list >= 1000 && list < 2000 && g_level && gangs_spawn_record(g_level, list - 1000)) return; npcs_set_list((Npcs *)npcs, list, on); }
static int gang_wall_probe(void *ctx, const float *a, const float *b) { Level *L = ctx; ColHit h; return !L->hferr && col_probe(&L->col, a, b, 0, &h); }
static int heli_collision_probe(void *ctx,const float *a,const float *b,unsigned mask,float *point)
{
    Level *L=ctx;ColHit h;
    if(!L || L->hferr || !col_probe(&L->col,a,b,mask,&h))return 0;
    memcpy(point,h.p,12);return 1;
}
static int rocket_landing_tag(void *context,const float *position,float *center)
{
    Level *L=context;if(!L || L->gferr)return 0;
    int result=0;
    for(int i=0;i<L->gf.n;i++) {
        const Spot *s=L->gf.spots+i;if(!s->active || !s->hasvol || s->attached)continue;
        int inside=1;
        for(int k=0;k<6 && inside;k++) {
            float x=s->pts[k*3]-position[0],y=s->pts[k*3+1]-position[1],z=s->pts[k*3+2]-position[2];
            double dot=(double)x*s->nrm[k*3];dot+=(double)y*s->nrm[k*3+1];dot+=(double)z*s->nrm[k*3+2];
            if((float)dot<0)inside=0;
        }
        if(inside){center[0]=s->x;center[1]=s->y;center[2]=s->z;result=s->revealing || s->complete?2:1;}
    }
    int last=0;
    for(unsigned i=0;i<GFO_CAPACITY;i++) {
        const GfoObject *o=L->object_graffiti.objects+i;
        if(o->handle && o->registered>last && gfo_contains(o,position)) {
            last=o->registered;memcpy(center,o->center,12);result=o->progress.reveal || o->progress.complete?2:1;
        }
    }
    return result;
}
static float gang_ground(void *ctx, float x, float y, float z) { Level *L = ctx; float g; if (!L->hferr && ground_at(&L->col, x, z, y + 30.f, &g) == 0) return g; return y; }
static int police_route_cb(void *ctx,int record)
{
    Level *L=ctx;
    /* 0dd2c8 creates 9fe only when the mission has a route-trigger list. */
    if(!mission_word(&L->ms,0x50))return 0;
    if(record!=-1 && (record<0 || record>=L->police.nrec || !L->police.rec[record].npts))return 0;
    police_route(&L->police,record);return 1;
}
static int police_status_cb(void *ctx)
{
    Level *L=ctx;
    return mission_word(&L->ms,0x50) ? L->police.flags:-1;
}
static void police_wave_cb(void *ctx,int list)
{
    Level *L=ctx;
    if(mission_word(&L->ms,0x50))npcs_wave(&L->npcs,list,L->police.seg);
}
static void train_event_cb(void *ctx,int code,unsigned route,unsigned frame,unsigned percent)
{
    Level *L=ctx;
    int error=bantam_train_event(L->trains,code,route,frame,percent);
    if(error){L->campaign_error=8;runtime_log("BANTAM_TRAIN event=%x error=%d\n",code,error);}
}
static void police_cb(void *ctx, int code, int a1, int a2, int a3, int a4)
{
    Level *L = ctx;
    if (code == 0x1f) police_route(&L->police, a1); else if (code == 0x20) police_route(&L->police, -1);
    else if (code == 0x17) police_alert_place(&L->police, (float)a1, (float)a2, (float)a3, 0);
    else if (code == 0x1d) police_alert_place(&L->police, (float)a1, (float)a2, (float)a3, a4);
    else if (code == 0x18 || code == 0x1e) police_alert_kill(&L->police);
}
/* FUN_8c0d84f0: while the pursuit is active every cordon segment draws its member effects (translate, rotate -z, y, -x, scale) with the
   effect-bank object 0x8c182fe8+0x94 (cordon.jgc) at the manager's frame */
static void police_draw_cordon(const Police *P, Chars *fx)
{
    if (!P->route || !(P->flags & 2)) return;
    const PolRoute *R = P->route;
    for (int i = 0; i < R->nsegs; i++) {
        const PolSeg *s = &R->sg[i];
        for (int j = 0; j < s->nmem; j++) {
            const PolMem *m = &R->mem[s->mem0 + j]; float xf[16], r[16];
            m_ident(xf); xf[3] = m->pos[0]; xf[7] = m->pos[1]; xf[11] = m->pos[2];
            float cz = cosf(-m->rot[2]), sz = sinf(-m->rot[2]); m_ident(r); r[0] = cz; r[1] = -sz; r[4] = sz; r[5] = cz; m_mul(xf, xf, r);
            float cy = cosf(m->rot[1]), sy = sinf(m->rot[1]); m_ident(r); r[0] = cy; r[2] = sy; r[8] = -sy; r[10] = cy; m_mul(xf, xf, r);
            float cx = cosf(-m->rot[0]), sx = sinf(-m->rot[0]); m_ident(r); r[5] = cx; r[6] = -sx; r[9] = sx; r[10] = cx; m_mul(xf, xf, r);
            m_ident(r); r[0] = m->scl[0]; r[5] = m->scl[1]; r[10] = m->scl[2]; m_mul(xf, xf, r);
            chars_draw_one_xf(fx, 0, xf, 0, (float)P->frame);
        }
    }
}
static int bgm_stage_idx = 0, bgm_slot = 0, bgm_phase_seen = 0;   /* game-mode task 6 +0x7c / +0x80: stage index and chapter slot of the current mission */
static void scene_command(void *context,const Scene *scene,const uint32_t command[5])
{
    Level *level=context;
    /* 02e204: EVENT 59 starts the mission music chain, except
       for kind 4. EVENT 5a -> 02e234 is an empty source function. */
    if(command[0]==0x59 && level->scene_music_kind!=4) {
        bgm_stage((int)level->scene_music_mode,bgm_stage_idx,bgm_slot);
        runtime_log("SCENE music clock=%.0f mode=%u song=%d\n",scene->clock,level->scene_music_mode,bgm_current());
    }
    if(command[0]==0x55 && command[1]==0)audio_message_voice(-1);
}
static int last_scene_begin;
static void level_retire_gameplay_resources(Level *level);
static int level_scene_begin(Level *level,int id,int preload)
{
    /* JMDc 1c prepares a later EVENT while play continues (S1M01: tag 1
       prepares 0101; tag 3 plays it). PSP shares game/render/audio RAM.
       Validate the source descriptor now; allocate the complete EVENT at
       opcode 0e, when its camera owns the frame. Never mute/remove gameplay
       owners during the intervening tags. Final-controller preloads use
       their separate sceneplay path and retain their original lifetime. */
    if(preload){unsigned bytes=0;int result=sceneplay_preflight(id,&bytes);
        runtime_log("SCENE_PREFLIGHT id=%d result=%d source_bytes=%u heap=%u\n",id,result,bytes,(unsigned)mallinfo().fordblks);
        return result;}
    /* Retain the source music binding even when the development arena
       releases its mission task to prevent story/save progression. */
    level->scene_music_kind=mission_word(&level->ms,0x14);
    level->scene_music_mode=mission_word(&level->ms,0x18);
    int mode=scene_dialogue_mode(id);
    unsigned before=mallinfo().fordblks;
    if(mode || scene_bespoke_supported(id) || scene_supported(id)){
        audio_free_hud_voice();level_retire_gameplay_resources(level);
        /* These render banks are not submitted while scene_frozen. Keep
           all path/collision/tag/player state; reload only immutable meshes
           after EVENT cleanup. chars_free fences their last GE submission. */
        chars_free(&level->traffic_models);level->traffic_models_attempted=0;
        level->pl.spray_bank=NULL;chars_free(&spraychar);spraychar_model=-1;spraychar_error=-1;
        if(level->hud_wide_owned){font_real_free_wide();level->hud_wide_owned=0;}
    }
    unsigned released=mallinfo().fordblks;
    audio_event_reset_load_info(id);
    /* The kind-6 controller temporarily borrows the gameplay actor for
       its demonstrator. EVENT's primary PLAY still belongs to the chosen
       player: S1M07's intro/retry must retain Cube beside Gum. */
    int primary_model=level->saved_pchar>=0 ? level->saved_pchar:pchar;
    int character=CHAR_ID_OF_MODEL[primary_model&15];
    int result=scene_bespoke_supported(id) ? sceneplay_event_begin(&level->scene,id,character,(int)mission_word(&level->ms,0x20),preload):
                    mode ? sceneplay_recruit_begin(&level->scene,id,character,
                    (int)mission_word(&level->ms,0x20),preload):
                    sceneplay_begin(&level->scene,id,character,preload);
    last_scene_begin=result;
    runtime_log("SCENE_LOAD id=%d result=%d heap_before=%u heap_released=%u heap_after=%u\n",id,result,before,released,(unsigned)mallinfo().fordblks);
    if(result>0)runtime_log("SCENE_CAST id=%d primary=%d secondary=%d primary_model=%d gameplay_character=%d\n",
        id,character,level->scene.vm.recruit_mode,primary_model,CHAR_ID_OF_MODEL[pchar&15]);
    if(scene_bespoke_supported(id)){EventAudioLoadInfo info=audio_event_load_info();runtime_log("SCENE_AUDIO_LOAD id=%d dependency=%u stage=%u error=%d largest=%u\n",info.id,info.file,info.stage,info.error,info.largest);}
    if(result>0){
        level->scene.vm.ctx=level;level->scene.vm.command=scene_command;
        if(!level->demo_on && !level->ms.phase && !level->result_scene)
            gameplay_audio_preload_begin(level->id,CHAR_ID_OF_MODEL[pchar&15],level->enemy_preload_kinds);
    }
    return result;
}
static int level_final_scene_begin(void *context,int id,int preload)
{
    Level *level=context;
    int result=sceneplay_final_begin(&level->scene,id,CHAR_ID_OF_MODEL[pchar&15],preload,&level->final.boss_rng);
    last_scene_begin=result;
    if(result>0){level->scene.vm.ctx=level;level->scene.vm.command=scene_command;}
    printf("FINAL EVENT id=%d preload=%d result=%d phase=%d\n",id,preload,result,level->final.phase);
    return result;
}
static int final_attack_sound(void *context,unsigned owner,int program)
{ (void)context;return audio_final_attack(owner,program); }
static void level_retire_gameplay_resources(Level *level)
{
    gameplay_audio_preload_finish(NULL);
    /* Result/failure phases already suspend these owners. Keep their
       storage out of the following presentation's allocation budget. */
    audio_free_player();player_audio_selection=-1;
    audio_free_world();world_audio_selection=-1;
    audio_free_enemies();level->enemy_preload_ready=level->enemy_audio_preloaded=0;
    enemycombat_release_idle_projectiles(&level->combat);
    enemycombat_release_idle_graphics(&level->combat,level->gangs,level->ngangs);
    int live_helicopter=0;
    for(int i=0;i<level->ngangs;i++) {
        const Gang *g=level->gangs+i;
        if(g->active && (g->kind==13 || g->kind==15 || g->kind==32))live_helicopter=1;
    }
    if(!live_helicopter){chars_free(&heliwreck);heliwreck_err=1;}
    level->pl.sound_event=NULL;level->pl.sound_spatial=NULL;
}
static int level_recruit_begin(Level *level,int id)
{
    /* The result tally and its fade have finished (0369dc). Its HUD/PLAY
       sounds no longer run while the EVENT owns the actors. PSP shares
       audio and game RAM: keeping these banks would duplicate the common
       and primary voices while loading the recruitment cast. Release the
       finished gameplay owners first, including the frozen city's samples.
       The next mission rebinds them after EVENT cleanup. */
    unsigned available_before=mallinfo().fordblks;
    audio_free_hud_voice();
    level_retire_gameplay_resources(level);
    level->scene_music_kind=mission_word(&level->ms,0x14);
    level->scene_music_mode=mission_word(&level->ms,0x18);
    unsigned available_released=mallinfo().fordblks;
    int result=sceneplay_recruit_begin(&level->scene,id,CHAR_ID_OF_MODEL[pchar&15],
                                     (int)mission_word(&level->ms,0x20),0);
    runtime_log("RECRUIT_LOAD id=%d result=%d heap_before=%u heap_released=%u heap_after=%u\n",
                id,result,available_before,available_released,(unsigned)mallinfo().fordblks);
    last_scene_begin=result;
    if(result>0){level->scene.vm.ctx=level;level->scene.vm.command=scene_command;}
    return result;
}
static int level_result_scene_id(const Level *L)
{
    if(!L->mrec || L->demo_on)return -1;
    StoryResultRoute route=story_result_route(L->mrec->mid,(int)mission_word(&L->ms,0x18),L->mrec->kind,0);
    return scene_recruit_supported(route.scene,(int)mission_word(&L->ms,0x20)) ? route.scene:-1;
}
static int level_result_scene_ready(Level *L)
{
    int id=level_result_scene_id(L);if(id<0)return 1;
    if(L->result_scene==2)return 1;
    if(L->result_scene==1) {
        if(L->scene.vm.error){L->campaign_error=8;return 0;}
        if(sceneplay_resident(&L->scene))return 0;
        L->result_scene=2;return 1;
    }
    if(sceneplay_resident(&L->scene))return 0;
    int result=level_recruit_begin(L,id);
    if(result<=0){L->campaign_error=8;runtime_log("CAMPAIGN recruitment load failed: EVENT %d error=%d\n",id,result);return 0;}
    L->result_scene=1;return 0;
}
static void level_final_progress(Level *level,float dt)
{
    FinalArena *a=&level->final;if(!a->loaded)return;
    a->progress_remainder+=dt;
    while(a->progress_remainder>=1.f/30.f) {
        a->progress_remainder-=1.f/30.f;
        a->scene_status=sceneplay_status(&level->scene);
        a->event_alive=sceneplay_resident(&level->scene);
        if(level->scene.vm.error || a->error){a->progression_error=1;level->campaign_error=8;}
        int old=a->phase;
        if(finalarena_progress(a)<0)printf("FINAL progression stopped: EVENT load failed\n");
        if(a->phase!=old)printf("FINAL phase %d -> %d status=%d event=%d clear=%d\n",old,a->phase,a->scene_status,a->event_alive,a->clear_requested);
        /* 0e51a4 -> 018690(1), only after the ending EVENT's cleanup.
           Inspection arenas without a mission never write a campaign result. */
        if(!level->campaign_error && level->id==4 && finalarena_clear_ready(a))
            mission_request_clear(&level->ms);
    }
}
static void level_trial_event(void *context,int action)
{
    Level *L=context;
    if(!L->mrec || L->mrec->kind==6 || !L->ms.active || L->ms.phase || !L->trial.count)return;
    Player *p=&L->pl;float feet[3]={p->x,p->y,p->z};
    trial_action(&L->trial,action,feet,p->speed,p->air_trick);
    L->ms.trial_result=L->trial.result;
}
static void level_result_totals(Level *L)
{
    /* 0360d8 snapshots the score/health/time/inventory at creation. Do not
       recalculate at departure after late trick awards or the HUD drain. */
    if(L->recorded)return;
    /* The PSP result snapshot can precede the score-task destructor.
       Retire its pending rows and live flights once before taking totals. */
    if(!L->gferr)L->score+=graffiti_score_retire(&L->gf);
    mission_results(&L->ms,L->score,L->pl.health,L->pl.health_max,L->gferr ? 0:L->gf.cancount);
    if(L->mrec && !L->recorded) {
        L->recorded=1;
        L->record_place=story_record_result(L->mrec->mid,CHAR_ID_OF_MODEL[pchar&15],L->mrec->st,
            L->mrec->kind,L->ms.res_rank,L->ms.res_total,campaign_writable && !L->demo_on);
    }
}
static void level_story_retire(Level *L)
{
    if(!L->mrec || !L->ms.active || L->campaign_error || L->demo_on)return;
    if(L->ms.phase==1)level_result_totals(L);
    /* Full rival-result replay must retire the earned clear in RAM so the
       following chapter/invitation matches gameplay. File saves remain
       disabled by campaign_writable throughout this isolated fixture. */
    if(story_phase_retire(&L->story_retired,campaign_writable || campaign_clear_fixture,L->ms.phase,
            L->mrec->mid,(int)mission_word(&L->ms,0x18),L->mrec->st,L->mrec->kind,L->ms.res_rank)) {
        StoryResultRoute route=story_result_route(L->mrec->mid,(int)mission_word(&L->ms,0x18),L->mrec->kind,0);
        printf("STORY retire name=%.15s record=%ld mode=%u branch=%d event=%d destination=%d\n",
               L->mrec->name,(long)(L->mrec-missions)-L->mrec->first,
               mission_word(&L->ms,0x18),route.branch,route.scene,route.mode);
        /* Recruitment EVENTs must complete before retirement. */
        if(route.scene>=0 && L->result_scene!=2)printf("STORY result choreography pending: EVENT %d\n",route.scene);
    }
}
static void mission_begin(Level *L, const Mission *m, int set_idx)
{
    gameplay_audio_preload_finish(NULL);
    spike_generation++;
    /* mission_start resets the outgoing GF owner, including its tally. */
    if(!L->gferr)L->score+=graffiti_score_retire(&L->gf);
    /* Demo -> player (including Tab's retry) uses the identical immutable
       vehicle mesh bank. Preserve that allocation while rebuilding ALL
       original path tasks and contacts. Releasing and reloading 288 KiB
       here failed allocation and stopped player ticks with world -8. */
    int keep_traffic_models=L->mrec && L->mrec->st==m->st && L->mrec->set==m->set &&
        L->mrec->kind==6 && m->kind==4 && L->set_idx==set_idx && L->traffic_models.h;
    result_camera_free(&L->result_camera);L->follow.result_tracks=NULL;
    /* Result/failure models are lazy presentation assets. Keeping them
       after a handoff consumes the space needed by the next PLAY bank. */
    failure_models_free(&failure_models);
    audio_free_player();player_audio_selection=-1;
    audio_free_world();world_audio_selection=-1;
    audio_free_enemies();L->enemy_preload_ready=L->enemy_audio_preloaded=0;L->enemy_preload_kinds=0;L->traffic_preload=0;
    audio_free_hud_voice();
    /* Isolated entry fixture, not a progression shortcut: one authored
       record/set start, no clear/trial/actor flags or saves. */
    if(!L->mrec && campaign_entry_record>=0) {
        const Mission *entry=mission_chain_select(missions,nmis,m,campaign_entry_record);
        campaign_entry_record=-1;
        if(!entry || entry->st!=m->st || entry->set!=m->set){L->campaign_error=7;return;}
        m=entry;set_idx=campaign_entry_set;
    }
    /* Retire every borrower before replacing the CTL2 allocation. The
       original loader selects both image chains before starting actors. */
    memset(&L->play,0,sizeof L->play);L->nrv=0;trial_begin(&L->trial,NULL,0);
    for(int i=0;i<3;i++)title_route_free(&L->rival_route[i]);
    L->campaign_error=0;
    if(L->hferr || !L->col.ntri) {
        L->campaign_error=11;
        runtime_log("CAMPAIGN collision unavailable name=%.15s error=%d triangles=%d\n",m->name,L->hferr,L->col.ntri);
        return;
    }
    if(!L->ctl.data || L->ctl_bank!=m->set) {
        char p[96];snprintf(p,sizeof p,"set%u_ctrl.bin",m->set);
        int err=ctrl_load(&L->ctl,p);
        if(err){L->campaign_error=1;runtime_log("CAMPAIGN unavailable controller bank=%u error=%d heap=%u\n",m->set,err,(unsigned)mallinfo().fordblks);return;}
        L->ctl_bank=m->set;
    }
    const CtrlSub *selected=ctrl_sub(&L->ctl,set_idx);
    if(!selected || (m->kind==6 && (!selected->ntab || !selected->nseg[0]))) {
        L->campaign_error=2;runtime_log("CAMPAIGN unavailable set/stream bank=%u set=%d kind=%u\n",m->set,set_idx,m->kind);return;
    }
    const GangRoute *gang_route=gang_route_find(m->name,(unsigned)set_idx);
    if(gang_route)for(int i=0;i<3;i++) {
        unsigned character=gang_route->character;
        char path[96];snprintf(path,sizeof path,"%s_rival%d.bin",gang_route->prefix,i);
        int err=title_route_load(&L->rival_route[i],path);
        if(err || !gang_route_validate(&L->rival_route[i],selected,i,character)) {
            L->campaign_error=12;runtime_log("GANG route unavailable name=%s member=%d error=%d\n",m->name,i,err);return;
        }
        runtime_log("GANG route name=%s member=%d frames=%u stride=%u\n",m->name,i,L->rival_route[i].count,L->rival_route[i].stride);
    }
    const ChallengeRival *challenge=m->kind==5 ? challenge_rival_find(m->name,(unsigned)set_idx):NULL;
    if(challenge) {
        char path[128];snprintf(path,sizeof path,"%s",challenge->path);
        int err=title_route_load(&L->rival_route[0],path);
        if(err || !challenge_rival_validate(&L->rival_route[0],selected,challenge->character)) {
            L->campaign_error=12;runtime_log("CHALLENGE rival unavailable name=%s error=%d\n",m->name,err);return;
        }
        runtime_log("CHALLENGE rival name=%s frames=%u character=%u\n",m->name,L->rival_route[0].count,challenge->character);
    }
    level_story_retire(L); /* 036ef8, before the outgoing JMDc is replaced */
    runtime_log("CAMPAIGN begin name=%.15s record=%ld set=%d bank=%u kind=%u\n",m->name,(long)(m-missions)-m->first,set_idx,m->set,m->kind);
    sceneplay_free(&L->scene);
    title_route_free(&L->title_route);L->recruit_demo_tick=0;L->recruit_audio=(RecruitAudio){0};L->recruit_demo=NULL;
    chars_free(&heliwreck);heliwreck_err=1;enemycombat_free(&L->combat);
    L->set_idx = set_idx;
    L->mission_set_bank=m->set;gfo_reset(&L->object_graffiti);
    kogane_house_reset(L->house);
    shibuya_gates_reset(L->gates);city_openings_reset(L->openings);benten_train_reset(L->trains);benten_crane_reset(L->crane);grind_props_reset(L->grind_props);
    street_props_reset(L->street_props);
    free(L->object_graffiti_texture);L->object_graffiti_texture=NULL;
    { char p[96];unsigned selected=story[0x3e5];if(selected<34) {
        snprintf(p,sizeof p,"grf/s%02u.bin",selected);L->object_graffiti_texture=gfo_texture_load(p);
    } }
    L->ngangs = 0; { char p[80]; snprintf(p, sizeof p, "set%u_graph.bin", m->set); L->graphs_ok = graphs_load(&L->graphs, p) == 0;
      int len = 0; free(L->groups); snprintf(p, sizeof p, "set%u_groups.bin", m->set); L->groups = read_file(p, &len);  L->groups_len=len;L->ngroups = L->groups && len >= 8 ? (int)grd32(L->groups + 4) : 0; }
    { char p[80]; snprintf(p, sizeof p, "set%u_police.bin", m->set); police_load(&L->police, p); police_reset(&L->police); L->ms.police = police_cb; }
    /* Actor placement pointers are identical across the supplied JMSc
       sub-sets (audited against +10/+18/+24/+28/+30). Actors themselves
       must be recreated: do not carry killed/knocked/active lists through
       a tutorial retry. Empty authored PED3 lists are valid. */
    loading_phase="mission_npcs";
    npcs_free(&L->npcs);
    { char p[96];snprintf(p,sizeof p,"set%u_peds.bin",m->set);
      if(npcs_load(&L->npcs,p,&L->col,0,L->id))L->campaign_error=3;
      snprintf(p,sizeof p,"set%u_groups.bin",m->set);
      if(npcs_load_groups(&L->npcs,p,&L->col))L->campaign_error=3;
      if(npcs_load_vehicle_models(&L->npcs))L->campaign_error=3;
      snprintf(p,sizeof p,"set%u_waves.bin",m->set);
      if(npcs_load_waves(&L->npcs,p,&L->col))L->campaign_error=3;
    }
    if(L->campaign_error){runtime_log("CAMPAIGN actor bank unavailable name=%.15s bank=%u\n",m->name,m->set);return;}
    npcs_prepare_detail(&L->npcs,&chars,people_err ? NULL:&people);
    L->ms.npcs = &L->npcs; L->ms.ped_list = rival_list_cb; g_level = L; gang_wall = gang_wall_probe; L->ms.wave = police_wave_cb; L->npcs.px = m->start[0]; L->npcs.pz = m->start[2];
    L->nrv = 0; L->ms.ctx = L; L->ms.rival_spawn = rival_spawn; L->ms.rival_delete=rival_delete;L->ms.ped_zone = zone_assign; for (int z = 0; z < 256; z++) { L->zone_list[z] = -1; L->zone_spawned[z] = 0; }
    L->ms.police_route=police_route_cb;L->ms.police_status=police_status_cb;
    memset(L->group_parent,0,sizeof L->group_parent);
    memset(&L->group_clock,0,sizeof L->group_clock);
    for(int i=0;i<L->npcs.count;i++)if(L->npcs.p[i].group_owner){L->npcs.p[i].active=0;L->npcs.p[i].group_owner=0;}
    L->ms.train=train_event_cb;L->ms.task_parents=3;L->ms.group_spawn=group_parent_spawn;L->ms.group_signal=group_parent_signal;
    L->ms.group_graffiti=group_parent_graffiti;
    L->ms.task_word=group_parent_word;L->ms.task_delete=group_parent_delete;
    traffic_release(L,keep_traffic_models);enemycombat_free(&L->combat);
    if(keep_traffic_models)runtime_log("TRAFFIC models reused stage=%d set=%d heap=%u\n",L->id,set_idx,(unsigned)mallinfo().fordblks);
    L->ms.path_ctx=L;L->ms.path_spawn=traffic_spawn;L->ms.path_control=traffic_control;
    L->ms.path_reset=traffic_reset;L->ms.task_parents|=4;
    if(L->final.loaded)level_bind_final_player(L);
    else {L->pl.after_tick=traffic_after_tick;L->pl.after_tick_context=L;}
    /* mission_start executes spawn events immediately. Those callbacks
       need the incoming kind to create the rival's tag/counter owner even
       on a fresh load, before any previous mission exists. */
    L->mrec=m;
    if(mission_start(&L->ms,m,mispath,L->gferr ? NULL:&L->gf)) {
        L->campaign_error=5;runtime_log("CAMPAIGN mission image unavailable name=%.15s\n",m->name);return;
    }
    L->enemy_preload_kinds=mission_enemy_preload_kinds(&L->ms,L->groups,L->groups_len>0 ? (size_t)L->groups_len:0);
    L->traffic_preload=mission_preload_traffic(&L->ms);
    if(L->city_tags.count){
        unsigned art=mission_word(&L->ms,0x24);int error=citytags_select_mask(&L->city_tags,art);
        runtime_log("CITY_TAGS rival_art=%u mask=%u error=%d\n",art,L->city_tags.mask_index,error);
        if(error){L->campaign_error=8;return;}
    }
    if(L->final.loaded) {
        int light=chars_load_lighting(&chars,"playerlight.bin");
        printf("FINAL playable lighting status=%d bytes=%u\n",light,light ? 0:20+chars.h->n_verts*20);
        if(finalarena_reset(&L->final))L->hferr=-1;
        if(audio_load_final_attacks()){L->final.error=-11;L->campaign_error=8;}
        if(L->gferr)L->final.progression_error=1;
        if(finaltags_art_load(&L->final_art,story[0x3e5],story[0x3e7])) {
            L->final.progression_error=1;printf("FINAL original graffiti art unavailable\n");
        }
    }
    L->cur_zone = L->zerr ? -1 : (int)mission_word(&L->ms, 0xd4); zone_eval(L);   /* start zone = JMDc +0xd4 (FUN_8c0422e0; +0x18 is the music mode) (FUN_8c0422e0 -> FUN_8c0c2c1c param 7); zone volumes only mark transitions */
    citysky_sector(&L->sky,L->cur_zone); /* 039af0 applies the starting sector before the intro. */
    stage_render_mission(&L->st,L->cur_zone,L->zerr ? 0:zone_area(&L->zones,L->cur_zone));
    loading_phase="mission_placement";
    setobj_place(L, m, set_idx);
    if(kogane_cover_select(&L->cover,mission_word(&L->ms,0xe0)))L->hferr=-2;
    if(L->hferr){L->campaign_error=11;runtime_log("CAMPAIGN set collision unavailable name=%.15s error=%d\n",m->name,L->hferr);return;}
    L->replay_clock=(CtrlFrameClock){0};L->mtick=0;
    L->mrec = m; L->story_retired=0;L->result_scene=0;L->recorded=0;L->record_place=5; memset(&L->play, 0, sizeof L->play);
    bgm_phase_seen = 0;
    float g; L->pl.x = m->start[0]; L->pl.z = m->start[2]; L->pl.y = m->start[1];
    if (!L->hferr && ground_at(&L->col, m->start[0], m->start[2], m->start[1] + 30.f, &g) == 0) L->pl.y = g;
    L->pl.onground = 1; L->pl.yaw = m->yaw * (6.2831853f / 65536.f) + 3.14159265f; L->pl.speed = 0; L->pl.vy = 0; L->pl.grinding = 0;
    L->pl.state = 1; L->pl.sub = 0; player_reset_controls(&L->pl);
    L->pl.sound_blocked=0;
    /* Mission creation also uses 01aeb0 (+90=1). EVENT 4 has no player
       handoff command: retain this until the first actual player update. */
    L->pl.reset_query=1;
    if (L->saved_pchar >= 0) { pchar = L->saved_pchar; L->saved_pchar = -1; }
    trial_begin(&L->trial,selected->trial,selected->ntrial);L->done_ticks=0;
    if(L->result_wide_owned){font_real_free_wide();L->result_wide_owned=0;}
    memset(&L->result,0,sizeof L->result);L->result_confirm=0;L->result_hud_tick=0;
    story_mission_souls_reset(); /* 018578 stage-parent initialization */
    L->pl.trial_event=level_trial_event;L->pl.trial_context=L;
    if (m->kind == 6) {
        const CtrlSub *cs = selected;
        if (cs->ntab > 0 && cs->nseg[0] > 0 && cs->segs[0][0].stream < cs->nstream) {
            const CtrlStream *s = &cs->streams[cs->segs[0][0].stream];
            ctrl_play_start(&L->play, s);
            L->pl.x = s->x; L->pl.y = s->y; L->pl.z = s->z; L->pl.yaw = s->yaw * (6.2831853f / 65536.f) + 3.14159265f; L->pl.speed = s->speed;
            if (!L->hferr && ground_at(&L->col, s->x, s->z, s->y + 30.f, &g) == 0 && g > s->y - 30.f) L->pl.y = g;
            L->saved_pchar = pchar;
            /* 018fb0: raw character 16 keeps the selected skater. */
            if(s->chr!=16)pchar=MODEL_OF_CHAR[s->chr&15];
            /* The authored demonstration uses the original completed
               actor/pose/camera callbacks. Re-running its inputs through
               PSP collision displaced the rivals before their tricks.
               Keep the controller clock and ordinary trial handoff. */
            const RecruitDemo *demo=recruit_demo_find(m->name,(unsigned)set_idx);
            if(demo) {
                char path[96];snprintf(path,sizeof path,"%s",demo->path);
                int err=title_route_load(&L->title_route,path);
                if(err || demo->character!=(unsigned)s->chr || L->title_route.character!=(unsigned)s->chr){
                    L->campaign_error=12;runtime_log("CHALLENGE route unavailable name=%s set=%d error=%d\n",m->name,set_idx,err);return;
                }
                L->recruit_demo=demo;
                /* Original captured cues own demo audio, including spray
                   loops. The live graffiti adapter still paints the tags;
                   its reconstructed sound requests must not double them. */
                L->pl.sound_blocked=1;
                runtime_log("CHALLENGE route name=%s set=%d frames=%u character=%u\n",m->name,set_idx,L->title_route.count,L->title_route.character);
            }
        }
    }
    /* 018fb0 creates a fresh actor for every mission entry, including
       0ca1dc retries: +8bc comes from the resolved character's record and
       +8b8 starts at that maximum. The port reuses Player, so this must
       also run for playable missions, after restoring/selecting pchar.
       Control/teleport resets deliberately preserve health. */
    player_set_character(&L->pl,CHAR_ID_OF_MODEL[pchar&15]);
    L->pl.health_max=L->pl.health=L->pl.tun->health_max;
    L->pl.stun=L->pl.damage_invincible=0;
    L->pl.fire_pending=L->pl.burn_ticks=L->pl.recovery_flash_ticks=0;
    if(!L->gferr) {
        char path[96];snprintf(path,sizeof path,"set%u_cans.bin",m->set);
        int err=graffiti_load_cans(&L->gf,path);
        int area=L->zerr ? 0:zone_area(&L->zones,L->cur_zone);
        if(err || graffiti_cans_select(&L->gf,set_idx,(unsigned)area<5 ? 1u<<area:0)) {
            L->campaign_error=9;runtime_log("CAMPAIGN missing can bank/set bank=%u set=%d error=%d\n",m->set,set_idx,err);return;
        }
        int chr=CHAR_ID_OF_MODEL[pchar&15];
        /* 018fb0: ordinary player starts empty. Recorded demonstration
           actors select their own creation flags; credits bind separately. */
        graffiti_cans_reset(&L->gf,PLAYER_TUNING[chr].can_capacity,L->play.active && L->play.s->chr==16);
        unsigned health_count=0;
        for(int i=0;i<L->gf.ncans;i++)health_count+=L->gf.cans[i].set==(unsigned)set_idx && L->gf.cans[i].kind>=2;
        if(health_count && !L->health_models.h && chars_load(&L->health_models,"healthpickups.jgc")) {
            L->campaign_error=9;runtime_log("CAMPAIGN health mesh unavailable\n");return;
        }
        if(!health_count)chars_free(&L->health_models);
        runtime_log("PICKUP_BANK set=%d paint_health_rows=%d health_rows=%u health_mesh=%d\n",set_idx,L->gf.ncans,health_count,L->health_models.h!=NULL);
    }
    health_hud_init(&L->health_hud,L->pl.health_max);L->health_hud_tick=L->pl.logic_tick;
    hud_notice_init(&L->notice);L->message_serial=0;
    audio_free_message();
    memset(&L->failure,0,sizeof L->failure);L->failure_tick=L->failure_fade=0;L->failure_action=0;L->failure_edge=0;
    memset(&L->exit,0,sizeof L->exit);
    {const float position[3]={L->pl.x,L->pl.y,L->pl.z};
        stage_exit_reset(&L->stage_exit,m->st,m->mid,position);}
    memset(&L->pause,0,sizeof L->pause);memset(&L->pause_wipe,0,sizeof L->pause_wipe);L->pause_tick=0;L->pause_edge=0;L->pause_resume_tick=L->pl.logic_tick;
    audio_pause_music(0);
    timer_hud_init(&L->timer_hud,m->kind,0);audio_countdown_voice(-1);
    loading_phase="mission_hud";
    if(L->timer_hud.countdown && audio_load_countdown())printf("HUD countdown voice bank unavailable\n");
    can_hud_play_free(&L->can_hud);can_hud_init(&L->can_hud.counter,0);
    if(!L->can_hud_model.h && chars_load(&L->can_hud_model,"canhud.jgc")) {
        L->campaign_error=10;runtime_log("CAMPAIGN original pickup HUD mesh unavailable\n");return;
    }
    L->cam_yaw = L->pl.yaw;   /* the DC camera starts exactly behind the skater (camyaw == yaw in trace16) */
    player_cam_reset(&L->pl); L->cam_eye[0] = L->pl.cam_ex; L->cam_eye[1] = L->pl.y + 18; L->cam_eye[2] = L->pl.cam_ez;
    /* A chained mission can begin inside the outgoing simulation tick.
       Retire that actor's completed view/query history before this frame
       draws: camera_reset is consumed only on the next player update.
       Otherwise the old outdoor view overrides cam_eye for one frame and
       re-creates Kogane's outdoor water after the indoor mission reset. */
    player_follow_bind(&L->follow,&L->pl,&L->col);
    /* 015988 / 0422f4: the authored scene at JMDc +d8 precedes control.
       Its final actor and camera poses replace the raw mission spawn. */
    if(!ending_stage_load && !title_stage_load){ int intro=(int)mission_word(&L->ms,0xd8);
      loading_phase="mission_intro";
      if(scene_supported(intro) || scene_dialogue_mode(intro) || scene_bespoke_supported(intro)) {
        int result=level_scene_begin(L,intro,0);
        if(result>0)L->ms.nsub=0;
        else {L->campaign_error=8;runtime_log("CAMPAIGN intro load failed EVENT=%d result=%d\n",intro,result);}
      } else if(mission_word(&L->ms,0x14)!=4)bgm_stage((int)mission_word(&L->ms,0x18),bgm_stage_idx,bgm_slot); }
    if(!ending_stage_load && !title_stage_load && !L->campaign_error && !sceneplay_resident(&L->scene))
        level_prepare_gameplay_resources(L);
}
/* end of a kind-6 stream (controller task killed at the 0x80 record, phase task FUN_8c018a0c state 3): next chain record or done */
/* event 0xc: a skater object driven by a mode-3 controller (FUN_8c069a7c -> FUN_8c027310/FUN_8c027224 -> FUN_8c0277d8 spawns task 0x8c4) */
static unsigned rival_graffiti_create(Level *L,int index,int character)
{
    if((unsigned)character>=16)return 0;
    unsigned owner=0x40000000u|(unsigned)(index+1);
    gfo_delete_parent(&L->object_graffiti,owner);
    if(!L->skater_graffiti_data[character]) {
        char path[96];int length=0;
        snprintf(path,sizeof path,"skatergraffiti%02d.bin",character);
        unsigned char *data=read_file(path,&length);
        if(!data || gfo_add_bank(&L->object_graffiti,data,length<0?0:(unsigned)length)){free(data);return 0;}
        L->skater_graffiti_data[character]=data;
    }
    return gfo_create(&L->object_graffiti,owner,63,7,character);
}
static int rival_spawn(void *ctx,int slot,int table,unsigned event)
{
    Level *L=ctx;if(L->demo_on)return 0;
    const CtrlSub *cs=ctrl_sub(&L->ctl,L->set_idx);
    if(!cs || slot<0 || slot>2 || table<0 || table>=cs->ntab || cs->nseg[table]<=0)return 0;
    int index=0;while(index<L->nrv && L->rv[index].active)index++;
    if(index==4)return 0;if(index==L->nrv)L->nrv++;
    /* A reused controller slot can change from a taggable rival to a
       tutorial/demo actor. Retire its old owner even when no new tag is made. */
    gfo_delete_parent(&L->object_graffiti,0x40000000u|(unsigned)(index+1));
    struct Rival *r=&L->rv[index];memset(r,0,sizeof *r);
    r->slot=slot;r->table=table;r->source_event=event;r->active=1;
    const CtrlSeg *sg = &cs->segs[table][0]; const CtrlStream *st = &cs->streams[sg->stream < cs->nstream ? sg->stream : 0];
    player_init(&r->pl, st->x, st->y, st->z);r->pl.world_stage=L->id;r->pl.trick_hud.blocked=1;
    const float position[3]={st->x,st->y,st->z};const int32_t angles[3]={(int32_t)st->w5,st->yaw,(int32_t)st->w7};
    rival_recorded_start(&r->pl,position,angles,st->speed);r->chr = st->chr & 15;
    r->spray_cans=PLAYER_TUNING[r->chr].can_capacity;
    r->pl.health_max = r->pl.health = CHAR_HEALTH[r->chr]; player_set_character(&r->pl, r->chr); player_cam_reset(&r->pl); r->cam_yaw = r->pl.cam_yaw;
    r->started = (sg->flags & 1) ? 0 : 1; ctrl_play_start(&r->play, st); if (!r->started) r->play.active = 0;   /* +0xb8 = 0 while a wait-flag segment waits for the player */
    if(L->mrec && graffiti_arrow_rival_enabled(L->mrec->kind,table)) {
        r->graffiti=rival_graffiti_create(L,index,r->chr);
        if(!r->graffiti){r->active=0;L->campaign_error=4;return 0;}
    }
    return index+1;
}
static void rival_delete(void *ctx,unsigned event)
{
    Level *L=ctx;
    for(int i=0;i<L->nrv;i++)if(L->rv[i].source_event==event){L->rv[i].active=0;L->rv[i].play.active=0;gfo_delete_parent(&L->object_graffiti,0x40000000u|(unsigned)(i+1));}
}
static void rival_spawn_stream(Level *L, const CtrlStream *st)   /* PLAYDEMO skater 1..: a mode-2 controller (one stream, no segment table) */
{
    if (L->nrv >= 4) return; struct Rival *r = &L->rv[L->nrv++]; memset(r, 0, sizeof *r); r->slot = -1; r->table = -1; r->active = 1; r->started = 1;
    player_init(&r->pl, st->x, st->y, st->z);r->pl.world_stage=L->id;r->pl.trick_hud.blocked=1;
    r->pl.yaw = st->yaw * (6.2831853f / 65536.f) + 3.14159265f; r->pl.speed = st->speed; r->chr = (st->chr & 16) ? 8 : (st->chr & 15);
    r->pl.health_max = r->pl.health = CHAR_HEALTH[r->chr]; player_set_character(&r->pl, r->chr); player_cam_reset(&r->pl); r->cam_yaw = r->pl.cam_yaw; ctrl_play_start(&r->play, st);
}
static void rival_segment(Level *L, struct Rival *r, int seg)   /* FUN_8c0278c0 mode 3 segment switch: +0x7c = new stream, teleport (FUN_8c01ae70) */
{
    const CtrlSub *cs=ctrl_sub(&L->ctl,L->set_idx);
    if (!cs || r->table<0 || r->table>=cs->ntab || seg<0 || seg>=cs->nseg[r->table]) { r->active = 0; return; }
    const CtrlSeg *sg = &cs->segs[r->table][seg]; const CtrlStream *st = &cs->streams[sg->stream < cs->nstream ? sg->stream : 0]; r->seg = seg;
    const float position[3]={st->x,st->y,st->z};const int32_t angles[3]={(int32_t)st->w5,st->yaw,(int32_t)st->w7};
    rival_recorded_start(&r->pl,position,angles,st->speed);
    r->started = (sg->flags & 1) ? 0 : 1; ctrl_play_start(&r->play, st); if (!r->started) r->play.active = 0;
    player_cam_reset(&r->pl); r->cam_yaw = r->pl.cam_yaw;
}
static void rival_tick(Level *L, struct Rival *r)   /* one 30 Hz controller tick (FUN_8c0278c0 mode 3) */
{
    const CtrlSub *cs=ctrl_sub(&L->ctl,L->set_idx); if (!r->active) return;
    if (r->table < 0) { if (r->play.active && !ctrl_play_tick(&r->play)) r->active = 0; return; }   /* mode 2: plain stream */
    if(!cs || r->table>=cs->ntab || r->seg<0 || r->seg>=cs->nseg[r->table]){r->active=0;return;}
    const CtrlSeg *sg = &cs->segs[r->table][r->seg]; const CtrlStream *st = &cs->streams[sg->stream < cs->nstream ? sg->stream : 0];
    /* 0278c0 waits for GF +14c; an individual press never pauses input. */
    GfoObject *tag=gfo_find(&L->object_graffiti,r->graffiti);
    if(tag && tag->progress.complete) {
        if(!r->tagged){r->tagged=1;r->completion=1;mission_rival_result(&L->ms,r->source_event,1);}
        return;
    }
    if (!r->started) {                                                /* 0279e0 reads TABLE +4 even on later segments, not segment +4. */
        float dx = L->pl.x - st->x, dz = L->pl.z - st->z;
        if (sqrtf(dx * dx + dz * dz) < cs->segs[r->table][0].dist) {
            const float position[3]={st->x,st->y,st->z};const int32_t angles[3]={(int32_t)st->w5,st->yaw,(int32_t)st->w7};
            rival_recorded_start(&r->pl,position,angles,st->speed);
            r->started=1;ctrl_play_start(&r->play,st);
            player_cam_reset(&r->pl);r->cam_yaw=r->pl.cam_yaw;
        }
        if(!r->started)return; /* source falls through to advance on activation */
    }
    if (r->play.active && !ctrl_play_tick(&r->play)) {               /* 0x80 end record */
        if (sg->flags & 4) { r->completion = 2; mission_rival_result(&L->ms,r->source_event,2);r->active = 0;
            gfo_delete_parent(&L->object_graffiti,0x40000000u|(unsigned)(r-L->rv+1)); } /* source +c4 -> event +0c */
        else rival_segment(L, r, (sg->flags & 2) ? 0 : r->seg + 1);   /* loop / next segment */
    }
}
static void mission_advance(Level *L)
{
    int nr = L->ms.next_rec, ns = L->ms.next_set; const Mission *m = L->mrec;
    const Mission *n=NULL;int result=mission_handoff(missions,nmis,m,nr,ns,L->ctl.nsub,&n);
    if(result>0) {
        /* Score the outgoing actor before the source character switch. */
        level_story_retire(L);
        /* 0369dc branch 6 changes task-6's playable skater to Combo
           between the two recruitment challenges; it is not demo identity. */
        StoryResultRoute route=story_result_route(m->mid,(int)mission_word(&L->ms,0x18),m->kind,0);
        if(L->ms.phase==1 && route.character>=0) {
            pchar=MODEL_OF_CHAR[route.character];L->saved_pchar=-1;
            player_set_character(&L->pl,route.character);
            L->pl.health=L->pl.health_max=CHAR_HEALTH[route.character];
        }
        mission_begin(L,n,ns);
    }
    else if(!result){L->ms.f30=1;L->play.active=0;}
    else {L->campaign_error=6;L->play.active=0;printf("CAMPAIGN invalid handoff name=%.15s record=%d set=%d\n",m?m->name:"?",nr,ns);}
}
static void title_apply(Level *L,unsigned frame,float fraction)
{
    if(!title_route_sample(&L->title_route,frame,fraction,&L->title_sample)){
        if(!L->campaign_error)runtime_log("TITLE route read failed row=%u\n",frame);
        L->campaign_error=12;return;
    }
    const TitleSample *s=&L->title_sample;Player *p=&L->pl;
    p->x=s->pos[0];p->y=s->pos[1];p->z=s->pos[2];
    p->yaw=(s->angle[1]+32768)*(6.283185307f/65536.f);
    p->state=s->state;p->visual_valid=1;memcpy(p->visual_angles,s->angle,sizeof s->angle);
    p->grinding=s->state==3;p->onground=s->state==1 || p->grinding;
    p->speed=s->effects.speed;p->logical_motion=s->effects.motion;
    p->frame=s->effects.frame;p->motion_flags=s->effects.flags;
    p->mirrored=s->effects.mirrored;p->grind_pose=s->effects.grind_pose;
    p->dash_held=s->effects.dash;p->boost=s->effects.boost;
    /* These are completed PLAY callbacks. The one-tick relocation guard
       has already retired; retaining it forever prevents can collection. */
    p->reset_query=0;
    memcpy(p->pose,s->pose,sizeof s->pose);p->pose_valid=1;p->pose_bank=&chars;p->pose_model=pchar;
    p->overlay_motion=-1;p->overlay_blend=0;p->logic_tick=s->tick;
}
static int recruit_demo_active(const Level *L)
{return !L->demo_on && L->play.active && L->title_route.image;}
static void recruit_demo_apply(Level *L,unsigned tick,float fraction)
{
    /* Capture begins at completed source player clock 2. The first idle
       sample also holds the initial tick; clamp the terminal idle pose,
       never wrap into the beginning before the controller retires. */
    unsigned frame=tick ? tick-1:0;
    if(frame>=L->title_route.count){frame=L->title_route.count-1;fraction=0;}
    title_apply(L,frame,fraction);
    if(!L->campaign_error)recruit_spray_apply(&L->pl,L->recruit_demo,L->title_sample.tick);
}
static void demo_start(Level *L)
{
    /* 015dd4 starts PLAYDEMO and music entry 0. The fixed presentation
       uses completed original poses/cameras, so incomplete gameplay
       collision cannot change the authored jumps, grinds or landings. */
    int err=title_route_load(&L->title_route,"title_route.bin");
    if(err){L->campaign_error=12;runtime_log("TITLE route unavailable error=%d\n",err);return;}
    L->saved_pchar=pchar;pchar=MODEL_OF_CHAR[L->title_route.character];
    player_set_character(&L->pl,L->title_route.character);
    L->pl.sound_blocked=1;L->pl.trick_hud.blocked=1;
    L->play.active=0;L->nrv=0;L->demo_on=1;L->demo_cam_script=0;
    L->demo_tick_n=0;L->dcam_on=1;L->replay_clock=(CtrlFrameClock){0};L->mtick=0;
    title_apply(L,0,0);bgm_play(0);audio_set_music(1);
    int voice=audio_load_movie_voice(183);if(!voice)audio_start_movie_voice(); /* 0176d4 -> 02d568(b7) */
    runtime_log("TITLE ready character=%u samples=%u stride=%u song=%d voice=%d mission_scene=%d\n",
        L->title_route.character,L->title_route.count,L->title_route.stride,bgm_current(),voice,sceneplay_resident(&L->scene));
}
static void level_load(Level *L, int id, const Mission *m)
{
    spike_generation++;
    char a[96], b[96];
    loading_begin();
    loading_phase="collision";
    level_unload(L); memset(L, 0, sizeof *L); L->saved_pchar = -1; L->id = id; L->loaded = 1;
    hang_progress.stage=(unsigned)id;
    if(!title_stage_load && !ending_stage_load)gameplay_hud_load();
    if(gameplay_characters_load(m,ending_stage_load || !m)){L->err=-2;L->campaign_error=8;loading_end();return;}
    /* Collision needs the largest temporary contiguous allocation before
       its lossless storage sharing. Reserve it before render/actor banks;
       after a stage reload those banks otherwise fragment the PSP heap. */
    runtime_log("COL_LOAD stage=%d before heap=%d\n",id,mallinfo().fordblks);
    L->hferr=col_load(&L->col,lvlpath(a,96,"stage",id,"col"));
    runtime_log("COL_LOAD stage=%d local=%d triangles=%d heap=%d\n",id,L->hferr,L->col.ntri,mallinfo().fordblks);
    /* Reserve the exact high-water mark of this JMSc bank while the heap
       still has large holes. Garage -> S1M01 used to grow the city grid
       only AFTER render/actor allocation: 984 KiB free in small holes
       could not hold its new contiguous index. Retained capacities cover
       every authored sub-set (including lesson/retry changes). No tasks
       exist yet; mission_begin still installs the selected set normally. */
    if(m && id!=4 && !L->hferr) {
        setobj_place(L,m,0);
        int sets=L->sobj ? *(int *)(L->sobj+4):0;
        for(int set=1;set<sets && !L->hferr;set++)setobj_place(L,m,set);
        if(!L->hferr)col_reset_extra(&L->col);
        L->nsobj=L->nsobrec=0;
        runtime_log("COL_RESERVE bank=%u sets=%d error=%d grid_bytes=%u heap=%d\n",
            m->set,sets,L->hferr,L->col.cellstart_cap*(unsigned)sizeof(int)+L->col.cellidx_bytes,mallinfo().fordblks);
    }
    /* Staff playback owns its recorded skater, not the ordinary mission's
       pedestrian/enemy tasks. Do not pin their unused banks on the PSP. */
    loading_phase="people";
    if(!ending_stage_load) { char pp[96]; int bank_stage = id == 6 ? 5 : id; snprintf(pp, sizeof pp, "people%d_compact.jgc", bank_stage); people_err = chars_load(&people, pp);  }
    { char zp[64]; snprintf(zp, sizeof zp, "zones%d.bin", id); L->zerr = zones_load(&L->zones, zp);  L->cur_zone = -1; }
    loading_phase="stage";
    {struct mallinfo mi=mallinfo();runtime_log("STAGE_LOAD stage=%d before arena=%u used=%u free=%u top=%u\n",id,(unsigned)mi.arena,(unsigned)mi.uordblks,(unsigned)mi.fordblks,(unsigned)mi.keepcost);}
    unsigned city_vram_bytes;void *city_vram=gfx_stage_texture_memory(&city_vram_bytes);
    L->err = stage_load_city_vram(&L->st, lvlpath(a, 96, "stage", id, "jgs"),city_vram,city_vram_bytes);
    runtime_log("STAGE_LOAD stage=%d local=%d objects=%u split=%d paged=%d\n",id,L->err,L->st.h ? L->st.h->n_obj:0,L->st.vertex_data!=NULL,L->st.geometry!=NULL);
    if(id!=4 && (L->err || L->hferr)) {
        L->campaign_error=8;
        runtime_log("CAMPAIGN required city unavailable stage=%d render=%d collision=%d\n",id,L->err,L->hferr);
        loading_end();return;
    }
    if(!L->err) {
        int source=stage_render_load(&L->st,lvlpath(a,96,"stage",id,"jrs"));
        runtime_log("STAGE_SOURCE stage=%d result=%d near_objects=%u\n",id,source,stage_render_near_objects(&L->st));
        if(id!=4 && source) {
            L->campaign_error=8;runtime_log("CAMPAIGN required renderer metadata unavailable stage=%d error=%d\n",id,source);
            loading_end();return;
        }
        int visibility=stage_visibility_load(&L->st,lvlpath(a,96,"stage",id,"jvo"));
        runtime_log("STAGE_VISIBILITY stage=%d result=%d\n",id,visibility);
        int binding=citysky_bind_stage(&L->st,id);
        unsigned texture_space;void *texture_memory=gfx_stage_texture_memory(&texture_space);
        stage_cache_textures(&L->st,texture_memory,texture_space);
        runtime_log("STAGE_VRAM direct=%u\n",L->st.texture_vram_bytes);
        int sky_error=ending_stage_load ? 0:citysky_load(&L->sky,id);
        runtime_log("CITY_SKY stage=%d binding=%d error=%d models=%u textures=%u heap=%d\n",
            id,binding,sky_error,L->sky.models.h ? L->sky.models.h->n_chars:0,
            L->sky.models.h ? L->sky.models.h->n_tex:0,mallinfo().fordblks);
        if(binding)L->sky.error=binding;
    }
    loading_present();
    loading_phase="props";
    if(id==1 && !ending_stage_load && !L->err && !L->hferr) {
        int error=shibuya_gates_load(&L->gates,&L->st,&L->col);
        runtime_log("SHIBUYA_GATES load=%d\n",error);
        if(error){L->campaign_error=8;loading_end();return;}
    }
    if(street_props_supported(id) && !ending_stage_load && !L->err && !L->hferr) {
        int error=street_props_load(&L->street_props,id,&L->col,&L->st);
        runtime_log("STREET_PROPS stage=%d load=%d effects=1\n",id,error);
        if(error){L->campaign_error=8;loading_end();return;}
    }
    if((id==1 || id==3 || id==5) && !ending_stage_load && !L->hferr) {
        int error=city_openings_load(&L->openings,id,&L->col,&L->st);
        runtime_log("CITY_OPENINGS stage=%d load=%d\n",id,error);
        if(error){L->campaign_error=8;loading_end();return;}
    }
    if(id==3 && !ending_stage_load && !L->hferr){
        int error=benten_train_load(&L->trains,&L->col);
        runtime_log("BENTEN_TRAIN load=%d\n",error);
        if(!error)error=benten_crane_load(&L->crane,&L->col);
        runtime_log("BENTEN_CRANE load=%d\n",error);
        if(error){L->campaign_error=8;loading_end();return;}
    }
    if(id==6 && !ending_stage_load && !L->hferr){
        int error=grind_props_load(&L->grind_props,&L->col);
        runtime_log("GRIND_PROPS load=%d\n",error);
        if(error){L->campaign_error=8;loading_end();return;}
    }
    if(id==5 && !ending_stage_load && !L->hferr){
        int error=bantam_train_load(&L->trains,&L->col);
        runtime_log("BANTAM_TRAIN load=%d\n",error);
        if(error){L->campaign_error=8;loading_end();return;}
    }
    if(id==2 && !ending_stage_load) {
        int error=kogane_house_load(&L->house);
        runtime_log("KOGANE_HOUSE load=%d\n",error);
        if(error){L->campaign_error=8;loading_end();return;}
        error=kogane_cover_load(&L->cover,&L->col);
        runtime_log("KOGANE_COVER load=%d\n",error);
        if(error){L->campaign_error=8;loading_end();return;}
        error=chars_load(&L->water_splash,"watersplash.jgc");
        if(error){chars_free(&L->water_splash);}
        runtime_log("KOGANE_WATER splash=%d\n",error);
        if(error){L->campaign_error=8;loading_end();return;}
    }
    { char sp[96]; snprintf(sp, sizeof sp, "stage%d_obj.jgs", id); L->sobst_ok = stage_load(&L->sobst, sp) == 0;  }
    if(!ending_stage_load && traffic_test)traffic_load(L);
    loading_present();
    if(id==4) {
        L->hferr=finalarena_load(&L->final,&L->col);
        if(!L->hferr) {
            L->hferr=audio_load_final_attacks();
            if(!L->hferr){L->final.attack_sound=final_attack_sound;L->final.boss_voice=audio_final_boss_voice;}
            else L->final.error=L->hferr;
        }
    }
    loading_phase="graffiti";
    L->gferr = graffiti_load(&L->gf, lvlpath(a, 96, "spots", id, "bin"), "tags.jgs");
    if(id!=4 && !L->gferr) {
        int error=citytags_load(&L->city_tags,&L->gf,id,story+0x3e5);
        runtime_log("CITY_TAGS load=%d count=%u art=%u,%u,%u\n",error,L->city_tags.count,story[0x3e5],story[0x3e6],story[0x3e7]);
    }
    if(L->final.loaded) {
        L->gferr=finalarena_bind_tags(&L->final,&L->gf);
        if(L->gferr){L->final.progression_error=1;printf("FINAL original target bank unavailable\n");}
        L->final.scene_begin=level_final_scene_begin;L->final.scene_context=L;
        if(!m && finaltags_art_load(&L->final_art,story[0x3e5],story[0x3e7])) {
            L->final.progression_error=1;printf("FINAL original graffiti art unavailable\n");
        }
    }
    if(!ending_stage_load) { int len=0;L->object_graffiti_data=read_file("objectgraffiti.bin",&len);
      if(gfo_bank(&L->object_graffiti,L->object_graffiti_data,len<0?0:(unsigned)len))printf("GROUP graffiti bank missing/invalid\n"); }
    /* Mission begin selects the exact JMSc sub-set/area can bank. */
    /* Campaign music comes from EVENT 59 / JMDc. Do not overwrite an
       existing recruitment chain with the old per-city preview ADX. */
    if(!ending_stage_load && !m){stream_set_chain(0,NULL);stream_set_file(lvlpath(b,96,"music",id,"adx"));}
    loading_present();
    /* spawn */
    player_init(&L->pl, 400.f, 60.f, 780.f);L->pl.world_stage=L->id;
    L->pl.health_max = L->pl.health = CHAR_HEALTH[CHAR_ID_OF_MODEL[pchar & 15]];
    if (m && !L->hferr) {
        /* mission start position (JMDc +0x70); snap to the ground under it */
        float g; L->pl.x = m->start[0]; L->pl.z = m->start[2]; L->pl.y = m->start[1];
        if (ground_at(&L->col, m->start[0], m->start[2], m->start[1] + 30.f, &g) == 0) L->pl.y = g; L->pl.onground = 1;
    } else if (!L->hferr) {
        float g, cx = (id == 1) ? 400.f : (L->st.h->bmin[0] + L->st.h->bmax[0]) * 0.5f, cz = (id == 1) ? 780.f : (L->st.h->bmin[2] + L->st.h->bmax[2]) * 0.5f;
        int found = 0; float bestscore = 1e9f, bx = cx, bz = cz, bg = 0;
        for (int gz = -60; gz <= 60; gz++) for (int gx = -60; gx <= 60; gx++) {
            float tx = cx + gx * 16.f, tz = cz + gz * 16.f, g0, gn; int ok = 1;
            if (ground_at(&L->col, tx, tz, 900.f, &g0) != 0 || g0 > 600.f) continue;
            for (int k = 0; k < 8 && ok; k++) { float ox = (k % 3 - 1) * 12.f, oz = (k / 3 - 1) * 12.f; if (ground_at(&L->col, tx + ox, tz + oz, g0 + 6.f, &gn) != 0 || g0 - gn > 6.f) ok = 0; }
            if (!ok) continue;
            float score = g0 * 2.f + sqrtf((float)(gx*gx + gz*gz)) * 8.f;
            if (score < bestscore) { bestscore = score; bx = tx; bz = tz; bg = g0; found = 1; }
        }
        if (found) { L->pl.x = bx; L->pl.z = bz; L->pl.y = bg + 0.5f; L->pl.onground = 1; }
        else if (ground_at(&L->col, cx, cz, 2000.f, &g) == 0) { L->pl.x = cx; L->pl.z = cz; L->pl.y = g + 0.5f; L->pl.onground = 1; }
        (void)g;
    }
    if(!m) {char pp[96];npcs_load(&L->npcs,lvlpath(pp,96,"peds",id,"bin"),&L->col,1,id);}
    if(!m)npcs_prepare_detail(&L->npcs,&chars,people_err ? NULL:&people);
    L->saved_pchar = -1;
    if(m && ending_stage_load) {
        /* Keep the stage's authored set geometry/collision, but no live
           JMDc, pickup HUD, controller bank or mission enemy allocation.
           END_START_STAFF supplies the original STAFF input/GF lifecycle. */
        setobj_place(L,m,0);
    } else if(m)mission_begin(L,m,0);
    loading_present();
    if(L->final.loaded)level_bind_final_player(L);
    else if(!ending_stage_load){
        if(id==2)L->pl.stage_fall_recovery=1;
        L->pl.before_tick=traffic_tick;L->pl.before_tick_context=L;
        L->pl.surface_contact=level_surface_contact;L->pl.surface_contact_context=L;
        L->pl.after_tick=traffic_after_tick;L->pl.after_tick_context=L;
    }
    L->cam_yaw = L->pl.yaw;   /* the DC camera starts exactly behind the skater (camyaw == yaw in trace16) */ L->cam_pitch = 0.f; player_cam_reset(&L->pl); L->cam_eye[0] = L->pl.cam_ex; L->cam_eye[1] = L->pl.y + 18; L->cam_eye[2] = L->pl.cam_ez;
    runtime_log("LEVEL_READY stage=%d error=%d heap=%u title=%d\n",id,L->campaign_error,(unsigned)mallinfo().fordblks,title_stage_load);
    loading_end();
}

/* Dreamcast stage-module background colours, recovered from the calls to
   FUN_8c27cb16 in the original executable:
     stage 1 FUN_8c02f4f0: FF808080
     stage 2 FUN_8c039a34: FF783410
     stage 3 FUN_8c031aa8: FF000014
     stage 5/6 FUN_8c0f33e0/FUN_8c0f3a3c: FF000000
   The source value is ARGB; GCOLOR converts RGB to the PSP's ABGR word. */
static unsigned int stage_back_color(int id)
{
    switch (id) {
    case 1: return GCOLOR(128, 128, 128, 255);
    case 2: return GCOLOR(120, 52, 16, 255);
    case 3: return GCOLOR(0, 0, 20, 255);
    case 5:
    case 6:
    default: return GCOLOR(0, 0, 0, 255);
    }
}

int main(int argc,char **argv)
{
    unsigned boot_time[8]={sceKernelGetSystemTimeLow()};
    (void)argc;(void)argv;
#if JGR_PORTABLE_ASSETS && JGR_BUNDLED_RUNTIME
    if(argc==3 && !strcmp(argv[1],"--jgr-root") && portable_assets_chdir_application(argv[2])<0){sceKernelExitGame();return 1;}
#endif
    SceCtrlData pad; unsigned prev = 0;ViewerSelect viewer_select_state={0};
    int th = sceKernelCreateThread("cb", callback_thread, 0x11, 0xFA0, 0, 0); if (th >= 0) sceKernelStartThread(th, 0, 0);
    scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0); sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
#if JGR_PORTABLE_ASSETS
    portable_assets_show_startup();
    boot_time[1]=sceKernelGetSystemTimeLow();
    /* A partial installation cannot boot. The initial frame remains visible
       while the receipt, startup tables and native game systems initialize. */
    int portable_setup=portable_assets_bootstrap();
    boot_time[2]=sceKernelGetSystemTimeLow();
    if(!portable_setup && source_data_startup_load()<0) {
        portable_assets_data_error();
        portable_setup=-1;
    }
    if(portable_setup<0) {
        while(running) {
            sceCtrlReadBufferPositive(&pad,1);
            if(pad.Buttons&PSP_CTRL_START)break;
            sceKernelDelayThread(50000);
        }
        sceKernelExitGame();return 0;
    }
#endif
#if !JGR_PORTABLE_ASSETS
    boot_time[1]=boot_time[2]=boot_time[0];
#endif
    boot_time[3]=sceKernelGetSystemTimeLow();
    runtime_log_deferred=1; /* One batch after startup, not one write per phase. */
    logfd = runtime_log_open_session();
    runtime_log("RELEASE_REPORT version=1 rotation=%d heartbeat_frames=60 health_frames=300\n",runtime_log_rotation);
    int crash_status=crash_init();
    runtime_log("CRASH_HANDLER status=%d installed=%d emulator=%d build=%s\n",
                crash_status,crash_status==0,crash_status==1,crash_build_id());
    {FILE *f=fixture_fopen("spikecapture","rb");spike_enabled=JGR_DIAGNOSTICS && f!=NULL;if(f)fclose(f);
     spike_capture_init(&spike_capture,spike_enabled);gfx_capture_mode(spike_enabled);
     stage_city_profile_mode(spike_enabled);
     runtime_log("SPIKE_CAPTURE_CONFIG enabled=%d threshold_us=%u warmup_frames=%u history=%u bytes=%u\n",spike_enabled,SPIKE_CAPTURE_THRESHOLD_US,SPIKE_CAPTURE_WARMUP,SPIKE_CAPTURE_HISTORY,(unsigned)(sizeof spike_capture+sizeof spike_pending+sizeof spike_before+sizeof spike_text_buffer+sizeof spike_overlap_collision+sizeof spike_overlap_player+sizeof spike_overlap_traffic+stage_capture_bytes()+3*sizeof(GfxFrameSample)));}
    runtime_log("PERFORMANCE_DIAGNOSTICS enabled=%d spike_capture=%d\n",JGR_DIAGNOSTICS,spike_enabled);
    int hang_status=hang_init();runtime_log("HANG_MONITOR status=%d interval_ms=500 threshold_ms=8000\n",hang_status);
    runtime_log("BOOT audio\n");
    /* Original compressed common/PLAY banks stay resident through gameplay.
       The unused legacy sfx.bin PCM bank cost 374 KiB at startup; retiring
       that allocation pays for the complete selected player's two banks. */
    audio_init("music1.adx");audio_player_cache(0);audio_set_music(0);
    boot_time[4]=sceKernelGetSystemTimeLow();
    /* Gameplay cast is selected after the mission/character is known.
       Garage and movie tasks own independent banks. */
    printf("SPRAY original camera bank status=%d\n",player_load_spray_cameras());
    if (!cherr) chars.motion_sel = player_anim(0);   /* source steady idle (87) for the stage-1 character audit lineup */
    cordon_err = chars_load(&cordon, "cordon.jgc");
    cans_err = chars_load(&cans, "cans.jgc");
    if(chars_load(&impactfx,"impactfx.jgc"))chars_free(&impactfx);
    skatefx_err = chars_load(&skatefx, "skatefx.jgc");
    tex2d_load(&skatetrails,"skatetrails.bin");
    runtime_log("SKATE_EFFECTS mesh=%d trail_atlases=%d\n",skatefx_err,skatetrails.n);
    gang_heli_probe=heli_collision_probe;gang_landing_tag=rocket_landing_tag;
    font_real_load("fontmain.bin");
    hud_load("gamehud.bin");
#if JGR_TEST_FIXTURES
    /* finaltest can load and draw a level during the probes below. Keep
       its original ordering; fixture graphics time is part of resources. */
    chars_set_pose_sine(player_fsca_sin);
    gfx_init();
#endif
    boot_time[5]=sceKernelGetSystemTimeLow();

    int pickuproadblocktest=0,pickuproadblocktick=0;unsigned pickuproadblockshots=0;
    {FILE *f=fixture_fopen("pickuproadblocktest","r");if(f){if(fscanf(f,"%d",&pickuproadblocktest)!=1)pickuproadblocktest=1;fclose(f);}}
    int poltest = 0, policehudtest=0, policehud_age=-1;
    int failurehudtest=0,failurehud_age=0,failure_retry_age=0;
    int messagehudtest=0,messagehud_started=0;unsigned messagehud_shots=0;
    int openingtest=0,opening_started=0;unsigned opening_tick=0,opening_shots=0,opening_buttons=0;
    {FILE *f=fixture_fopen("openingtest","r");if(f){if(fscanf(f,"%d",&openingtest)!=1 || openingtest<1 || openingtest>7)openingtest=0;fclose(f);}}
    int traintest=0,train_started=0,train_view_placed=0;unsigned train_tick=0,train_shots=0;
    {FILE *f=fixture_fopen("traintest","r");if(f){traintest=fgetc(f)=='1';fclose(f);}}
    int koganehousetest=0,house_started=0;unsigned house_tick=0,house_shots=0;
    unsigned water_shots=0;
    int sewerwatertest=0,sewerwater_started=0;unsigned sewerwater_tick=0,sewerwater_shots=0;
    {FILE *f=fixture_fopen("sewerwatertest","r");if(f){sewerwatertest=1;fclose(f);}}
    int rivalroutetest=0,rivalroute_phase=-1;
    unsigned passage_test_ticks=0,passage_test_departure=0;
    {FILE *f=fixture_fopen("rivalroutetest","r");if(f){fscanf(f,"%d",&rivalroutetest);fclose(f);}}
    {FILE *f=fixture_fopen("koganewatertest","r");if(f){water_test=1;fclose(f);}}
    {FILE *f=fixture_fopen("koganehousetest","r");if(f){if(fscanf(f,"%d",&koganehousetest)!=1 || koganehousetest<1 || koganehousetest>4)koganehousetest=1;fclose(f);}}
    int hudpolishtest=0,hudpolish_started=0;unsigned hudpolish_shots=0,finish_shots=0;
    int trickhudtest=0,trickhud_started=0,trickhud_base=0;unsigned trickhud_events=0,trickhud_shots=0;
    {FILE *f=fixture_fopen("trickhudtest","r");if(f){trickhudtest=1;fclose(f);}}
    {FILE *f=fixture_fopen("messagehudtest","r");if(f){messagehudtest=1;fclose(f);}}
    {FILE *f=fixture_fopen("hudpolishtest","r");if(f){hudpolishtest=1;fclose(f);}}
    {FILE *f=fixture_fopen("failurehudtest","r");if(f){fscanf(f,"%d",&failurehudtest);fclose(f);}}
    int pausehudtest=0,pausehud_phase=0;unsigned pausehud_tick=0;float pausehud_pos[3]={0};int pausehud_time=0,pausehud_health=0;
    {FILE *f=fixture_fopen("pausehudtest","r");if(f){fscanf(f,"%d",&pausehudtest);fclose(f);}}
    int resulthudtest=0,resulthudsouls=0;
    {FILE *f=fixture_fopen("resulthudtest","r");if(f){fscanf(f,"%d %d",&resulthudtest,&resulthudsouls);fclose(f);}}
    unsigned policehud_tick=0,policehud_shots=0;
    { SceUID t=fixture_open("policehudtest",PSP_O_RDONLY,0777);if(t>=0){policehudtest=1;sceIoClose(t);} }
    { FILE *f=fixture_fopen("traffictest","r");if(f){traffic_test=fgetc(f)=='1';fclose(f);} }
    { FILE *f=fixture_fopen("worldaudiotest","r");if(f){world_audio_test=traffic_test;fclose(f);} }
    { FILE *f=fixture_fopen("ambientaudiotest","r");if(f){ambient_audio_test=1;fclose(f);} }
    { FILE *f=fixture_fopen("missionentry","r");if(f){campaign_fixture=1;
        int character=-1,count=fscanf(f,"%d %d %d",&campaign_entry_record,&campaign_entry_set,&character);
        if(count<2 || campaign_entry_record<0 || campaign_entry_set<0)campaign_entry_record=-1;
        else if(count==3 && (unsigned)character<16)pchar=MODEL_OF_CHAR[character];
        fclose(f);} }
    /* Opt-in renderer smoke fixtures at the original rail/wall trace coordinates. */
    int movementtest=0, movementframe=0;
    PassageFixture passagefixture={0};
    {FILE *f=fixture_fopen("passagetest","r");if(f){int stage=1;if(fscanf(f,"%d",&stage)!=1)stage=1;passagefixture.enabled=stage==2 ? 2:1;fclose(f);}}
    int turninputtest=0,turninputticks=0,turninputshots=0;
    int skateinputtest=0,skateinputshots=0;float skateinputclock=0;
    {FILE *f=fixture_fopen("skateinputtest","r");if(f){skateinputtest=1;fclose(f);}}
    {FILE *f=fixture_fopen("turninputtest","r");if(f){turninputtest=1;fclose(f);}}
    { FILE *f=fixture_fopen("movementtest","r");if (f) { fscanf(f,"%d",&movementtest);fclose(f); } }
    int lessoninputtest=0,lesson_started=0,lesson_ticks=0;
    CtrlPlay lesson_input={0};const Mission *lesson_record=NULL;
    {FILE *f=fixture_fopen("lessoninputtest","r");if(f){lessoninputtest=1;fclose(f);}}
    int spraytest=0,spraytest_phase=0,spraytest_spot=-1,spraytest_ticks=0,spraytest_logged=0;
    int bus_event_wait=0,bus_event_captured=0,bus_event_shots=0,bus_event_large=0,bus_event_tags=0;
    int arrival_area=0,arrival_started=0,arrival_wait=0,arrival_scenes=0,arrival_done=0;
    {FILE *f=fixture_fopen("arrivaltest","r");if(f){fscanf(f,"%d",&arrival_area);fclose(f);arrival_area=!!arrival_area;}}
    CitySprayFixture cityspray={.target=-1};
    float spraytest_origin[3]={0},spraytest_point[3]={0};
    { FILE *f=fixture_fopen("spraytest","r");if(f){fscanf(f,"%d",&spraytest);fclose(f);} }
    int carlessontest=0,carlesson_shots=0;
    {FILE *f=fixture_fopen("carlessontest","r");if(f){carlessontest=1;fclose(f);}}
    int rivaldroptest=0,rival_drop_tick=0,rival_drop_pick=-1,rival_drop_initial=0,rival_drop_shots=0;
    int rival_drop_kind=18,rival_drop_index=0,rival_drop_count=0,rival_drop_health=0;
    {FILE *f=fixture_fopen("rivaldroptest","r");if(f){fscanf(f,"%d %d %d",&rivaldroptest,&rival_drop_kind,&rival_drop_index);fclose(f);}}
    int objecttagtest=0,objecttag_phase=0,objecttag_ticks=0;unsigned objecttag_handle=0;
    { FILE *f=fixture_fopen("objecttagtest","r");if(f){fscanf(f,"%d",&objecttagtest);fclose(f);} }
    int helitest=0,heli_test_index=-1,heli_test_tick=0;unsigned heli_test_shots=0,heli_test_parent=0;
    int policegrabtest=0,police_grab_tick=0,police_grab_max=0,police_grab_first=0,police_grab_release=0,police_grab_shots=0;
    {FILE *f=fixture_fopen("policegrabtest","r");if(f){fscanf(f,"%d",&policegrabtest);fclose(f);if(policegrabtest!=1 && policegrabtest!=2 && policegrabtest!=3)policegrabtest=0;}}
    int combattest=0,combat_test_tick=0;unsigned combat_test_captures=0;
    {FILE *f=fixture_fopen("combattest","r");if(f){fscanf(f,"%d",&combattest);fclose(f);}}
    { FILE *f=fixture_fopen("helitest","r");if(f){fscanf(f,"%d",&helitest);fclose(f);} }
    int gangtest_rec = -1;   /* debug: `gangtest` file holding a record index spawns that rival record after level load */
    int group_camera_test=0;
    {FILE *f=fixture_fopen("groupcameratest","r");if(f){group_camera_test=1;fclose(f);}}
    int autopilot = 0; char autoname[16] = {0}; { SceUID t = fixture_open("autopilot", PSP_O_RDONLY, 0777); if (t >= 0) { char ab[16] = {0}; int n = sceIoRead(t, ab, 15); { char *q = strstr(ab, "S"); if (q && n >= 6) { for (int k = 0; k < 6 && q[k] > 32; k++) autoname[k] = q[k]; } } autopilot = (n >= 6 && !memcmp(ab, "manual", 6)) ? 7 : (n >= 7 && !memcmp(ab, "neutral", 7)) ? 6 : (n >= 4 && !memcmp(ab, "look", 4)) ? 5 : (n >= 5 && !memcmp(ab, "chase", 5)) ? 4 : (n >= 5 && !memcmp(ab, "trial", 5)) ? 3 : (n >= 6 && !memcmp(ab, "tagall", 6)) ? 2 : 1; sceIoClose(t); } }   /* self-test; "neutral" never teleports or injects movement */
    /* Isolated emulator pressure fixture: PPSSPP does not reserve the
       physical ME callback arena/helper. No allocation in ordinary play. */
    static ExitFixture exit_fixture;
    int exit_fixture_destination=0;
    if(autopilot){FILE *f=fixture_fopen("exittest","r");if(f){exit_fixture.enabled=1;fclose(f);}}
    void *audio_heap_reserve=NULL;
    if(autopilot){FILE *f=fixture_fopen("audioheapreserve","r");if(f){
        unsigned bytes=0;if(fscanf(f,"%u",&bytes)==1 && bytes && bytes<=1024*1024) {
            audio_heap_reserve=malloc(bytes);if(audio_heap_reserve)memset(audio_heap_reserve,0,bytes);
            runtime_log("AUDIO_HEAP_RESERVE bytes=%u ok=%d\n",bytes,audio_heap_reserve!=NULL);
        }fclose(f);
    }}
    missions_load("missions.bin"); int mcur = -1;
    int cursor = 0; { SceUID t = fixture_open("level.txt", PSP_O_RDONLY, 0777); if (t >= 0) { char c = 0; sceIoRead(t, &c, 1); sceIoClose(t); for (int i = 0; i < NLEVELS; i++) if (LEVELS[i].id == c - '0') cursor = i; } }
    /* This singleton lives for the entire game. Keeping it on main's stack
       left less space than the movie renderer's nested scratch arrays need
       and overwrote the PSP thread stack during the opening movie. */
    static Level L; memset(&L, 0, sizeof L);
    int state = autoname[0] ? 0 : 2;   /* 0 stage menu (debug / autopilot with a mission name), 1 playing, 2 title screen (task 0xf), 3 garage, 4 STREET map, 5 character select */
    /* Main is entered once. Menu banks/text remain alive throughout play;
       keeping them in its frame consumed headroom in every nested collision
       and loading callback. Give these owners static storage, like Level. */
    static Chars tlogo;int tlogo_ok=0;static Tex2dSet title_ui;static List2dSet title_lists; /* owned by title entry, including returns */
    int garage_bgm = 0; static Chars garage, gmark; static Chars radio; int radio_ok = 0, rframe = 0, radio_test_action = 0; RadioUi radio_ui={0}; static char radio_names[52][48]; int radio_names_n = 0; int grftest = 0, gsub = 0, gsubframe = 0; static List2dSet mlists; int mlists_ok = 0; static Tex2dSet mdisp; int mdisp_ok = 0; int gpage = 0, gst = 0, gcur[3] = {0, 0, 0}; static char grf_names[102][24], grf_desc[204][24]; int grf_txt = 0; unsigned short *grf_px = 0; int grf_w = 0, grf_h = 0, grf_loaded = -1; int radiotest = 0, nettest = 0, nframe = 0;
    HudNotice garage_notice;hud_notice_init(&garage_notice);float notice_acc=0;int notice_wide_owned=0;
    GarageCast garage_cast={0};GarageSelection garage_selection={0};GarageDeparture garage_departure={0};GarageExit garage_exit={0};int garage_cast_active=0;
    unsigned garage_voice_tick=0;
    int garage_queued_direction=0;float garage_hold_time=0.f; /* PSP: taps during a pan are kept; a held direction skims */
    GaragePending pending={0};static Chars pending_cast;
    GarageRival street_rival={0};static Chars street_rival_model;int street_rival_active=0;
    int pending_ok=0,pending_checked=0,cpending=0,pending_axis=0;
    SavePrompt save_prompt={0};int save_error=0,save_return=3,movie_save_prompt=0,movie_save_yes=1;
    static RecordBook book;static RecordBrowser records;static Chars book_bank;
    RecordOptions options={0};
    int book_ok=0,book_child_done=0,book_axis=0,book_wide_owned=0;
    static EndingPlay ending;int endingtest=0,ending_previous=-1,ending_save_error=0;unsigned ending_captures=0,ending_spray_captures=0;
    unsigned movie_captures[35]={0};int campaign_movie=0,campaign_movie_return=3;
    int movie_route_test=0,movie_skip_tick=-1,movie_skip_sent=0,movie_capture_frame=60;
    int story_start_frame=700,story_skip_frame=-1;
    if(autopilot){FILE *f=fixture_fopen("storyinput","r");if(f){
        int start,skip;if(fscanf(f,"%d %d",&start,&skip)==2 && start>=0 && skip>=-1){story_start_frame=start;story_skip_frame=skip;}fclose(f);
    }}
    /* Opt-in capture only; the fixture never writes story progress. */
    int garagetest=0;
    { SceUID t=fixture_open("garagetest",PSP_O_RDONLY,0777);if (t>=0) {
        char mode=0;sceIoRead(t,&mode,1);sceIoClose(t);garagetest=mode>='1' && mode<='7' ? mode-'0':1;state=garagetest==2 || garagetest==3 || garagetest==6 ? 5:3;
    } }
    { SceUID t = fixture_open("grftest", PSP_O_RDONLY, 0777); if (t >= 0) { grftest = 1; sceIoClose(t); } t = fixture_open("radiotest", PSP_O_RDONLY, 0777); if (t >= 0) { radiotest = 1; state = 3; sceIoClose(t); } t = fixture_open("nettest", PSP_O_RDONLY, 0777); if (t >= 0) { nettest = 1; sceIoClose(t); } { extern int stage_flat; t = fixture_open("flat", PSP_O_RDONLY, 0777); if (t >= 0) { stage_flat = 1; sceIoClose(t); } } } int garage_ok = 0, gmark_ok = 0; int gframe = 0, gcursor = 2;   /* initial item = word 0x8c10dbf0; garage assets are loaded on entering the garage and freed when a level starts (PSP RAM) */
    static Tex2dSet gdisp; int gdisp_ok = 0;
    ChapterCard chapter_card={0};int chapter_wide_owned=0,chapter_checked=0;
    
#if JGR_PORTABLE_ASSETS
#include "source_data_main_mark_model.inc"
#else

#include "generated/source_data/main_mark_model.inc"

#endif
   /* 0x8c10dd64: marker object slot per entry */
    story_reset(); int new_story=story_load("jgr_story.sav")<0;
    if(!new_story)save_presentation(0);
    audio_set_mono(story[0x35c]!=0);
    {int error=audio_load_menu();if(logfd>=0){char b[64];int n=snprintf(b,sizeof b,"MENU_AUDIO load_error=%d\n",error);runtime_log_write(logfd,b,n);}}
    int sentry=0,sframe=0,street_city=0; /* Tokyo-to or Grind City. */
    static GrindMap grind_map;int grind_map_ok=0;static Chars grind_exit;
    float street_marker_frame[24]={0};
    if (garagetest) { memset(story+0x32c,1,garagetest==7 ? 3:15); } /* volatile capture */
    if(garagetest==4 || garagetest==5) { memset(story+0x336,0,5);story[0x33e]=1;story[0x351]=garagetest==5;story[0x356]=0;memset(story+8,0,4); }
    int cspecial=0;
    int csel = 0, cframe = 0, crec = -1; { extern int getenv_nocull, getenv_nohull, getenv_hullcw; SceUID t = fixture_open("poltest", PSP_O_RDONLY, 0777); if (t >= 0) { poltest = 1; sceIoClose(t); } { extern int gang_force_activate; t = fixture_open("gangtest", PSP_O_RDONLY, 0777); if (t >= 0) { char gb[16] = {0}; int gn = sceIoRead(t, gb, 15); gang_force_activate = 1; if (gn > 0 && gb[0] >= '0' && gb[0] <= '9') gangtest_rec = atoi(gb); sceIoClose(t); } } t = fixture_open("nocull", PSP_O_RDONLY, 0777); if (t >= 0) { getenv_nocull = 1; sceIoClose(t); } t = fixture_open("nohull", PSP_O_RDONLY, 0777); if (t >= 0) { getenv_nohull = 1; sceIoClose(t); } t = fixture_open("hullcw", PSP_O_RDONLY, 0777); if (t >= 0) { getenv_hullcw = 1; sceIoClose(t); } }   /* character select (task 0x219): source slot order = inverse of 0x8c10e06c, availability = story[0x32c + resident] */
    if(garagetest) { /* Optional isolated resident/pet fixture; never saved. */
        FILE *f=fixture_fopen("garagetest","r");int resident=-1,pet=-1;
        if(f){fscanf(f,"%*d %d %d",&resident,&pet);fclose(f);}
        if(resident>=0 && resident<15)for(int i=0;i<15;i++)if(garage_cast_character(i)==resident)csel=i;
        if(pet>=0 && pet<=3){story[0x33a]=0;story[0x35b]=pet==3 ? 2:pet ? 1:0;story[0x346]=pet==2;story[0x341]=0;}
    }
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_main_c__mark_0.inc"
#else

#include "generated/source_data/main_c__MARK_0.inc"

#endif
   /* garage bank marker objects (0x8c10dcb0 table), screen = (x, -y) */   /* garage_disptxr: 0 Tokyo-to map, 1 names/menu words, 2 ?, 3 carousel labels, 4 Bantam/Grind map, 5 SegaNet notice */   /* garage task 0x222: room bank + 3 camera paths (GCM1) */
    int title_frames = 0, title_mode = 0;
    int lookat_live=0;
    {FILE *f=fixture_fopen("lookatlive","r");if(f){if(fscanf(f,"%d",&lookat_live)!=1 || lookat_live<1)lookat_live=0;fclose(f);}}
    int gamemode = 1, cullmode = 0 /* the DC draws stage strips without back-face culling (Benten canal floor, Shibuya F tiles) */, wire = 0, freeze = 0, vsync = 1, logframes = 0; float logt = 0.f;
    float camtest[7] = {0},camtest_sweep=0; int camtest_on = 0,camtest_edge_capture=0,camtest_edge_shots=0; { SceUID t = fixture_open("camtest", PSP_O_RDONLY, 0777); if (t >= 0) { char cb[128] = {0}; sceIoRead(t, cb, 127); sceIoClose(t); if (sscanf(cb, "%f %f %f %f %f %f %f %f", &camtest[0], &camtest[1], &camtest[2], &camtest[3], &camtest[4], &camtest[5], &camtest[6], &camtest_sweep) >= 7) camtest_on = 1; } }   /* debug: DC camera eye/target/focal(640px) */
    float eye[3] = {0, 60, 0}, yaw = 0.f, pitch = -0.2f; float ms_avg = 33.f, cpu_avg = 0.f, ms_max = 0.f; int frames = 0; float planes[6][4];
    unsigned t_prev = sceKernelGetSystemTimeLow();
    int finalfalltest=0,finalcollapsetest=0,finalformtest=0;unsigned scene_shots=0;
    /* Isolated final-arena development entry; never selects/clears the boss
       campaign mission or writes its story progress. No fabricated boss AI. */
    int finalprogresstest=0,recruitinspect=0,recruitresulttest=0,recruit_garage_pending=0,recruit_handoff_frames=0;FinalFixture finalfixture={0};
    {FILE *f=fixture_fopen("recruitresulttest","r");if(f){int id=0;if(fscanf(f,"%d",&id)==1){if(id==1)id=13;for(int mode=1;mode<=16;mode++)if(scene_recruit_supported(id,mode))recruitresulttest=id;}fclose(f);}}
    { FILE *f=fixture_fopen("finaltest","r");if(f) {
        int fixture=fgetc(f);finalfalltest=fixture=='2';finalcollapsetest=fixture=='3' || fixture=='4';finalformtest=fixture=='4';fclose(f);const Mission *m=0;
        finalprogresstest=fixture=='5';
        for(int i=0;i<nmis;i++)if(!strncmp(missions[i].name,"S4M01",5)){m=&missions[i];break;}
        if(m){level_load(&L,4,m);mission_free(&L.ms);L.ms.scene_request=L.ms.scene_preload=-1;L.mrec=0;state=1;}
    } }
    { FILE *f=fixture_fopen("scenetest","r");if(f) {
        int id=0;int read=fscanf(f,"%d",&id);fclose(f);
        if(read==1 && (id==13 || id==14))recruitinspect=id;
        if(read==1 && (id==103 || id==90) && L.final.loaded) {
            if(id!=103 || finalarena_inspect_scene(&L.final,id)==0) {
                if(logfd>=0) {
                    unsigned removed=0;for(int i=0;i<4;i++)if(L.final.pieces[i].deleted)removed|=1u<<i;
                    char line[144];int n=snprintf(line,sizeof line,"SCENE_LAYOUT id=%d phase=%d removed=%u mask=%u boss_mask=%u\n",id,L.final.phase,removed,L.final.collider.active,L.final.boss_collider.active);
                    runtime_log_write(logfd,line,n);
                }
                sceneplay_inspect(&L.scene,id,CHAR_ID_OF_MODEL[pchar&15]);
            }
        }
    } }
    int eventinspect=-1,eventinspect_mode=0;
    {FILE *f=fixture_fopen("eventtest","r");if(f) {
        int id,secondary;if(fscanf(f,"%d %d",&id,&secondary)==2 && scene_bespoke_supported(id)){eventinspect=id;eventinspect_mode=secondary;}
        fclose(f);
    }}

    campaign_writable=eventinspect<0 && !autopilot && !campaign_fixture && !traffic_test && !garagetest && !combattest && !sewerwatertest && !rivalroutetest &&
        !pickuproadblocktest && !rivaldroptest && !policegrabtest && !movementtest && !passagefixture.enabled && !skateinputtest && !turninputtest && !lessoninputtest && !spraytest && !objecttagtest && !helitest && !poltest && !policehudtest && !failurehudtest && !pausehudtest && !resulthudtest && !messagehudtest && !koganehousetest && !water_test && !openingtest && !hudpolishtest && !trickhudtest &&
        gangtest_rec<0 && !traintest && !finalfalltest && !finalcollapsetest && !finalformtest && !finalprogresstest &&
        !camtest_on && !grftest && !radiotest && !nettest && !recruitinspect && !recruitresulttest;
    campaign_clear_fixture=rivalroutetest==4;
    int routetest=0,route_tick=0,route_action=-1,route_stage=1;
    {FILE *f=fixture_fopen("routetest","r");if(f){int mode=0;fscanf(f,"%d %d",&mode,&route_stage);fclose(f);
        if(mode==3 || mode==5 || mode==9 || mode==15 || mode==16 || mode==17 || mode==18 || mode==20 || mode==21 || mode==23 || mode==24 || mode==30){routetest=mode;campaign_writable=0;
            if(mode==17){ /* Isolated save-I/O fixture: never jgr_story.sav. */
                unsigned char check[1024];story[0x300]=0x31;
                int first=story_save("route_save_fixture.sav");story[0x300]=0x62;
                int second=story_save("route_save_fixture.sav"),n=-1,match=0;
                SceUID fd=fixture_open("route_save_fixture.sav",PSP_O_RDONLY,0777);
                if(fd>=0){n=sceIoRead(fd,check,sizeof check);sceIoClose(fd);match=n==sizeof check && !memcmp(check,story,sizeof check);}
                if(logfd>=0){char b[128];int z=snprintf(b,sizeof b,"ROUTE_DONE save_io first=%d second=%d read=%d match=%d\n",first,second,n,match);runtime_log_write(logfd,b,z);}
            }
            if(mode==18){state=3;gcursor=0;
                if(route_stage==2 || route_stage==3)story[0x33c]=1;
                if(route_stage>=5)story[0x351]=route_stage==6;
            }
            else if(mode==30){story_reset();story[8]=1;memset(story+0x32c,1,15);
                story[0x351]=story[0x356]=1;state=4;sentry=23;pending_checked=1;chapter_checked=1;new_story=0;
                if(route_stage<2 || route_stage>8)route_stage=2;}
            else if(mode>=20){state=3;gcursor=6;story[0x33e]=1;}
            else if(mode<15){memset(story+0x32c,1,15);story[0x32c+mode]=2;state=3;gcursor=2;
                if(mode==9){story_reset();story_mission_cleared(0,0x12,0,0,0);story_mission_cleared(0,0x15,0,0,0);story_mission_cleared(1,0,0,0,1);story_mission_cleared(5,0,1,0,1);}}
            else {save_prompt_init(&save_prompt);state=10;}}}}
    {FILE *f=fixture_fopen("endingtest","r");if(f){int mode=0;fscanf(f,"%d",&mode);fclose(f);
        if(mode==25 || mode==26 || mode==27){endingtest=mode;campaign_writable=0;new_story=0;state=15;
            endingplay_init(&ending,CHAR_ID_OF_MODEL[pchar&15]);
            if(mode==26)ending.flow.phase=3;
            if(mode==27)ending.flow.phase=2;}}}
    {FILE *f=fixture_fopen("movietest","r");if(f){int id=-1;fscanf(f,"%d %d %d %d",&id,&movie_route_test,&movie_skip_tick,&movie_capture_frame);fclose(f);
        if(movie_capture_frame<0 || movie_capture_frame>100000)movie_capture_frame=60;
        if((unsigned)id<35){endingtest=100+id;campaign_writable=0;new_story=0;state=15;
            endingplay_inspect_movie(&ending,id);
            if(movie_route_test==1 || movie_route_test==3 || movie_route_test==4){campaign_movie=1;campaign_movie_return=3;}
            else if(movie_route_test==2 && id==29) {
                const char *name=story_file_name(story_chapter_file(0,10));
                for(int i=0;i<nmis;i++)if(mis_head[i] && !strcmp(missions[i].name,name)){mcur=i;break;}
                if(mcur>=0){campaign_movie=1;campaign_movie_return=1;}else ending.error=30;
            }
            runtime_log("MOVIE_FIXTURE id=%d handoff=%d skip=%d writable=0 supplied_context=1\n",id,movie_route_test,movie_skip_tick);
        }}}
    RenderBench render_bench={0};unsigned benchmark_profile_frames=0;
    int npc_detail=1;
    {FILE *f=fixture_fopen("charlod","r");if(f){fscanf(f,"%d",&npc_detail);fclose(f);chars_lod_mode(npc_detail);}}
    {FILE *f=fixture_fopen("mipmaptest","r");if(f){int enabled=1;if(fscanf(f,"%d",&enabled)==1)stage_set_mipmaps(enabled);fclose(f);}}
    int render_bench_auto=0,render_bench_detail_mode=RENDER_BENCH_PED_OCCLUSION_PROXIES;
    int render_bench_diagnostics=0,render_bench_force_diagnostics=0;
    /* Native pedestrians are the gameplay default. Benchmarks measure and
       diagnose this path without changing the normal-play policy. */
    chars_ped_shared_mode(1);chars_ped_transform_mode(0);chars_ped_emit_mode(1);
    chars_actor_timing_mode(0);chars_ped_math_mode(1);npcs_occlusion_mode(NPC_OCCLUSION_PROXIES);
    runtime_log("PED_NATIVE_DEFAULT enabled=1 emitter=1 actor_clocks=0 combined_math=1 benchmark_required=0\n");
    runtime_log("PED_OCCLUSION_DEFAULT enabled=1 aggressive=0 proxies=1 body=torso blockers=bus_roof_proxies uncertain=visible max_proxies=%u\n",
        NPC_OCCLUSION_MAX_PROXIES);
    {FILE *f=fixture_fopen("renderbench","r");if(f){fscanf(f,"%d %d %d",&render_bench_auto,&render_bench_detail_mode,&render_bench_force_diagnostics);fclose(f);}}
    int realtime_profile=0,fixed_final_ticks=0;
    float vfpu_scope_ms=0,vfpu_budget30_pct=0;
    int vfpu_scope_valid=0;
    unsigned pose_me_last=0;float pose_wait_last=0.f;
    float realtime_profile_clock=0;
    { SceUID fd=fixture_open("realtimeprofile",PSP_O_RDONLY,0777);
      if(fd>=0){char mode=0;sceIoRead(fd,&mode,1);realtime_profile=mode=='2' ? 2:1;sceIoClose(fd);
          runtime_log("PERF_CLOCK wall_time=1 input=%s\n",realtime_profile==2 ? "drive":"neutral");} }
    if(autopilot==6 && !realtime_profile) {
        SceUID fd=fixture_open("fixedfinalticks",PSP_O_RDONLY,0777);
        if(fd>=0){char mode=0;sceIoRead(fd,&mode,1);sceIoClose(fd);
            if(mode>='1' && mode<='3')fixed_final_ticks=mode-'0';
            runtime_log("PERF_CLOCK fixed_final_ticks=%d gameplay_only=1 input=neutral\n",fixed_final_ticks);}
    }
    if(autopilot==6 && fixed_final_ticks) {
        SceUID fd=fixture_open("finalopts",PSP_O_RDONLY,0777);
        if(fd>=0){char mode[4]={0};sceIoRead(fd,mode,3);sceIoClose(fd);
            unsigned mask=(unsigned)atoi(mode);
            if(mode[0]>='0' && mode[0]<='9' && mask<16u) {
                chars_final_admission_retry_mode(mask&1u);
                chars_final_cel_vfpu_mode(mask&8u ? 2:!!(mask&2u));col_dirty_mode(mask&4u);
                runtime_log("PERF_FINAL_OPT_OVERRIDE mask=%u isolated_fixture=1\n",mask);
            }
        }
    }
    int hang_fixture=0;
    if(autopilot){FILE *f=fixture_fopen("hangtest","r");if(f){hang_fixture=1;fclose(f);}
        f=fixture_fopen("spikefixture","r");if(f){int mode=1;fscanf(f,"%d",&mode);spike_fixture=mode==2 ? 3:1;fclose(f);}}
    /* Automated legacy fixtures retain their detailed reports. Physical
       gameplay and spike captures never format/flush those reports. */
    runtime_verbose=JGR_DIAGNOSTICS && ((autopilot && autopilot!=7) || routetest || endingtest || garagetest || grftest || radiotest || nettest) && !spike_enabled;
    {FILE *f=fixture_fopen("verboseperf","r");if(f){runtime_verbose=JGR_DIAGNOSTICS;fclose(f);}}
    unsigned log_fixture=0;
    if(autopilot){FILE *f=fixture_fopen("logfixture","r");if(f){fscanf(f,"%u",&log_fixture);fclose(f);
        if(log_fixture){if(log_fixture<180)log_fixture=180;runtime_verbose=0;}}}
    runtime_log("LOG_MODE verbose=%d deferred=%d event_bytes=%u profile_bytes=%u windows=%u flush=pause_load_exit\n",
        runtime_verbose,!runtime_verbose,DEFERRED_LOG_CAPACITY,(unsigned)sizeof runtime_profiles,RUNTIME_PROFILE_WINDOWS);
    runtime_log_deferred=!runtime_verbose;
    boot_time[6]=sceKernelGetSystemTimeLow();
#if JGR_TEST_FIXTURES
    boot_time[7]=boot_time[6];
#else
    chars_set_pose_sine(player_fsca_sin);
    gfx_init();
    boot_time[7]=sceKernelGetSystemTimeLow();
#endif
    runtime_log("BOOT_TIMING splash_ms=%.3f bootstrap_ms=%.3f tables_ms=%.3f audio_ms=%.3f resources_ms=%.3f menus_ms=%.3f graphics_ms=%.3f total_ms=%.3f fixtures=%d\n",
        (boot_time[1]-boot_time[0])*.001f,(boot_time[2]-boot_time[1])*.001f,
        (boot_time[3]-boot_time[2])*.001f,(boot_time[4]-boot_time[3])*.001f,
        (boot_time[5]-boot_time[4])*.001f,(boot_time[6]-boot_time[5])*.001f,
        (boot_time[7]-boot_time[6])*.001f,(boot_time[7]-boot_time[0])*.001f,JGR_TEST_FIXTURES);
    runtime_log("BOOT ready stack_free=%d\n",sceKernelCheckThreadStack());
    while (running) {
        hang_frame(frames,L.id,state);
        while(power_sleeping && running)sceKernelDelayThread(1000);
        if(!running)break;
        {
            static int last_stage=-1,last_state=-1;
            if(last_stage!=L.id || last_state!=state) {
                release_report_event(RELEASE_EVENT_STATE,(unsigned)L.id,(unsigned)state,(unsigned)L.cur_zone);
                last_stage=L.id;last_state=state;
            } else if(frames%60u==0) {
                release_report_event(RELEASE_EVENT_HEARTBEAT,(unsigned)L.id,(unsigned)state,(unsigned)L.cur_zone);
            }
            if(frames%300u==0)runtime_release_health();
        }
        if(power_resumed) {
            spike_generation++;
            hang_mark(HANG_RESUME);
            power_resumed=0;
            scePowerSetClockFrequency(333,333,166);
            level_collect_gameplay_audio(&L);
            audio_player_resume();
            title_route_resume(&L.title_route);
            for(int i=0;i<3;i++)title_route_resume(&L.rival_route[i]);
            endingplay_resume(&ending);
            if(logfd>=0)sceIoClose(logfd);
            logfd=sceIoOpen("jgr_log.txt",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
            runtime_log("POWER resume complete; clocks reset and stream reopen requested\n");
            t_prev=sceKernelGetSystemTimeLow();gfx_note_resume(t_prev);
        }
        if(hang_fixture && frames>=360) {
            hang_fixture=0;
            runtime_log("HANG_FIXTURE synthetic_delay_ms=10000 frame=%u writable=0\n",frames);runtime_log_flush();
            unsigned phase=hang_mark(HANG_DIAGNOSTIC);
            sceKernelDelayThread(10000000);hang_mark(phase);
            runtime_log("HANG_FIXTURE recovered frame=%u\n",frames);
            t_prev=sceKernelGetSystemTimeLow();gfx_note_resume(t_prev);
        }
        audio_message_collect();
        hang_mark(HANG_PROFILE);
        unsigned t0 = sceKernelGetSystemTimeLow(); float dt = (t0 - t_prev) * 1e-6f; if (dt > 0.1f) dt = 0.1f; t_prev = t0;
        gfx_frame_start(t0);
        spike_log_start=spike_log_us;spike_bytes_start=spike_log_bytes;spike_calls_start=spike_log_calls;spike_profile_start=spike_profile_us;spike_ram_start=runtime_profile_collect_us;
        { GfxProfile p;if(JGR_DIAGNOSTICS && gfx_profile_read(&p)) {
          if(!runtime_verbose) {
            runtime_profile_remember(&p,frames,state,L.id,L.scene.vm.id,L.cur_zone);
            /* HUD estimates remain available without generating report text. */
            unsigned cpu[5];VfpuProfile vfpu;CharPoseProfile pose;
            chars_cpu_profile(cpu);vfpu_profile_read(&vfpu);chars_pose_profile(&pose);
            float scale=.001f/p.frames;
            float sampled=vfpu.sample_frames ? (float)(vfpu.clip_sample_us+vfpu.vertex_sample_us)*p.frames/vfpu.sample_frames:0.f;
            vfpu_scope_ms=(cpu[0]+vfpu.attachment_us+vfpu.visibility_us+sampled)*scale;
            vfpu_budget30_pct=vfpu_scope_ms*3.f;
            vfpu_scope_valid=chars_actor_timing_enabled() && (vfpu.sample_frames!=0 || vfpu.clip_calls==0);
            pose_me_last=pose.me_bones;pose_wait_last=pose.wait_us*scale;
            benchmark_profile_frames=0;npc_prepare_us=gang_prepare_us=pose_submit_us=0;
            col_profile_items(&L.col,NULL,0);
            unsigned collected=sceKernelGetSystemTimeLow()-t0;
            runtime_profile_collect_us+=collected;
            if(collected>runtime_profile_collect_max_us)runtime_profile_collect_max_us=collected;
          } else {
            /* One sequential Memory Stick write per profiling window,
               instead of a separate synchronous write for every line. */
            char profile_text[LOG_BATCH_SIZE];static unsigned profile_write_us;
            log_batch=profile_text;log_batch_used=0;
            float scale=.001f/p.frames;
            CharPreparationProfile prep;chars_preparation_profile(&prep);
            runtime_log("PERF_MODEL_PREP built=%u reused=%u owner_reads=%u vertex_reads=%u\n",prep.built_bones,prep.reused_bones,prep.owner_reads,prep.vertex_reads);
            unsigned submit[2],bones[2],textures[4];ge_submit_profile(submit);chars_bone_profile(bones);stage_texture_profile(textures);
            unsigned skin[4];chars_skin_profile(skin);
            unsigned packets[4];chars_render_profile(packets);
            unsigned skin_cpu[5];chars_cpu_profile(skin_cpu);
            {CharMaterialCacheProfile a;chars_material_cache_profile(&a);
             runtime_log("PERF_PROP_CACHE hits=%u misses=%u reused_vertices=%u evictions=%u bytes=%u\n",
                a.hits,a.misses,a.vertices,a.evictions,CHAR_MATERIAL_CACHE_BYTES);}
            {CharFinalVertexProfile a;chars_final_vertex_profile(&a);
             if(L.final.loaded || a.hits || a.fills) {
                 runtime_log("PERF_FINAL_VERTEX_CACHE hits=%u fills=%u reused_vertices=%u written_bytes=%u evictions=%u rejections=%u observations=%u search_steps=%u\n",
                     a.hits,a.fills,a.vertices,a.bytes,a.evictions,a.rejections,a.observations,a.search_steps);
                 runtime_log("PERF_FINAL_VERTEX_ADMISSION attempts=%u failures=%u redundant_probes_avoided=%u\n",
                     a.admission_attempts,a.admission_failures,a.redundant_probes_avoided);
             }}
            {CharFinalIndexedProfile a;chars_final_indexed_profile(&a);
             if(L.final.loaded || a.actors)runtime_log("PERF_FINAL_INDEXED actors=%u batches=%u source_vertices=%u prepared_vertices=%u reused_vertices=%u boundary_hits=%u fallback_batches=%u map_bytes=%u\n",
                 a.actors,a.batches,a.source_vertices,a.prepared_vertices,a.reused_vertices,a.boundary_hits,a.fallback_batches,a.map_bytes);}
            {CharLodProfile a;chars_lod_profile(&a);
             runtime_log("PERF_CHAR_LOD actors=%u texture_actors=%u hull_actors=%u omitted_triangles=%u texture_binds=%u source_texels=%u lod_texels=%u fallbacks=%u bytes=%u\n",
                a.actors,a.texture_actors,a.hull_actors,a.hull_triangles,a.texture_binds,a.source_texels,a.lod_texels,a.texture_fallbacks,a.bytes);}
            {CharArrowProfile a;chars_arrow_profile(&a);
             runtime_log("PERF_ARROWS ms=%.3f requests=%u drawn=%u culled=%u reference=%u pose_built=%u pose_reused=%u draws=%u scopes=%u bytes=%u\n",
                a.total_us*scale,a.requests,a.drawn,a.culled,a.reference,a.pose_built,a.pose_reused,a.draw_calls,a.scopes,CHAR_ARROW_BYTES);}
            AudioProfile mixer;audio_profile_read(&mixer);
            AudioMeProfile me_audio;audio_me_profile_read(&me_audio);
            PlayerProfile players;player_profile_read(&players);
            runtime_log("PERF_PLAYER ticks=%u total_ms=%.3f animation_ms=%.3f body_ms=%.3f ground_ms=%.3f audio_ms=%.3f camera_ms=%.3f effects_ms=%.3f\n",
                players.ticks,players.total_us*scale,players.animation_us*scale,players.body_us*scale,
                players.ground_us*scale,players.audio_us*scale,players.camera_us*scale,players.effects_us*scale);
            runtime_log("PERF_BODY_POSE prepared=%u reused=%u\n",players.body_prepared_bones,players.body_reused_bones);
            StageProfile world;stage_profile_read(&world);
            VfpuProfile vfpu;vfpu_profile_read(&vfpu);
            float clip_est_us=vfpu.sample_frames ? (float)vfpu.clip_sample_us*p.frames/vfpu.sample_frames:0.f;
            float vertex_est_us=vfpu.sample_frames ? (float)vfpu.vertex_sample_us*p.frames/vfpu.sample_frames:0.f;
            vfpu_scope_ms=(skin_cpu[0]+vfpu.attachment_us+vfpu.visibility_us+clip_est_us+vertex_est_us)*scale;
            vfpu_budget30_pct=vfpu_scope_ms*3.f;
            vfpu_scope_valid=chars_actor_timing_enabled() && (vfpu.sample_frames!=0 || vfpu.clip_calls==0);
            runtime_log("PERF_ACTOR_TIMING enabled=%d note=disabled_actor_times_are_unavailable work_counts_valid=1\n",chars_actor_timing_enabled());
            runtime_log("PERF state=%d stage=%d scene=%d frames=%u benchmark_frames=%u logic_ms=%.3f submit_ms=%.3f ge_ms=%.3f vblank_ms=%.3f max_ms=%.3f list=%u cpu_mhz=%d bus_mhz=%d list_limit=%u\n",
                state,L.id,L.scene.vm.id,p.frames,benchmark_profile_frames,p.logic_us*scale,p.submit_us*scale,p.ge_us*scale,p.vblank_us*scale,
                p.max_frame_us*.001f,p.max_list_bytes,scePowerGetCpuClockFrequencyInt(),scePowerGetBusClockFrequencyInt(),p.max_list_capacity);
            benchmark_profile_frames=0;
            runtime_log("PERF_SUBMIT draws=%u stall_updates=%u\n",submit[0],submit[1]);
            {unsigned writes[3];ge_writeback_profile(writes);
             runtime_log("PERF_GE_WRITEBACK frames=%u publications=%u ms=%.3f\n",writes[2],writes[0],writes[1]*scale);
             struct mallinfo mi=mallinfo();
             unsigned system_free=sceKernelTotalFreeMemSize(),system_largest=sceKernelMaxFreeMemSize();
             crash_memory_sample(frames,L.id,state,(unsigned)mi.fordblks,system_free,system_largest);
             runtime_log("PERF_MEMORY arena=%u used=%u heap_free=%u kernel_free=%u kernel_largest=%u\n",
                (unsigned)mi.arena,(unsigned)mi.uordblks,(unsigned)mi.fordblks,
                system_free,system_largest);}
            {CharPoseProfile prep;chars_pose_profile(&prep);pose_me_last=prep.me_bones;pose_wait_last=prep.wait_us*scale;
             runtime_log("PERF_POSE_PREP queued_bones=%u me_bones=%u cpu_bones=%u reused_bones=%u cursor_reused=%u batches=%u wait_ms=%.3f cancelled=%u chunks=%u me_ticks=%u max_chunk_ticks=%u bytes=%u verify=%d\n",
                prep.queued_bones,prep.me_bones,prep.cpu_bones,prep.reused_bones,prep.cursor_reused,prep.batches,
                prep.wait_us*scale,prep.cancelled,prep.chunks,prep.me_ticks,prep.max_chunk_ticks,prep.storage_bytes,prep.verify_status);}
            {unsigned materials[2];chars_material_profile(materials);
             runtime_log("PERF_MATERIAL applied=%u reused=%u\n",materials[0],materials[1]);}
            {CharPlanProfile plans;chars_plan_profile(&plans);
             runtime_log("PERF_DRAW_PLANS hits=%u built=%u entries=%u visited=%u build_ms=%.3f group_vertices=%u skipped=%u bytes=%u\n",
                plans.hits,plans.built,plans.entries,plans.visited,plans.build_us*scale,plans.group_vertices,plans.group_skipped,CHAR_DRAW_PLAN_BYTES);}
            runtime_log("PERF_BONES uploaded=%u reused=%u\n",bones[0],bones[1]);
            {unsigned grouped[3];chars_group_profile(grouped);
                runtime_log("PERF_GROUPED draws=%u weighted_vertices=%u max_weights=%u\n",grouped[0],grouped[1],grouped[2]);}
            runtime_log("PERF_VRAM textures=%u bytes=%u vram_binds=%u ram_binds=%u\n",textures[0],textures[1],textures[2],textures[3]);
            runtime_log("PERF_SKIN ge_batches=%u ge_vertices=%u cpu_batches=%u cpu_vertices=%u\n",skin[0],skin[1],skin[2],skin[3]);
            runtime_log("PERF_PACKETS prepared=%u reused=%u vertices_prepared=%u actors_culled=%u pose_ms=%.3f packet_ms=%.3f cpu_skin_ms=%.3f bounds_ms=%.3f posed_bones=%u\n",
                packets[0],packets[1],packets[2],packets[3],skin_cpu[0]*scale,skin_cpu[1]*scale,skin_cpu[2]*scale,skin_cpu[3]*scale,skin_cpu[4]);
            {unsigned phases[3],shadow[4];chars_draw_phase_profile(phases);garage_shadow_profile(shadow);
             runtime_log("PERF_GARAGE_DRAW posed_ms=%.3f ge_skin_ms=%.3f draw_skin_ms=%.3f shadow_pose_ms=%.3f shadow_transform_ms=%.3f shadow_volume_ms=%.3f shadow_submit_ms=%.3f\n",
                phases[0]*scale,phases[1]*scale,phases[2]*scale,shadow[0]*scale,shadow[1]*scale,shadow[2]*scale,shadow[3]*scale);
             unsigned cel[2],room[2];chars_cel_profile(cel);chars_static_cache_profile(room);
             runtime_log("PERF_CEL cut_triangles=%u batches=%u room_cache_hits=%u room_cache_fills=%u\n",cel[0],cel[1],room[0],room[1]);
             unsigned mixed[3];chars_cel_mixed_profile(mixed);
             if(L.final.loaded || mixed[0])runtime_log("PERF_CEL_MIXED batches=%u saved_draws=%u extra_bound_bytes=%u\n",mixed[0],mixed[1],mixed[2]);
             unsigned shared[3];chars_cel_shared_profile(shared);
             if(L.final.loaded || shared[0])runtime_log("PERF_CEL_SHARED cuts=%u vertices_avoided=%u edge_interpolations_avoided=%u\n",shared[0],shared[1],shared[2]);}
            {CharFinalClipProfile f;chars_final_clip_profile(&f);
             if(L.final.loaded || f.batches)runtime_log("PERF_FINAL_CLIP batches=%u safe_batches=%u clipped_batches=%u outside_batches=%u tested_triangles=%u clipped_triangles=%u output_triangles=%u\n",
                 f.batches,f.safe_batches,f.clipped_batches,f.outside_batches,f.tested_triangles,f.clipped_triangles,f.output_triangles);}
            {CharFinalGeNormalProfile f;chars_final_ge_normal_profile(&f);
             if(L.final.loaded || f.batches)runtime_log("PERF_FINAL_GE_NORMAL enabled=%d batches=%u vertices=%u built=%u reused=%u rigid_rejected=%u fallbacks=%u packet_bytes=%u\n",
                 chars_final_ge_normal_enabled(),f.batches,f.vertices,f.built,f.reused,f.rigid_rejected,f.fallbacks,f.packet_bytes);}
            {CharFinalCelPacketProfile f;chars_final_cel_packet_profile(&f);
             if(L.final.loaded || f.hits || f.built)runtime_log("PERF_FINAL_CEL_PACKET hits=%u built=%u misses=%u rejected=%u triangles=%u cuts_reused=%u bytes=%u resident_bytes=%u\n",
                 f.hits,f.built,f.misses,f.rejected,f.triangles,f.cuts_reused,f.bytes,f.resident_bytes);}
#if defined(JGR_ACTOR_TIMING_FORCE) && JGR_ACTOR_TIMING_FORCE
            runtime_log("PERF_FINAL_LOOKUP ms=%.3f\n",chars_final_lookup_profile()*scale);
#endif
            {unsigned movie_times[4];endingplay_render_profile(movie_times);
                if(state==15 || movie_times[0] || movie_times[1] || movie_times[2] || movie_times[3])
                    runtime_log("PERF_MOVIE id=%d segment=%d sample_ms=%.3f ordinary_ms=%.3f cel_ms=%.3f shadow_ms=%.3f prepared_light_vertices=%u\n",ending.movie.movie,ending.movie.segment,
                        movie_times[0]*scale,movie_times[1]*scale,movie_times[2]*scale,movie_times[3]*scale,ending.prepared_light_vertices);}
            {FinalArenaProfile f;finalarena_profile_read(&f);
             if(L.final.loaded || f.updates || f.scene_updates || f.draw_calls || f.fire_calls) {
                 runtime_log("PERF_FINAL updates=%u scene_updates=%u ticks=%u scene_ticks=%u collision_samples=%u publishes=%u triangles=%u tag_prepares=%u after_ticks=%u update_ms=%.3f scene_ms=%.3f collision_ms=%.3f publish_ms=%.3f tags_ms=%.3f other_ms=%.3f after_ms=%.3f\n",
                     f.updates,f.scene_updates,f.ticks,f.scene_ticks,f.collision_samples,f.world_publishes,f.world_triangles,f.tag_prepares,f.after_ticks,
                     f.update_us*scale,f.scene_us*scale,f.collision_us*scale,f.publish_us*scale,f.tags_us*scale,f.other_us*scale,f.after_us*scale);
                 runtime_log("PERF_FINAL_DRAW calls=%u fire_calls=%u total_ms=%.3f hazard_ms=%.3f background_ms=%.3f platforms_ms=%.3f center_ms=%.3f boss_ms=%.3f fire_ms=%.3f\n",
                     f.draw_calls,f.fire_calls,f.draw_us*scale,f.hazard_us*scale,f.background_us*scale,f.platforms_us*scale,f.center_us*scale,f.boss_us*scale,f.fire_us*scale);
                 runtime_log("PERF_FINAL_BOSS root_ms=%.3f surface_ms=%.3f goji_ms=%.3f attachments_ms=%.3f orbit_parts_ms=%.3f\n",
                     f.boss_root_us*scale,f.boss_surface_us*scale,f.goji_us*scale,f.attachments_us*scale,f.orbit_parts_us*scale);
             }}
            runtime_log("PERF_PACKET_CACHE capacity_bytes=%u metadata_extra_bytes=%u\n",CHAR_GE_PACKET_BYTES,CHAR_GE_PACKET_EXTRA_BYTES+CHAR_GE_GROUP_METADATA_BYTES);
            runtime_log("PERF_DRAW other_ms=%.3f sky_ms=%.3f stage_ms=%.3f props_ms=%.3f actors_ms=%.3f effects_ms=%.3f hud_ms=%.3f tags_ms=%.3f\n",
                p.section_us[GFX_OTHER]*scale,p.section_us[GFX_SKY]*scale,p.section_us[GFX_STAGE]*scale,
                p.section_us[GFX_PROPS]*scale,p.section_us[GFX_ACTORS]*scale,p.section_us[GFX_EFFECTS]*scale,p.section_us[GFX_HUD]*scale,p.section_us[GFX_TAGS]*scale);
            {CharWorkStats work[CHAR_WORK_COUNT];chars_work_profile(work);
             static const char *const names[CHAR_WORK_COUNT]={"PLAYER","PEDESTRIANS","TRAFFIC","OTHER"};
             for(unsigned i=0;i<CHAR_WORK_COUNT;i++) {
                 const CharWorkStats *w=work+i;
                 runtime_log("PERF_ACTOR_%s calls=%u bones=%u bone_uploads=%u skin_draws=%u skin_vertices=%u culled=%u total_ms=%.3f pose_ms=%.3f bounds_ms=%.3f packet_ms=%.3f skin_ms=%.3f cpu_skin_ms=%.3f\n",
                     names[i],w->calls,w->bones,w->bone_uploads,w->skin_draws,w->skin_vertices,w->culled,
                     w->total_us*scale,w->pose_us*scale,w->bounds_us*scale,w->packet_us*scale,w->skin_us*scale,w->cpu_skin_us*scale);
             }
             runtime_log("PERF_ACTOR_PREP npc_ms=%.3f gang_ms=%.3f submit_ms=%.3f\n",
                 npc_prepare_us*scale,gang_prepare_us*scale,pose_submit_us*scale);
             npc_prepare_us=gang_prepare_us=pose_submit_us=0;}
            {CharPedPacketProfile p;chars_ped_packet_profile(&p);
             runtime_log("PERF_PED_PACKETS draws=%u strip_draws=%u source_vertices=%u vertices=%u source_indices=%u indices=%u triangles=%u built=%u reused=%u fallbacks=%u\n",
                 p.draws,p.strip_draws,p.source_vertices,p.vertices,p.source_indices,p.indices,p.triangles,p.built,p.reused,p.fallbacks);}
            {NpcOcclusionProfile o;npcs_occlusion_profile(&o);
             runtime_log("PERF_PED_OCCLUSION frames=%u enabled_frames=%u candidates=%u tested=%u hidden=%u kept=%u prepare_skipped=%u draw_skipped=%u query_ms=%.3f occluders=%u late_tested=%u late_hidden=%u aggressive_frames=%u rays=%u ray_blocked=%u ray_uncertain=%u ray_unavailable=%u ray_triangles=%u ray_steps=%u proxy_frames=%u proxy_tests=%u proxy_blocked=%u\n",
                 o.frames,o.enabled_frames,o.candidates,o.tested,o.hidden,o.kept,o.prepare_skipped,o.draw_skipped,o.query_us*scale,o.occluders,o.late_tested,o.late_hidden,
                 o.aggressive_frames,o.rays,o.ray_blocked,o.ray_uncertain,o.ray_unavailable,o.ray_triangles,o.ray_steps,o.proxy_frames,o.proxy_tests,o.proxy_blocked);}
            {CharPedSharedProfile p;chars_ped_shared_profile(&p);
             runtime_log("PERF_PED_NATIVE actors=%u fallback_actors=%u source_vertices=%u unique_positions=%u draws=%u triangles=%u indices=%u vertex_bytes=%u built=%u reused=%u transform_ms=%.3f gather_ms=%.3f submit_ms=%.3f\n",
                 p.actors,p.fallback_actors,p.source_vertices,p.unique_positions,p.draws,p.triangles,p.indices,p.vertex_bytes,p.built,p.reused,
                 p.transform_us*scale,p.gather_us*scale,p.submit_us*scale);}
            runtime_log("PERF_STAGE fast_tris=%u tested_tris=%u rejected_tris=%u clipped_tris=%u clipped_vertices=%u draw_calls=%u\n",
                world.fast_triangles,world.tested_triangles,world.rejected_triangles,
                world.clipped_triangles,world.clipped_vertices,world.draw_calls);
            runtime_log("PERF_STAGE_JOIN saved_draws=%u copied_index_bytes=%u cache_hits=%u\n",
                world.joined_draws,world.joined_index_bytes,world.index_cache_hits);
            runtime_log("PERF_VISIBILITY frames=%u ms=%.3f box_rejected=%u occluders=%u tests=%u hidden_objects=%u searches=%u batch_tests=%u hidden_batches=%u hidden_batch_tris=%u\n",
                world.visibility_frames,world.visibility_us*scale,world.box_rejected,world.occluders,world.occlusion_tests,world.occlusion_rejected,world.occlusion_searches,
                world.occlusion_batch_tests,world.occlusion_batches,world.occlusion_batch_triangles);
            runtime_log("PERF_STAGE_MATERIAL cached=%u\n",world.material_calls);
            {unsigned mip_stats[2];stage_mip_profile(mip_stats);
             runtime_log("PERF_STAGE_MIPS textures=%u vram_bytes=%u\n",mip_stats[0],mip_stats[1]);}
            runtime_log("PERF_STAGE_SOURCE frames=%u ms=%.3f sector_rejected=%u range_rejected=%u near=%u far=%u\n",
                world.selection_frames,world.selection_us*scale,world.sector_rejected,world.range_rejected,world.near_selected,world.far_selected);
            if(L.street_props)runtime_log("PERF_STREET_PROP_VIEW candidates=%u sector_rejected=%u range_rejected=%u selected=%u\n",
                world.prop_candidates,world.prop_sector_rejected,world.prop_range_rejected,world.prop_selected);
            if(L.street_props)runtime_log("PERF_STREET_PROP_RENDER prepared=%u reused=%u empty_passes=%u instances=%u\n",
                world.prop_prepared,world.prop_prepare_hits,world.prop_empty_passes,world.prop_instance_draws);
            runtime_log("PERF_VFPU pose_enabled=%d clip_enabled=%d hw_busy_available=0 pose_scope_ms=%.3f visibility_scope_ms=%.3f clip_scope_est_ms=%.3f scope_budget30_pct=%.2f\n",
                VFPU_PROFILE_POSE_ENABLED,VFPU_PROFILE_CLIP_ENABLED,skin_cpu[0]*scale,vfpu.visibility_us*scale,clip_est_us*scale,vfpu_budget30_pct);
            runtime_log("PERF_VFPU_SAMPLES clip_calls=%u samples=%u sample_frames=%u max_us=%u estimate_valid=%d cpu_budget30_left_ms=%.3f\n",
                vfpu.clip_calls,vfpu.clip_samples,vfpu.sample_frames,vfpu.clip_sample_max_us,
                vfpu_scope_valid,33.333333f-(p.logic_us+p.submit_us)*scale);
            runtime_log("PERF_VFPU_WORK pose_matrices=%u spheres=%u sphere_vectors=%u clip_vectors=%u\n",skin_cpu[4],vfpu.spheres,vfpu.spheres*2,vfpu.clip_vectors);
            runtime_log("PERF_VFPU_TRANSFORM batches=%u points=%u exact_rows=%u samples=%u sample_us=%u sample_frames=%u hw_busy_available=0\n",
                vfpu.transform_batches,vfpu.transform_points,vfpu.transform_exact_rows,vfpu.transform_samples,
                vfpu.transform_sample_us,vfpu.sample_frames);
            runtime_log("PERF_VFPU_EXTRA attachment_ms=%.3f attachment_matrices=%u vertex_scope_est_ms=%.3f vertex_chunks=%u vertices=%u samples=%u\n",
                vfpu.attachment_us*scale,vfpu.attachment_matrices,vertex_est_us*scale,vfpu.vertex_chunks,vfpu.vertices,vfpu.vertex_samples);
            runtime_log("PERF_AUDIO callbacks=%u total_ms=%.3f max_ms=%.3f\n",mixer.callbacks,mixer.total_us*.001f,mixer.max_us*.001f);
            runtime_log("PERF_ME enabled=%d jobs=%u samples=%u fallbacks=%u timeouts=%u wait_ms=%.3f max_wait_ms=%.3f submit_ms=%.3f polls=%u quarantined=%d\n",
                me_audio.enabled,me_audio.jobs,me_audio.samples,me_audio.fallbacks,me_audio.timeouts,
                me_audio.wait_us*.001f,me_audio.max_wait_us*.001f,me_audio.submit_us*.001f,me_audio.polls,me_audio.quarantined);
            runtime_log("PERF_ME_STATUS load_error=%d boot_status=%d ping_error=%d ping_us=%u mixer_nonwait_est_ms=%.3f\n",
                me_audio.load_error,me_audio.boot_status,me_audio.ping_error,me_audio.ping_us,
                fmaxf(0.f,(float)mixer.total_us-me_audio.wait_us)*.001f);
            runtime_log("PERF_ME_VERIFY status=%d samples=%u startup_ms=%.3f\n",
                me_audio.verify_status,me_audio.verify_samples,me_audio.verify_us*.001f);
            runtime_log("PERF_ME_MIX full_callback=%d verify_status=%d verify_samples=%u startup_ms=%.3f workspace_bytes=%u sequence_capacity=%u\n",
                me_audio.full_callback,me_audio.mix_verify_status,me_audio.mix_verify_samples,me_audio.mix_verify_us*.001f,me_audio.workspace_bytes,me_audio.sequence_capacity);
            runtime_log("PERF_ME_MIX_FAILURE kind=%d result=%d wait_ms=%.3f limit_ms=%.3f recoveries=%u reference_ms=%.3f\n",
                me_audio.mix_fail_kind,me_audio.mix_fail_result,me_audio.mix_fail_wait_us*.001f,
                me_audio.mix_fail_timeout_us*.001f,me_audio.mix_recoveries,me_audio.mix_reference_us*.001f);
            unsigned decode_me[3];audio_decode_profile(decode_me);
            runtime_log("PERF_ME_MUSIC jobs=%u blocks=%u cancelled=%u\n",decode_me[0],decode_me[1],decode_me[2]);
#if defined(__PSP__) && !defined(JGR_SCALAR_COLLISION_MATH)
            runtime_log("PERF_VFPU_COLLISION enabled=1 hw_busy_available=0\n");
#else
            runtime_log("PERF_VFPU_COLLISION enabled=0 hw_busy_available=0\n");
#endif
            runtime_log("PERF_ME_POWER suspends=%u resumes=%u resume_errors=%u\n",me_audio.suspends,me_audio.resumes,me_audio.resume_errors);
            runtime_log("PERF_AUDIO_DEADLINE lock_ms=%.3f max_lock_ms=%.3f max_gap_ms=%.3f late_callbacks=%u log_write_ms=%.3f\n",
                mixer.lock_us*.001f,mixer.max_lock_us*.001f,mixer.max_gap_us*.001f,mixer.late_callbacks,profile_write_us*.001f);
            runtime_log("PERF_AUDIO_PARTS sampled_callbacks=%u sampled_frames=%u dry_ms=%.3f reverb_ms=%.3f output_ms=%.3f service_ms=%.3f worker_ms=%.3f\n",
                mixer.detail_callbacks,mixer.detail_frames,mixer.dry_us*.001f,mixer.reverb_us*.001f,mixer.output_us*.001f,mixer.service_us*.001f,mixer.worker_us*.001f);
            runtime_log("PERF_STAGE_BOUNDS tested=%u rejected=%u\n",world.tested_batches,world.rejected_batches);
            runtime_log("PERF_STAGE_GROUPS tested=%u accepted=%u rejected=%u\n",world.tested_groups,world.accepted_groups,world.rejected_groups);
            runtime_log("PERF_MESH_BOUNDS tested=%u coarse=%u\n",world.mesh_bound_tests,world.mesh_coarse_decisions);
            runtime_log("PERF_CLIP_PLAN vertices=%u scalar_vertices=%u reduced_masks=%u bytes=%u\n",
                world.classified_vertices,world.scalar_vertices,world.reduced_group_masks,L.st.clip_group_bytes);
            runtime_log("PERF_STAGE_COMMANDS cached=%u regrouped=%u\n",world.cached_calls,world.sorted_batches);
            MusicProfile music;StreamProfile transport;
            audio_music_profile(&music);stream_profile(&transport);
            runtime_log("PERF_MUSIC on=%d paused=%d class=%d gain=%u frames=%u nonzero=%u underruns=%u peak=%u active=%d suspended=%d exhausted=%d decoded=%u queued=%u thread_error=%d io_error=%d open_failures=%u entry=%d\n",
                music.enabled,music.paused,music.stream_class,music.gain_q15,music.frames,music.nonzero_frames,music.underruns,music.input_peak,
                transport.active,transport.suspended,transport.exhausted,transport.decoded_blocks,transport.queued_frames,
                transport.thread_error,transport.io_error,transport.open_failures,bgm_current());
            runtime_log("PERF_STREAM priority=%u buffers=%u read_total_ms=%.3f decode_total_ms=%.3f max_read_ms=%.3f max_decode_ms=%.3f\n",
                transport.reader_priority,transport.published_buffers,transport.read_us*.001f,transport.decode_us*.001f,
                transport.max_read_us*.001f,transport.max_decode_us*.001f);
            runtime_log("PERF_LOGIC other_ms=%.3f player_ms=%.3f rivals_ms=%.3f npcs_ms=%.3f mission_ms=%.3f cans_ms=%.3f gangs_ms=%.3f\n",
                p.logic_section_us[LOGIC_OTHER]*scale,p.logic_section_us[LOGIC_PLAYER]*scale,
                p.logic_section_us[LOGIC_RIVALS]*scale,p.logic_section_us[LOGIC_NPCS]*scale,p.logic_section_us[LOGIC_MISSION]*scale,
                p.logic_section_us[LOGIC_CANS]*scale,p.logic_section_us[LOGIC_GANGS]*scale);
            ColProfile collision;col_profile_read(&collision);
            {unsigned predicates[2];col_predicate_profile(predicates);
             runtime_log("PERF_COLLISION_NATIVE resolved=%u boundary_exact=%u\n",predicates[0],predicates[1]);}
            runtime_log("PERF_COLLISION rays=%u bodies=%u ray_ms=%.3f body_ms=%.3f\n",
                collision.ray_queries,collision.body_queries,collision.ray_us*scale,collision.body_us*scale);
#if defined(__PSP__) && !defined(JGR_SCALAR_COLLISION_MATH)
            runtime_log("PERF_VFPU_COLLISION_SCOPES ray_ms=%.3f body_ms=%.3f hw_busy_available=0\n",collision.ray_us*scale,collision.body_us*scale);
#endif
            runtime_log("PERF_COL_PATCH entries=%u boxes=%u triangles=%u batches=%u vfpu=%d\n",
                collision.source_entries,collision.source_patches,collision.source_triangles,collision.patch_batches,
#if defined(__PSP__) && !defined(JGR_SCALAR_COLLISION_MATH) && !defined(JGR_SCALAR_PATCH_BOUNDS)
                1
#else
                0
#endif
            );
            ColItemProfile items[6];int item_count=col_profile_items(&L.col,items,6);
            for(int item=0;item<item_count;item++) {
                const ColItemProfile *q=items+item;
                runtime_log("PERF_COL_ITEM address=%08x kind=%u first=%u count=%u candidates=%u patches=%u skipped=%u ray_exact=%u body_exact=%u samples=%u sample_us=%u\n",
                    q->address,q->kind,q->first,q->count,q->candidates,q->patch_tests,q->patch_skipped,
                    q->ray_exact,q->body_exact,q->samples,q->sample_us);
            }
            runtime_log("PERF_COL_REUSE rays=%u bodies=%u ray_overflow=%u body_overflow=%u\n",
                collision.ray_query_reuse,collision.body_query_reuse,collision.ray_reuse_overflow,collision.body_reuse_overflow);
            runtime_log("PERF_COL_PLANES reused=%u prepared=%u body_reused=%u body_prepared=%u\n",
                collision.plane_hits,collision.plane_misses,collision.body_plane_hits,collision.body_plane_misses);
            if(L.final.loaded || collision.grid_publishes || collision.grid_fallbacks) {
                runtime_log("PERF_COL_GRID publishes=%u rebuilds=%u reused_rects=%u changed_rects=%u fallbacks=%u bytes=%u\n",
                    collision.grid_publishes,collision.grid_rebuilds,collision.grid_reused_rects,
                    collision.grid_changed_rects,collision.grid_fallbacks,collision.grid_bytes);
                runtime_log("PERF_COL_GRID_PHASES bounds_ms=%.3f copy_ms=%.3f update_ms=%.3f indexed_publishes=%u indexed_bytes=%u\n",
                    collision.grid_bounds_us*scale,collision.grid_copy_us*scale,collision.grid_update_us*scale,
                    collision.grid_indexed_publishes,collision.grid_indexed_bytes);
                runtime_log("PERF_COL_DIRTY publishes=%u visited=%u skipped=%u builds=%u fallbacks=%u bytes=%u\n",
                    collision.grid_dirty_publishes,collision.grid_dirty_visited,collision.grid_dirty_skipped,
                    collision.grid_dirty_builds,collision.grid_dirty_fallbacks,collision.grid_dirty_bytes);
                runtime_log("PERF_COL_UNCHANGED triangles=%u vertices=%u cells=%u\n",
                    collision.grid_unchanged_triangles,collision.grid_unchanged_vertices,collision.grid_unchanged_cells);
                runtime_log("PERF_COL_CELL_REUSE same_codes=%u skipped_rects=%u evaluated_rects=%u\n",
                    collision.grid_cell_code_same,collision.grid_cell_rect_skips,collision.grid_rect_evaluated);
            }
            runtime_log("PERF_COL_CANDIDATES reused=%u built=%u overflow=%u overflow_reused=%u\n",collision.candidate_hits,collision.candidate_misses,collision.candidate_overflows,collision.candidate_overflow_hits);
            runtime_log("PERF_COL_REJECT ray_reused=%u body_reused=%u stack_free=%d\n",
                collision.ray_reject_reuse,collision.body_reject_reuse,sceKernelCheckThreadStack());
            runtime_log("PERF_ENEMY gas_created=%u gas_impacts=%u gas_expired=%u gas_dropped=%u gas_error=%d shots=%u shot_dropped=%u warning_error=%d hits=%u\n",
                L.combat.gases.created,L.combat.gases.impacts,L.combat.gases.expired,L.combat.gases.dropped,L.combat.gas_error,
                L.combat.shots.created,L.combat.shots.dropped,L.combat.warning_error,L.combat.received);
            if(L.combat.burning_attempted)runtime_log("PERF_BURNING error=%d streams=%u dropped=%u bytes=%u burns=%u burn_dropped=%u\n",L.combat.burning_error,L.combat.fire_created,L.combat.fire_dropped,(unsigned)(ENEMY_FIRE_CAPACITY*sizeof(EnemyFire)),L.combat.burns_created,L.combat.burns_dropped);
            if(L.grind_props)runtime_log("GRIND_ELECTRIC waves=%u dropped=%u model_error=%d\n",L.combat.waves.created,L.combat.waves.dropped,L.combat.error);
            PathWorldProfile traffic_profile;pathworld_profile_read(&traffic_profile);
            runtime_log("PERF_TRAFFIC ticks=%u objects=%u boxes=%u total_ms=%.3f root_ms=%.3f body_ms=%.3f surface_ms=%.3f grip_ms=%.3f\n",
                traffic_profile.ticks,traffic_profile.objects,traffic_profile.body_prepared,traffic_profile.total_us*scale,
                traffic_profile.root_us*scale,traffic_profile.body_us*scale,traffic_profile.surface_us*scale,traffic_profile.grip_us*scale);
            TrafficDrawProfile traffic_draw;traffic_draw_profile(&traffic_draw);
            runtime_log("PERF_TRAFFIC_DRAW frames=%u candidates=%u source_rejected=%u frustum_rejected=%u close_kept=%u drawn=%u near=%u far=%u bones=%u triangles=%u ms=%.3f sample_prepares=%u sample_cache_bytes=%u bounds_prepares=%u state_batches=%u\n",
                traffic_draw.frames,traffic_draw.candidates,traffic_draw.source_rejected,traffic_draw.frustum_rejected,traffic_draw.close_kept,
                traffic_draw.drawn,traffic_draw.near_drawn,traffic_draw.far_drawn,traffic_draw.posed_bones,traffic_draw.triangles,traffic_draw.us*scale,
                traffic_draw.sample_prepares,L.traffic ? (unsigned)(PATHWORLD_CAPACITY*sizeof L.traffic->world.slots[0].render_key_hint):0,
                traffic_draw.bounds_prepares,traffic_draw.state_batches);
            unsigned write_started=sceKernelGetSystemTimeLow();runtime_log_flush();
            profile_write_us=sceKernelGetSystemTimeLow()-write_started;log_batch=NULL;
            if(spike_enabled)spike_profile_us+=sceKernelGetSystemTimeLow()-t0;
          }
        } }
        spike_begin_counters();
        vfpu_profile_frame_begin();
        hang_mark(HANG_INPUT);
        sceCtrlReadBufferPositive(&pad, 1); unsigned pressed = pad.Buttons & ~prev; prev = pad.Buttons;
        /* Earned-tag handoff fixture: only confirm the ordinary results. */
        if(rivalroutetest==4 && state==1 && L.ms.phase==1 && frames%30==0)pressed|=PSP_CTRL_CROSS;
        /* Read the physical SELECT hold before title/demo input replaces it. */
        pressed=viewer_select_pressed(&viewer_select_state,pad.Buttons,pressed,sceKernelGetSystemTimeLow());
        if(routetest){dt=1.f/60.f;pad.Buttons=pressed=0;pad.Lx=pad.Ly=128;route_tick++;
            if(state==3 && (route_tick==20 || (routetest==9 && gframe==20)))pressed=PSP_CTRL_CROSS;
            if(state==10){if(route_tick==30)pressed=PSP_CTRL_CIRCLE;
                if(routetest==16 && route_tick==40)pressed=PSP_CTRL_LEFT;
                if(route_tick==80)pressed=PSP_CTRL_CROSS;
                if(routetest==23 && route_tick%10==0)pressed=save_prompt.yes ? PSP_CTRL_LEFT:PSP_CTRL_CROSS;}}
        if(routetest==30) {
            if(state==4 && !street_city && route_action<0 && sframe==30) {
                dump_shot("shot_grind_exit.raw");pressed=PSP_CTRL_CROSS;route_action=0;
            }else if(state==4 && street_city && route_action==0 && sframe==30) {
                dump_shot("shot_grind_map.raw");sentry=route_stage;route_action=1;
            }else if(state==4 && street_city && route_action==1 && sframe==60) {
                dump_shot("shot_grind_selected.raw");pressed=PSP_CTRL_CROSS;route_action=2;
            }else if(state==5 && route_action==2 && cframe==60) {
                pressed=PSP_CTRL_CIRCLE;route_action=3;
            }else if(state==4 && street_city && route_action==3) {
                runtime_log("GRIND_RETURN city=%d entry=%d writable=%d\n",street_city,sentry,campaign_writable);
                pressed=PSP_CTRL_CROSS;route_action=4;
            }else if(state==5 && route_action==4 && cframe%60==30)pressed=PSP_CTRL_CROSS;
            if(state==4 && !street_city && route_action==2) {
                runtime_log("GRIND_DONE map_return city=%d writable=%d\n",street_city,campaign_writable);
                runtime_log_flush();break;
            }
            if(state==1 && L.loaded && route_action==4) {
                runtime_log("GRIND_DONE mission=%s stage=%d error=%d writable=%d\n",L.mrec->name,L.id,L.campaign_error,campaign_writable);
                runtime_log_flush();break;
            }
        }
        if (state!=3 && state!=5) garage_cast_active=0;
        if(state!=3 && notice_wide_owned){font_real_free_wide();notice_wide_owned=0;hud_notice_init(&garage_notice);notice_acc=0;}
        /* Menus and movies own their cast. Gameplay's bank returns at the
           next stage load, after every previous stage/pose borrower retires. */
        if(state>=3 && chars.base && !L.loaded && !ending.staff_busy && !ending.spray_actor.base) {
            chars_free(&chars);cherr=1;
            runtime_log("GAMEPLAY_CHARACTERS retired_for_%s heap=%u\n",state==15 ? "movie":"menu",(unsigned)mallinfo().fordblks);
        }
        if(state!=4 && grind_exit.base)chars_free(&grind_exit);
        if(state!=6 && radio_ok){chars_free(&radio);radio_ok=0;}
        if(radiotest && state==3 && radio_test_action==4){
            runtime_log("RADIO_DONE garage=%d stream=%d models=%d\n",state==3,bgm_current(),radio_ok);
            runtime_log_flush();break;
        }
        if(state!=8 && state!=9) {
            if(mdisp_ok){tex2d_free(&mdisp);mdisp_ok=0;}
            if(mlists_ok){list2d_free(&mlists);mlists_ok=0;}
            if(grf_px){gfx_wait_idle();free(grf_px);grf_px=NULL;grf_loaded=-1;}
        }
        if(routetest==18) {
            if(state==3 && gframe>=2) {
                if(route_action<0){gcursor=0;pressed=PSP_CTRL_CROSS;route_action=0;}
                else if(route_action==2){runtime_log("MENU_RETURN gallery models=%d textures=%d preview=%d\n",mlists_ok,mdisp_ok,grf_px!=NULL);gcursor=5;pressed=PSP_CTRL_CROSS;route_action=3;}
                else if(route_action==4){runtime_log("MENU_RETURN radio models=%d\n",radio_ok);gcursor=route_stage>=5 ? 1:2;pressed=PSP_CTRL_CROSS;route_action=5;}
            }
            if(state==8 && gsubframe>=30){
                if(route_action==0){pressed=PSP_CTRL_CROSS;route_action=1;}
                else if(route_action==2)pressed=PSP_CTRL_CIRCLE;
            }
            if(state==9 && gsubframe>=60 && route_action==1){dump_shot("shot_menu_gallery.raw");runtime_log("MENU_ENTER gallery models=%d textures=%d preview=%d\n",mlists_ok,mdisp_ok,grf_px!=NULL);pressed=PSP_CTRL_CIRCLE;route_action=2;}
            if(state==6 && radio_ui.phase==RADIO_IDLE && route_action==3){dump_shot("shot_menu_radio.raw");runtime_log("MENU_ENTER radio models=%d\n",radio_ok);pressed=PSP_CTRL_CIRCLE;route_action=4;}
            if(route_action==5 && state==4 && sframe>=20){sentry=route_stage==2 ? 4:route_stage==3 ? 8:0;pressed=PSP_CTRL_CROSS;}
            if(route_action==5 && state==5 && cframe%60==30)pressed=PSP_CTRL_CROSS;
        }

        if(state==15) { /* Original 9ff ending parent, not a garage shortcut. */
            if(title_ui.data)tex2d_free(&title_ui);
            if(title_lists.data)list2d_free(&title_lists);

            if(tlogo_ok){chars_free(&tlogo);tlogo_ok=0;}
if(garage_ok){garage_cast_assets_free(&garage);garage_ok=0;}
            if(gmark_ok){chars_free(&gmark);gmark_ok=0;}
            if(gdisp_ok){tex2d_free(&gdisp);gdisp_ok=0;}
            if(book_ok){chars_free(&book_bank);book_ok=0;}
            unsigned dc=(pressed&PSP_CTRL_CROSS ? 4:0)|(pressed&PSP_CTRL_START ? 8:0)|
                (pressed&PSP_CTRL_LEFT ? 0x40:0)|(pressed&PSP_CTRL_RIGHT ? 0x80:0);
            float ax=(pad.Lx-128.f)/128.f,ay=(pad.Ly-128.f)/128.f,axis=fminf(1,sqrtf(ax*ax+ay*ay));
            if(endingtest && ending.flow.phase==4 && ending.flow.sub==2)dc=ending.flow.yes ? 0x40:4;
            if(endingtest>=100 && !movie_skip_sent && movie_skip_tick>=0 && ending.movie.phase==14 && ending.movie.frame>=movie_skip_tick){dc|=8;movie_skip_sent=1;}
            if(autopilot && campaign_movie && campaign_movie_return==1 && !movie_skip_sent && story_skip_frame>=0 && ending.movie.phase==14 && ending.movie.frame>=story_skip_frame){dc|=8;movie_skip_sent=1;}
            int save_failed_before=ending_save_error;
            if(!ending_save_error){ending.edges|=dc;ending.remainder+=dt;}
            while(!ending.error && !ending_save_error && ending.remainder>=1.f/30.f){
                ending.remainder-=1.f/30.f;
                EndingRequest request=endingplay_step(&ending,&L.col,&chars,story,ending.edges,ax<0 ? 4:ax>0 ? 8:0,axis);ending.edges=0;
                if(ending.movie_source.length && ending.movie.audio_request>=0)
                    runtime_log("MOVIE_AUDIO movie=%d segment=%d caption=%.0f reference=%d start=%d error=%d\n",
                        ending.movie.movie,ending.movie.segment,ending.movie.caption_frame,
                        ending.movie.audio_request,ending.movie.audio_start,ending.error);
                if(request==END_LOAD_STAGE){
                    const char *name=story_file_name(story_chapter_file(0,1));const Mission *m=NULL;
                    for(int i=0;i<nmis;i++)if(!strcmp(missions[i].name,name)){m=&missions[i];break;}
                    if(!m)ending.error=30;
                    else {ending_stage_model=ending.model;ending_stage_load=1;level_load(&L,1,m);ending_stage_load=0;
                        if(L.err || L.hferr || L.campaign_error)ending.error=31;
                        /* Staff task owns movement; do not retire/update the
                           ordinary campaign mission loaded for stage setup. */
                        L.ms.active=0;L.mrec=NULL;
                        ending.staff_graffiti=L.gferr ? NULL:&L.gf;
                        if(!ending.staff_graffiti || !L.city_tags.count)ending.error=32;
                        else for(int i=0;i<L.gf.n;i++)L.gf.spots[i].active=1;
                    }
                }
                if(request==END_RELEASE_STAGE)level_unload(&L);
                if(request==END_SAVE_YES && campaign_writable && save_campaign("jgr_story.sav"))ending_save_error=1;
                if(request==END_DONE){
                    if(campaign_movie) {
                        int id=ending.movie.movie,skipped=ending.movie.skipped,route=movie_exit_route(id,skipped);
                        runtime_log("CAMPAIGN_MOVIE done=%d skipped=%d next=%d return=%d writable=%d stack_free=%d\n",id,skipped,route,campaign_movie_return,campaign_writable,sceKernelCheckThreadStack());
                        endingplay_free(&ending);ending_previous=-1;
                        if(route>=0){endingplay_begin_movie(&ending,route);break;}
                        campaign_movie=0;audio_set_music(0);gframe=0;pending_checked=0;garage_bgm=0;
                        if(route==MOVIE_ROUTE_SAVE){save_prompt_init(&save_prompt);save_prompt.yes=movie_save_yes;save_error=0;save_return=3;movie_save_prompt=1;state=10;}
                        else if(campaign_movie_return==1) {
                            pchar=MODEL_OF_CHAR[0];bgm_stage_idx=0;bgm_slot=10;
                            level_load(&L,1,&missions[mcur]);audio_set_music(!L.campaign_error);state=1;new_story=0;
                            /* Like the ordinary stage-entry path, retire the
                               movie transition's mute before tutorial play. */
                            runtime_log("CAMPAIGN_MOVIE tutorial=%.15s error=%d\n",missions[mcur].name,L.campaign_error);
                        }else state=campaign_movie_return;
                        break;
                    }
                    if(logfd>=0){char b[144];int n=snprintf(b,sizeof b,"ENDING_DONE cards=%d save=%d writable=%d return=title fixture=%d\n",ending.cards_shown,ending.save_yes,campaign_writable,endingtest);runtime_log_write(logfd,b,n);}
                    endingplay_free(&ending);level_unload(&L);audio_set_music(0);new_story=0;state=2;break;
                }
            }
            if(state!=15)continue;
            int stamp=ending.flow.phase*1000000+ending.movie.segment*100000+ending.movie.phase*1000+
                (ending.single_movie ? (int)ending.movie.frame/30:ending.clock/30);
            if(ending.error)stamp=-100-ending.error; /* -1 is the unlogged sentinel. */
            if(stamp!=ending_previous){ending_previous=stamp;
                if(ending.error){
                    runtime_log("MOVIE_LOAD_ERROR movie=%d step=%d error=%d heap=%u\n",ending.single_movie-1,ending.load_step,ending.load_error,(unsigned)mallinfo().fordblks);
                    if(ending.load_step==4)runtime_log("ENDING_CARD_LOAD_ERROR image=%d buffer=%d error=%d heap=%u\n",ending.load_card_image,ending.load_card_buffer,ending.load_error,(unsigned)mallinfo().fordblks);
                }
                if(logfd>=0){unsigned pos;int active,error;audio_movie_voice_info(&pos,&active,&error);char b[256];
                    int n=snprintf(b,sizeof b,"ENDING phase=%d sub=%d movie=%d mphase=%d segment=%d frame=%.0f staff=%d row=%d cards=%d error=%d voice=%u/%d/%d free=%u source_bytes=%u source_pages=%u\n",ending.flow.phase,ending.flow.sub,ending.movie.movie,ending.movie.phase,ending.movie.segment,ending.movie.frame,ending.clock,ending.replay.row,ending.cards_shown,ending.error,pos,active,error,sceKernelTotalFreeMemSize(),ending.movie_source.length,ending.movie_source.paged.pages ? (ending.movie_source.length+SCENE_PAGE_SIZE-1)/SCENE_PAGE_SIZE:0);runtime_log_write(logfd,b,n);}
            }
            gfx_begin();gfx_clear(0xff000000);
            if(ending.staff_busy || (ending.movie_source.length && ending.movie.phase>=14)){
                SceneCamera camera;endingplay_view(&ending,&camera);
                if(camera.active){float proj[16],view[16],clip[16],planes[6][4];
                    float focal=player_camera_focal(camera.fov,640,1),near=camera.near_clip,far=camera.far_clip;
                    if(near<=0)near=1;if(far<=near)far=20000;
                    near=ending.staff_busy ? ge_world_near(near):ge_movie_near(ending.movie.movie,ending.movie.segment,near);
                    player_camera_matrix(camera.pos,camera.forward,camera.roll,view);
                    m_perspective(proj,2.f*atanf(240.f/focal),640.f/480.f,near,far);
                    if(ending.staff_busy){float vp[6];staff_card_viewport(&ending.card,vp);
                        sceGuViewport(2048+(int)((vp[2]-320)*.75f),2048+(int)((vp[3]-240)*(272.f/480.f)),(int)(vp[0]*.75f),(int)(vp[1]*(272.f/480.f)));}
                    gu_set(GU_PROJECTION,proj);gu_set(GU_VIEW,view);gfx_mode3d();chars_final_lighting(0);chars_environment_view(view);
                    if(ending.staff_busy){
                        m_mul(clip,proj,view);frustum_planes(clip,planes);
                        if(!L.err){endingplay_draw_sky(&ending,&camera,planes);
                            if(!L.zerr)stage_render_sector(&L.st,zone_of(&L.zones,camera.pos[0],camera.pos[1],camera.pos[2]));
                            stage_render_view(&L.st,camera.pos,camera.forward,1.f/proj[0],NULL);stage_draw(&L.st,planes,clip,0);}
                        if(L.sobst_ok)for(int i=0;i<L.nsobrec;i++){
                            int oi=L.sobrec[i].type,nt=L.sobst.h->n_obj/2;if(oi<0 || oi>=nt)continue;
                            float x=L.sobrec[i].x-camera.pos[0],y=L.sobrec[i].y-camera.pos[1],z=L.sobrec[i].z-camera.pos[2];
                            if(x*x+y*y+z*z>=90000)oi+=nt;
                            sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();ScePspFVector3 pos={L.sobrec[i].x,L.sobrec[i].y,L.sobrec[i].z};sceGumTranslate(&pos);sceGumRotateY(L.sobrec[i].yaw);stage_draw_obj(&L.sobst,oi);
                        }
                        endingplay_draw_player(&ending,&chars);
                        draw_skate_effects(&ending.player);
                        if(!L.gferr)citytags_draw_staff(&L.city_tags,&L.gf,ending.cards);
                    }else endingplay_draw_movie(&ending);
                    sceGuViewport(2048,2048,480,272);
                }
            }
            if(ending.staff_busy)endingplay_draw_cards(&ending);
            gfx_mode2d();
            if(ending.flow.phase==4 && ending.flow.sub==2){
                font_real_draw_xy(224*.75f,228*(272.f/480.f),.75f,272.f/480.f,0xffffffff,"     SAVE?      ");
                font_real_draw_xy(224*.75f,264*(272.f/480.f),.75f,272.f/480.f,ending.flow.yes ? 0xffffffff:0xff808080,"  YES   ");
                font_real_draw_xy(344*.75f,264*(272.f/480.f),.75f,272.f/480.f,ending.flow.yes ? 0xff808080:0xffffffff," NO ");
            }
            if(ending.error){char b[96];snprintf(b,sizeof b,"%s ASSET/PLAYBACK ERROR %d - O: TITLE",ending.single_movie ? "MOVIE":"ENDING",ending.error);font_real_draw_center(240,135,.7f,0xffffffff,b);
                if(pressed&PSP_CTRL_CIRCLE){endingplay_free(&ending);level_unload(&L);campaign_movie=0;state=2;}}
            if(ending_save_error){font_real_draw_center(240,195,.8f,0xffffffff,"SAVE FAILED - X: RETRY  O: WITHOUT SAVING");
                if(save_failed_before && (pressed&PSP_CTRL_CROSS))ending_save_error=save_campaign("jgr_story.sav")!=0;
                else if(pressed&PSP_CTRL_CIRCLE){ending.save_yes=0;ending_save_error=0;}}
            gfx_end();
            if(endingtest || rivalroutetest==4){
                if(ending.staff_busy && ending.staff_graffiti)for(int i=0;i<L.gf.n;i++) {
                    const Spot *s=L.gf.spots+i;
                    if(s->started && s->stroke>0 && s->stroke<=7 && !(ending_spray_captures&(1u<<s->stroke)) && (s->reveal>=48 || s->complete)) {
                        char path[64];snprintf(path,sizeof path,"shot_ending_spray_%u.raw",s->stroke);dump_shot(path);ending_spray_captures|=1u<<s->stroke;
                    }
                }
                if(ending.movie.phase==14 && ending.movie.frame>=movie_capture_frame && (unsigned)ending.movie.movie<35 &&
                   !(movie_captures[ending.movie.movie]&(1u<<ending.movie.segment))){char path[64];snprintf(path,sizeof path,"shot_ending_%d_%d.raw",ending.movie.movie,ending.movie.segment);dump_shot(path);movie_captures[ending.movie.movie]|=1u<<ending.movie.segment;}
                if(ending.staff_busy && ending.card.phase==2 && ending.card.hold<=90 &&
                   ending.cards_shown<32 && !(ending_captures&(1u<<ending.cards_shown))){
                    char path[64];snprintf(path,sizeof path,"shot_ending_card_%02d.raw",ending.cards_shown);dump_shot(path);ending_captures|=1u<<ending.cards_shown;}
            }
            frames++;continue;
        }

        if (state == 2) {   /* title task 0xf: stage 1 + mission file 0 played back behind the animated logo bank; START/A leaves (pad bits 0xc) */
            if(!tlogo_ok)tlogo_ok=chars_load(&tlogo,"title.jgc")==0;
            title_stage_load=1;level_load(&L, 1, nmis > 0 ? &missions[0] : 0);title_stage_load=0;
            if(!title_ui.data)tex2d_load(&title_ui,"title_ui.bin");
            if(!title_lists.data)list2d_load(&title_lists,"title_lists.bin");
            demo_start(&L); title_mode = 1; title_frames = 0; state = 1; continue;
        }
        if(state==10) { /* 0163bc -> a00 -> mode 12 SAVE, or mode 5 return */
            unsigned dc=(pressed&PSP_CTRL_CROSS ? 4:0)|(pressed&PSP_CTRL_START ? 8:0)|
                (pressed&PSP_CTRL_LEFT ? 0x40:0)|(pressed&PSP_CTRL_RIGHT ? 0x80:0);
            float ax=(pad.Lx-128.f)/128.f,ay=(pad.Ly-128.f)/128.f;
            float axis=fminf(1,sqrtf(ax*ax+ay*ay));unsigned analog=ax<0 ? 4:ax>0 ? 8:0;
            int previous_yes=save_prompt.yes;
            if(recruit_garage_pending || ((movie_route_test || rivalroutetest==4) && movie_save_prompt))dc=save_prompt.yes ? 0x40:4; /* no-save fixture chooses No */
            /* Movie prompt 0151e4 accepts A only; task-a00 also accepts START. */
            int mode=save_error ? (pressed&PSP_CTRL_CROSS ? 12:0):save_prompt_step(&save_prompt,movie_save_prompt ? dc&~8u:dc,analog,axis);
            if(movie_save_prompt)movie_save_yes=save_prompt.yes;
            if(previous_yes!=save_prompt.yes)audio_menu_sound(0);
            if(mode)audio_menu_sound(1);
            if(mode) {
                if(mode==12 && campaign_writable && save_campaign("jgr_story.sav")) {
                    save_error=1;
                } else {
                    if(movie_route_test)runtime_log("MOVIE_HANDOFF_DONE save=%d return=%d writable=%d\n",mode==12,save_return,campaign_writable);
                    printf("STORY SAVE choice=%d writable=%d return=%d\n",mode==12,campaign_writable,save_return);
                    if(routetest && routetest!=23 && logfd>=0){char b[80];int n=snprintf(b,sizeof b,"ROUTE_DONE save=%d writable=%d\n",mode==12,campaign_writable);runtime_log_write(logfd,b,n);}
                    state=save_return;movie_save_prompt=0;if(state==12)book_child_done=1;gframe=0;pending_checked=0;continue;
                }
            }
            gfx_begin();gfx_clear(GCOLOR(0,0,0,255));gfx_mode2d();
            /* Strings and positions from 21a838 / 0f41c8, English column. */
            font_real_draw_xy(224*.75f,228*(272.f/480.f),.75f,272.f/480.f,0xffffffff,"     SAVE?      ");
            font_real_draw_xy(224*.75f,264*(272.f/480.f),.75f,272.f/480.f,save_prompt.yes ? 0xffffffff:0xff808080,"  YES   ");
            font_real_draw_xy(344*.75f,264*(272.f/480.f),.75f,272.f/480.f,save_prompt.yes ? 0xff808080:0xffffffff," NO ");
            if(save_error){font_real_draw_center(240,195,1,0xffffffff,"SAVE FAILED - X: RETRY  O: RETURN");
                if(pressed&PSP_CTRL_CIRCLE){state=save_return;movie_save_prompt=0;if(state==12)book_child_done=1;gframe=0;pending_checked=0;}}
            gfx_end();if(routetest && route_tick==60)dump_shot("shot_route_save.raw");frames++;continue;
        }
        if(state==12 || state==13 || state==14){
            if(!book_ok)book_ok=chars_load(&book_bank,"record_book.jgc")==0;
            if(!font_real_wide_ready()){
                int e=font_real_load_wide("fontwide.bin");
                if(!e)book_wide_owned=1;
            }
            unsigned dc=(pressed&PSP_CTRL_CROSS ? 4:0)|(pressed&PSP_CTRL_CIRCLE ? 2:0)|
                (pressed&PSP_CTRL_START ? 8:0)|(pressed&PSP_CTRL_UP ? 16:0)|(pressed&PSP_CTRL_DOWN ? 32:0)|
                (pressed&PSP_CTRL_LEFT ? 64:0)|(pressed&PSP_CTRL_RIGHT ? 128:0);
            int ax=state==14 ? (pad.Lx<65 ? -2:pad.Lx>191 ? 2:0):
                (pad.Ly<65 ? -1:pad.Ly>191 ? 1:0);
            if(ax && ax!=book_axis)dc|=ax==-2 ? 64:ax==2 ? 128:ax<0 ? 16:32;
            book_axis=ax;
            if(routetest==24){
                if(state==12 && book.phase==2){if(route_action<0){dc=book.cursor==2 ? 4:32;}else if(logfd>=0){char b[96];int n=snprintf(b,sizeof b,"ROUTE_DONE options sound=%d rumble=%d writable=%d\n",story[0x35c],story[0x35d],campaign_writable);runtime_log_write(logfd,b,n);}}
                if(state==14 && options.phase==2){if(route_action<0){dc=128;route_action=route_tick;}
                    else if(route_tick-route_action==20)dump_shot("shot_route_options.raw");
                    else if(route_tick-route_action>=40)dc=2;}
            }
            if(routetest==20 || routetest==21){
                if(state==12 && book.phase==2){if(book.cursor==0)dc=32;else dc=4;}
                if(routetest==21 && state==13 && records.phase==1 && route_action<0){dc=8;route_action=0;}
                if(routetest==20 && state==13 && records.phase==1 && records.cursor<19 && route_tick%8==0)dc=32;
                if(routetest==20 && state==13 && records.cursor==19 && records.phase==1 && route_tick%8==0){
                    dc=2;dump_shot("shot_route_record_last.raw");}
            }
            if(routetest==23 && state==12 && book.phase==2){
                if(route_action<0){dc=4;route_action=0;}
                else if(logfd>=0){runtime_log_write(logfd,"ROUTE_DONE book_save_return\n",sizeof("ROUTE_DONE book_save_return\n")-1);}
            }
            book.edges|=dc;book.remainder+=dt;
            if(book_ok)while(book.remainder>=1.f/30.f){book.remainder-=1.f/30.f;
                if(state==13){
                    /* The original parent keeps updating while task 220
                       is alive. START closes the whole book, not a page. */
                    int parent=record_book_step(&book,book.edges,0);
                    if(book.sound>=0)audio_menu_sound(book.sound);
                    if(parent==4){state=3;gframe=0;book.edges=0;
                        if(routetest==21 && logfd>=0)runtime_log_write(logfd,"ROUTE_DONE book_start_exit\n",sizeof("ROUTE_DONE book_start_exit\n")-1);
                        break;}
                    if(record_browser_step(&records,book.edges,story[0x33e]==1 || story[8] || story[9] || story[10] || story[11])){
                        book_child_done=1;state=12;
                        if(routetest==20 && logfd>=0)runtime_log_write(logfd,"ROUTE_DONE record_book\n",sizeof("ROUTE_DONE record_book\n")-1);
                    } else record_browser_pose_step(&records);
                    if(records.sound>=0)audio_menu_sound(records.sound);
                }else if(state==12){int child=record_book_step(&book,book.edges,book_child_done);book_child_done=0;
                    if(book.sound>=0)audio_menu_sound(book.sound);
                    record_book_pose_step(&book);
                    if(child==1){save_prompt_init(&save_prompt);save_error=0;save_return=12;state=10;}
                    if(child==2){record_browser_init(&records);state=13;}
                    if(child==3){record_options_init(&options,story);state=14;}
                    if(child==4){state=3;gframe=0;}
                }else {
                    int parent=record_book_step(&book,book.edges,0);
                    if(book.sound>=0)audio_menu_sound(book.sound);
                    if(parent==4){record_options_close(&options,story);state=3;gframe=0;}
                    else {if(record_options_step(&options,book.edges,0,story)){book_child_done=1;state=12;}
                        else record_options_pose_step(&options);
                        audio_set_mono(options.sound_mode);}
                    if(options.sound>=0)audio_menu_sound(options.sound);
                }
                book.edges=0;
            }
            if(state!=12 && state!=13 && state!=14)continue;
            float proj[16],view[16];m_ident(view);view[11]=-44;
            /* Original 028af4 / 08f8f8 screen focal 450; 2D menu camera
               keeps the original 640x480 composition in the PSP viewport. */
            m_perspective(proj,2.f*atanf(240.f/450.f),640.f/480.f,1,1000);
            gfx_begin();gfx_clear(GCOLOR(0,0,0,255));gu_set(GU_PROJECTION,proj);gu_set(GU_VIEW,view);gfx_mode3d();
            if(book_ok){record_book_draw(&book,&book_bank);if(state==13)record_browser_draw(&records,&book_bank,story);if(state==14)record_options_draw(&options,&book_bank,0);}
            gfx_mode2d();
            if(!book_ok){font_real_draw_center(240,130,.8f,0xffffffff,"RECORD ASSETS MISSING - O: RETURN");if(pressed&PSP_CTRL_CIRCLE)state=3;}
            if(book.fade>0)gfx_quad(0,0,480,272,((unsigned)(book.fade*255))<<24);
            if(gdisp_ok)garage_cast_ui(&gdisp,-1);
            gfx_end();
            if(routetest==20 && state==13 && records.phase==1 && records.cursor==0)dump_shot("shot_route_record_first.raw");
            frames++;continue;
        }
        if(book_ok && state!=10){chars_free(&book_bank);book_ok=0;}
        if(book_wide_owned && state!=10){font_real_free_wide();book_wide_owned=0;}
        if (state == 3 && !garage_bgm) { garage_bgm = 1; if(bgm_current()!=1)bgm_play(1); audio_set_music(1); runtime_log("GARAGE_MUSIC entry=%d\n",bgm_current()); } /* 0213d4 -> 02e804: HUM_M2 in both garages, before a01. */
        if(state!=3 && state!=16)chapter_checked=0;
        if(state==3 && !chapter_checked) {
            chapter_checked=1;
            int chapter=story_chapter_card_take();
            if(chapter>=0){chapter_card_begin(&chapter_card,chapter);state=16;runtime_log("CHAPTER_CARD begin=%d\n",chapter+1);}
        }
        if(state==16) { /* Original a01 precedes the garage carousel. */
            if(!font_real_wide_ready() && !font_real_load_wide("fontwide.bin"))chapter_wide_owned=1;
            if(font_real_wide_ready()) {
                chapter_card.remainder+=garagetest ? 1.f/60.f:dt;
                while(chapter_card.remainder>=1.f/30.f && !chapter_card.done){chapter_card.remainder-=1.f/30.f;chapter_card_step(&chapter_card);}
            }
            gfx_begin();gfx_clear(0xff000000);gfx_mode2d();
            const char *text=garage_ui_chapter(chapter_card.chapter);
            if(font_real_wide_ready()) {
                font_real_draw_xy((320-font_real_source_width(text)/2)*.75f,228.f*(272.f/480.f),.75f,272.f/480.f,0xffffffff,text);
                gfx_quad(0,0,480,272,GCOLOR(0,0,0,(int)(chapter_card.alpha*255.f)));
            }else font_real_draw_center(240,125,.75f,0xffffffff,"CHAPTER FONT UNAVAILABLE");
            gfx_end();frames++;
            if((garagetest==7 || rivalroutetest==4) && chapter_card.hold==30)dump_shot("shot_chapter.raw");
            if(chapter_card.done){
                runtime_log("CHAPTER_CARD done=%d\n",chapter_card.chapter+1);
                if(chapter_wide_owned){font_real_free_wide();chapter_wide_owned=0;}
                state=3;gframe=0;
            }
            continue;
        }
        if (state == 1 || state == 0) garage_bgm = 0;
        if (state == 3 || state == 4 || state == 5 || state==11) { if (!garage_ok) garage_ok = garage_cast_assets_load(&garage) == 0; if (!gmark_ok) gmark_ok = chars_load(&gmark, "garage_mark.jgc") == 0; if (!gdisp_ok) gdisp_ok = tex2d_load(&gdisp, "garage_disp.bin") == 0; }
        if(state!=11 && pending_ok){chars_free(&pending_cast);pending_ok=0;}
        if(state!=4 && street_rival_model.base){chars_free(&street_rival_model);street_rival_active=0;}
        if(state==11) { /* task 22a invitation; state 9 belongs to graffiti */
            if(!pending_ok)pending_ok=chars_load(&pending_cast,"garage_pending.jgc")==0;
            unsigned dc=(pressed&PSP_CTRL_CROSS ? 4:0)|(pressed&PSP_CTRL_CIRCLE ? 2:0)|
                (pressed&PSP_CTRL_LEFT ? 0x40:0)|(pressed&PSP_CTRL_RIGHT ? 0x80:0);
            if(routetest && pending.clock!=route_action){route_action=pending.clock;
                if(pending.clock==60)dc=4;
                if(routetest==5 || routetest==9){if(pending.clock==65)dc=2;if(pending.clock==70)dc=4;
                    if(pending.clock==75)dc=0x80;if(pending.clock==80)dc=4;}}
            int ax=pad.Lx<65 ? -1:pad.Lx>191 ? 1:0;
            if(ax && ax!=pending_axis)dc|=ax<0 ? 0x40:0x80;
            pending_axis=ax;pending.edges|=dc;pending.remainder+=dt;
            int ready=0;
            if(pending_ok && gdisp_ok && garage_ok)while(pending.remainder>=1.f/30.f) {
                pending.remainder-=1.f/30.f;
                ready=garage_pending_step(&pending,pending.edges);pending.edges=0;
                if(pending.sound>=0)audio_menu_sound(pending.sound);
                if(pending.voice>=0){int error=audio_garage_voice(pending.voice);
                    if(logfd>=0){char b[96];int n=snprintf(b,sizeof b,"MENU_VOICE selector=%d program=%d error=%d\n",pending.selector,pending.voice,error);runtime_log_write(logfd,b,n);}}
                garage_pending_pose_step(&pending);
                if(ready)break;
            }
            if(ready) {
                audio_garage_voice(-1);
                GaragePendingRoute route=garage_pending_route(&pending);
                printf("GARAGE invitation selector=%d declined=%d mode=%d stage=%d chapter=%d character=%d\n",pending.selector,pending.declined,route.mode,route.stage,route.chapter,route.character);
                if(routetest && logfd>=0){char b[128];int n=snprintf(b,sizeof b,"ROUTE_INVITE selector=%d declined=%d mode=%d stage=%d chapter=%d character=%d\n",pending.selector,pending.declined,route.mode,route.stage,route.chapter,route.character);runtime_log_write(logfd,b,n);
                    if(route.mode==1 && routetest!=9)runtime_log_write(logfd,"ROUTE_DONE declined\n",sizeof("ROUTE_DONE declined\n")-1);}
                if(route.mode==1){state=3;gframe=0;gcursor=2;continue;}
                const char *nm=story_file_name(story_chapter_file(route.stage,route.chapter));int rec=-1;
                for(int i=0;nm && i<nmis;i++)if(mis_head[i] && !strcmp(missions[i].name,nm)){rec=i;break;}
                if(rec>=0) {
                    bgm_stage_idx=route.stage;bgm_slot=route.chapter;crec=rec;cspecial=0;
                    if(route.mode==4){cpending=1;csel=0;cframe=0;state=5;continue;}
                    chars_free(&pending_cast);pending_ok=0;
                    if(garage_ok){garage_cast_assets_free(&garage);garage_ok=0;}
                    if(gmark_ok){chars_free(&gmark);gmark_ok=0;}
                    if(gdisp_ok){tex2d_free(&gdisp);gdisp_ok=0;}
                    pchar=MODEL_OF_CHAR[route.character];
                    level_load(&L,STAGE_OF_INDEX[missions[rec].st],&missions[rec]);mcur=rec;state=1;continue;
                }
                printf("GARAGE invitation mission unavailable\n");
            }
            float pe[3],pt[3],fov;int roll;garage_pending_view(&pending,pe,pt,&fov,&roll);
            float fwd[3]={pt[0]-pe[0],pt[1]-pe[1],pt[2]-pe[2]},proj[16],view[16];
            player_camera_matrix(pe,fwd,roll,view);
            float focal=player_camera_focal((int)lroundf(fov*(65536.f/6.283185307f)),640,1);
            m_perspective(proj,2.f*atanf(240.f/focal),640.f/480.f,1,3000);
            gfx_begin_room();gfx_clear(GCOLOR(0,0,0,255));gu_set(GU_PROJECTION,proj);gu_set(GU_VIEW,view);gfx_mode3d();
            float room_clip[16],room_planes[6][4];m_mul(room_clip,proj,view);frustum_planes(room_clip,room_planes);
            chars_environment_view(view);garage_light_begin();
            if(garage_ok)garage_room_draw_pass(&garage,0,0,room_planes);
            if(pending_ok)garage_pending_draw(&pending,&pending_cast);
            if(garage_ok)garage_room_draw_pass(&garage,0,1,room_planes);
            garage_light_end();
            gfx_mode2d();if(gdisp_ok)garage_pending_ui(&pending,&gdisp);
            if(!pending_ok || !gdisp_ok || !garage_ok){font_real_draw_center(240,130,1,0xffffffff,"INVITATION ASSETS MISSING - O: RETURN");
                if(pressed&PSP_CTRL_CIRCLE){state=3;gframe=0;}}
            gfx_end();
            if(routetest && pending.clock==40)dump_shot("shot_route_invite.raw");
            if(routetest && pending.clock==77)dump_shot("shot_route_choice.raw");
            frames++;continue;
        }
        if (state == 3) {   /* garage (task 0x222 / sub-task 0x21b main menu): room and original menu camera from garagedat, 7-item cursor (item -> mode table 0x8c10dc68) */
            int entry=garage_cast_menu_entry(story,gcursor);
            if(entry!=gcursor){gcursor=entry;garage_cast_active=0;}
            if (!garage_cast_active) { garage_cast_reset(&garage_cast);garage_cast_select(&garage_cast,story,gcursor,0);garage_cast_active=1;
                if(garagetest)runtime_log("GARAGE_MENU item=%d special=%d camera=%.0f\n",gcursor,garage_cast.special,garage_cast.camera);
            }
            garage_cast_update(&garage_cast,story,100,garagetest ? 1.f/60.f : dt);
            /* carousel (FUN_8c021a4c case 2): right (pad 0x80) = item+1 wrapping at 7, left (0x40) = item-1; in normal play items 1 and 3 are skipped
               (1 -> 2 and 3 -> 4 going right, 3 -> 2 and 1 -> 0 going left; the story-flag case 0x33e/0x32f/0x356 skips 2 instead) */
            if ((pressed & PSP_CTRL_RIGHT) && garage_cast.camera_mode==1) { gcursor=garage_cast_menu_next(story,gcursor,1);garage_cast_select(&garage_cast,story,gcursor,1); }
            if ((pressed & PSP_CTRL_LEFT) && garage_cast.camera_mode==1) { gcursor=garage_cast_menu_next(story,gcursor,-1);garage_cast_select(&garage_cast,story,gcursor,-1); }
            /* A: item -> mode table 0x8c10dc68 {6,9,2,5,7,11,8}; modes 2/3 select the Tokyo-to/Grind City street map */
            if (((pressed & PSP_CTRL_CROSS) && gcursor == 0) || (grftest && gframe == 150)) { gsub = 0; gsubframe = 0; if (!mlists_ok) mlists_ok = list2d_load(&mlists, "markedit_lists.bin") == 0; if (!mdisp_ok) mdisp_ok = tex2d_load(&mdisp, "markedit_disp.bin") == 0; state = 8; continue; }   /* GRAFFITI (mode 6, task 0x223 FUN_8c04a5b0) */
            if (((pressed & PSP_CTRL_CROSS) && gcursor == 4) || (nettest && gframe == 150)) { nframe = 0; state = 7; continue; }   /* INTERNET (mode 7, task 0x22b FUN_8c0e0b70): offline path (+0x90) shows the SegaNet notice */
            if((pressed&PSP_CTRL_CROSS) && gcursor==6){record_book_init(&book);book_child_done=book_axis=0;state=12;continue;}
            if (((pressed & PSP_CTRL_CROSS) && gcursor == 5) || (radiotest && gframe == 150)) {   /* RADIO (mode 11): 0fe744 starts a fresh selection at song zero. */
                if (!radio_ok) { radio_ok = chars_load(&radio, "radio.jgc") == 0; }
                if (!radio_names_n) { int len = 0; unsigned char *t = read_file("radio_names.txt", &len);  if (t) { int k = 0, li = 0; for (int i = 0; i < len && li < 52; i++) { if (t[i] == '\n') { radio_names[li][k] = 0; li++; k = 0; } else if (k < 47) radio_names[li][k++] = t[i]; } radio_names_n = li; free(t); } }
                int motion_frames=1;
                if(radio_ok && radio.h->n_chars>1 && radio.chars[1].nmotions)
                    motion_frames=radio.motions[radio.chars[1].motion0].nframes;
                radio_ui_init(&radio_ui,motion_frames);rframe=0;state=6;continue;
            }
            if(garagetest>=4 && gframe==20)pressed|=PSP_CTRL_CROSS;
            if((pressed&PSP_CTRL_CROSS) && gcursor==1) {
                /* 021490 mode 9 -> 0f5dc8. 0f5e0c chooses Bantam until
                   351 is clear, then Grind Square; both use chapter zero. */
                int st=story_special_stage();const char *nm=story_file_name(story_chapter_file(st,0));
                int rec=-1;for(int i=0;i<nmis;i++)if(mis_head[i] && !strncmp(missions[i].name,nm,5)){rec=i;break;}
                if(rec>=0){cspecial=1;csel=0;crec=rec;cframe=0;bgm_stage_idx=st;bgm_slot=0;state=5;continue;}
                printf("STORY special departure image unavailable stage=%d\n",st);
            }
            if (((pressed & PSP_CTRL_CROSS) && gcursor == 2) || (autopilot && gframe == 200)) { cspecial=0;unsigned char av[24];if(street_city)story_grind_entries(av);else story_entries(av); sentry = 0; for (int i = 0; i < (street_city ? 9:24); i++) if (av[i] == 0) { sentry = i; break; } sframe = 0; state = 4; continue; }   /* mode 2: STREET */
            if (pressed & PSP_CTRL_SELECT) { if (garage_ok) { garage_cast_assets_free(&garage); garage_ok = 0; } if (gmark_ok) { chars_free(&gmark); gmark_ok = 0; } if (gdisp_ok) { tex2d_free(&gdisp); gdisp_ok = 0; } state = 0; continue; }   /* debug: stand-in level menu */
            float proj[16], view[16], eye[3] = { 23.2f, 28.1f, 19.4f }, tgt[3] = { 33.8f, 25.2f, -5.5f };
            float camera_fov;garage_cast_view(&garage_cast,eye,tgt,&camera_fov);
            float fwd[3] = { tgt[0] - eye[0], tgt[1] - eye[1], tgt[2] - eye[2] }; { float l = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]); if (l > 0) { fwd[0] /= l; fwd[1] /= l; fwd[2] /= l; } }
            /* Ninja's camera FOV spans the original horizontal 640 pixels. */
            float garage_focal=player_camera_focal((int)lroundf(camera_fov*(65536.f/6.283185307f)),640,1);
            m_perspective(proj,2.f*atanf(240.f/garage_focal),640.f/480.f,1,3000);
            player_camera_matrix(eye,fwd,0,view);
            gfx_begin_room(); gfx_clear(GCOLOR(0, 0, 0, 255)); gu_set(GU_PROJECTION, proj); gu_set(GU_VIEW, view); gfx_mode3d();
            float room_clip[16],room_planes[6][4];m_mul(room_clip,proj,view);frustum_planes(room_clip,room_planes);
            chars_environment_view(view);garage_cast_frustum(room_planes);garage_light_begin();
            gfx_profile_section(GFX_STAGE);
            if (garage_ok) garage_room_draw_pass(&garage,garage_cast.room_frame,0,room_planes);
            gfx_profile_section(GFX_EFFECTS);
            if(garage_ok)garage_cast_shadows(&garage_cast,story,-1,&garage,view);
            gfx_profile_section(GFX_ACTORS);
            if (garage_ok) garage_cast_draw(&garage_cast,story,&garage);
            if (garage_ok) garage_pet_draw(&garage_cast,story,&garage);
            /* PVR modifier volumes affect the opaque receiver list, not
               a fence already blended over that list. Room alpha is last. */
            gfx_profile_section(GFX_STAGE);
            if (garage_ok) garage_room_draw_pass(&garage,garage_cast.room_frame,1,room_planes);
            /* The photo is alpha geometry too: original 0221b8 submits it
               after the room, so wall graffiti cannot blend over its face. */
            if(garage_ok && story[0x33e]==1 && story[0x32f]==1)garage_special_prop_draw(&garage);
            garage_light_end();garage_cast_frustum(NULL);
            gfx_profile_section(GFX_HUD);gfx_mode2d();
            if(gdisp_ok){garage_cast_ui(&gdisp,-1);garage_ui_menu(&gdisp,gcursor);}
            {int id=story_notification_take();if(id>=0)hud_notice_acquired(&garage_notice,id);}
            notice_acc+=dt;while(notice_acc>=1.f/30.f){hud_notice_present(&garage_notice,0,1);notice_acc-=1.f/30.f;}
            if(garage_notice.visible && !font_real_wide_ready()){
                if(!font_real_load_wide("fontwide.bin"))notice_wide_owned=1;
            }
            hud_draw_notice(&garage_notice);
            gfx_end();
            if((garagetest==4 || garagetest==5) && gframe==10)dump_shot("shot_garage_special_menu.raw");
            if(exit_fixture_destination==3 && gframe>=10) {
                dump_shot("shot_exit_destination.raw");exit_fixture_destination=0;
                runtime_log("EXIT_FIXTURE destination=garage rendered=1\n");
                runtime_log_safe_checkpoint("exit_fixture_destination");
            }
            if (autopilot && garage_notice.visible && garage_notice.draw_clock==0) dump_shot("shot_notify.raw"); frames++; gframe++;
            if(!garage_notice.visible && notice_wide_owned){font_real_free_wide();notice_wide_owned=0;}
            if(movie_route_test && movie_route_test!=2 && endingtest>=100 && gframe==2) {
                char shot[64];snprintf(shot,sizeof shot,"shot_movie_garage_%d.raw",endingtest-100);dump_shot(shot);
                runtime_log("MOVIE_GARAGE id=%d room=%d scene_resident=%d wide_font=%d writable=%d heap=%u\n",endingtest-100,garage_ok,sceneplay_resident(&L.scene),font_real_wide_ready(),campaign_writable,(unsigned)mallinfo().fordblks);
                runtime_log_flush();
                /* Read-only memory-lifetime fixture: follow the complete
                   movie chain, then exercise ordinary character departure
                   into the supplied first mission. No earned flags/save. */
                if(movie_route_test==4 && garage_ok){garagetest=6;state=5;cframe=0;}
                if(movie_route_test==3 && garage_ok) {
                    if(endingtest==134){runtime_log("MOVIE_SWEEP_DONE count=35 writable=0\n");runtime_log_flush();movie_route_test=0;}
                    else {
                        endingtest++;campaign_movie=1;campaign_movie_return=3;movie_skip_sent=0;
                        endingplay_begin_movie(&ending,endingtest-100);ending_previous=-1;ending_save_error=0;state=15;gframe=0;continue;
                    }
                }
            }
            if(rivalroutetest==4 && gframe==2) {
                runtime_log("RIVAL_MOVIE_GARAGE room=%d scene_resident=%d writable=%d\n",garage_ok,sceneplay_resident(&L.scene),campaign_writable);
                dump_shot("shot_rival_movie_garage.raw");runtime_log_flush();
            }
            if(recruit_garage_pending && gframe>=2) {
                dump_shot("shot_recruit_garage.raw");
                if(logfd>=0){char line[160];int n=snprintf(line,sizeof line,"RECRUIT_GARAGE scene=%d room=%d scene_resident=%d wide_font=%d frame=%d\n",recruit_garage_pending,garage_ok,sceneplay_resident(&L.scene),font_real_wide_ready(),frames);runtime_log_write(logfd,line,n);}
                recruit_garage_pending=0;
            }
            if ((autopilot || garagetest) && gframe == 120) dump_shot("shot_garage.raw");
            if (garagetest && gframe == 300) dump_shot("shot_garage_dance.raw");
            if (garagetest && gframe == 360) break;
            continue;
        }
        if (state == 9) {   /* GRAFFITI gallery (task 0x229: FUN_8c09e050 page select / FUN_8c09e15c grid, draw FUN_8c09eb14) */
            extern unsigned char story[0x400];
            int *cur = &gcur[gpage]; int total = 34;   /* built-in slots 0..0x21 per page; VMU custom designs (+0x8c/+0x100/+0x10c) do not exist on the PSP */
            if (grftest && gsubframe == 30) for (int i = 0; i < 10; i++) story[0x37a + i] = 1;   /* debug: unlock the first row (the DC save init sets the real defaults) */
            if (gst == 0) {   /* FUN_8c09e050: up/down (pad 0x10/0x20) pick the size page 0 S / 1 L / 2 XL; A -> grid; B -> top menu; Start -> garage */
                if (!(pressed & PSP_CTRL_UP) && (pressed & PSP_CTRL_DOWN) && gpage < 2) gpage++;
                else if ((pressed & PSP_CTRL_UP) && gpage > 0) gpage--;
                if (pressed & PSP_CTRL_CROSS) gst = 1;
                else if (pressed & PSP_CTRL_CIRCLE) { state = 8; continue; }
                else if (pressed & PSP_CTRL_START) { state = 3; continue; }
                if (grftest && gsubframe == 60) gst = 1;
            } else {   /* FUN_8c09e15c: left/right +-1, up/down +-10 within the page; A selects an unlocked design (save byte 0x3e5+page); B -> page select; Start -> garage */
                if (pressed & PSP_CTRL_LEFT) { if (*cur > 0) (*cur)--; }
                else if (pressed & PSP_CTRL_RIGHT) { if (*cur < total - 1) (*cur)++; }
                else if (pressed & PSP_CTRL_UP) { if (*cur >= 10) *cur -= 10; }
                else if (pressed & PSP_CTRL_DOWN) { if (*cur + 10 < total) *cur += 10; }
                else if (pressed & PSP_CTRL_CROSS) { if (story[0x37a + gpage * 0x22 + *cur] == 1) story[0x3e5 + gpage] = (unsigned char)*cur; }   /* FUN_8c09e718 + the store at 0x8c09e2xx */
                else if (pressed & PSP_CTRL_CIRCLE) gst = 0;
                else if (pressed & PSP_CTRL_START) { state = 3; continue; }
                if (grftest && gsubframe == 90) *cur = 2;
            }
            int sel = gpage * 34 + *cur;
            if (grf_loaded != sel) {gfx_wait_idle();   /* FUN_8c095798(kind, idx): U_GRF kind 2/1/0 = grfs/grfm/grfl (128x128 / 256x128 / 512x128 ARGB4444). */
                char fn[32]; snprintf(fn, sizeof fn, "grf/%c%02d.bin", "sml"[gpage], *cur); FILE *f = fopen(fn, "rb");
                if (f) { fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET); if (!grf_px) grf_px = memalign(16, 512 * 128 * 2);
                    grf_w=128<<gpage;grf_h=128;
                    if(grf_px && len==grf_w*grf_h*2 && fread(grf_px,1,len,f)==(unsigned)len) {
                        /* Same 095380 ARGB4444 upload as the wall, not RGB565. */
                        for(int j=0;j<grf_w*grf_h;j++){unsigned c=grf_px[j];grf_px[j]=(c&0xf0f0)|((c>>8)&15)|((c&15)<<8);}
                        sceKernelDcacheWritebackAll();grf_loaded=sel;
                    }else grf_loaded=-2;
                    fclose(f); }
                else grf_loaded = -2;
            }
            gfx_begin(); gfx_clear(GCOLOR(0, 0, 0, 255)); gfx_mode2d();
            if (mlists_ok && mdisp_ok) {
                list2d_draw(&mlists, 0x104 / 4, &mdisp, 0.f, 0.f);                       /* z -10 background */
                list2d_draw(&mlists, 0x114 / 4, &mdisp, 0.f, 0.f);                       /* -9 the 3 size tabs */
                list2d_draw(&mlists, 0x118 / 4, &mdisp, 0.f, 37.f * (float)gpage);       /* -8 green tab at page*37 */
                list2d_draw(&mlists, 0x130 / 4, &mdisp, 0.f, 0.f);                       /* -7 labels */
                list2d_draw(&mlists, (0x108 + 4 * gpage) / 4, &mdisp, 0.f, 0.f);         /* -5 page panel */
                for (int i = 0; i < 34; i++) if (story[0x37a + gpage * 0x22 + i]) list2d_draw(&mlists, 0x12c / 4, &mdisp, 26.f * (i % 10), 26.f * (i / 10));   /* -4 unlocked boxes, 10 per row, 26 px pitch */
                if (gst == 1) list2d_draw(&mlists, 0x120 / 4, &mdisp, 26.f * (*cur % 10), 26.f * (*cur / 10));   /* cursor box (yellow) */
                if (story[0x37a + gpage * 0x22 + *cur] && grf_txt) {   /* FUN_8c0c0d54: name centred at DC x 0x1d0, y 0x30; description lines at y 0x4c / 0x4c+.. in black */
                    font_real_draw_center(464.f * 0.75f, 48.f * (272.f / 480.f), 0.75f, 0xff000000u, grf_names[sel]);
                    font_real_draw_center(464.f * 0.75f, 76.f * (272.f / 480.f), 0.75f, 0xff000000u, grf_desc[sel * 2]);
                    font_real_draw_center(464.f * 0.75f, 96.f * (272.f / 480.f), 0.75f, 0xff000000u, grf_desc[sel * 2 + 1]);
                }
                if (grf_loaded == sel && grf_px && story[0x37a + gpage * 0x22 + *cur]) {   /* preview: the DC draws the art on a 3D wall (FUN_8c02f394, stick-rotated); flat quad here, placement approximated */
                    float w = grf_w == 512 ? 270.f : grf_w == 256 ? 220.f : 130.f, h = w * grf_h / grf_w, x = 465.f - w / 2, y = 318.f - h / 2;
                    const float SX = 480.f / 640.f, SY = 272.f / 480.f; typedef struct { float u, v; unsigned color; float x, y, z; } GV; GV *v = (GV *)sceGuGetMemory(2 * sizeof(GV));
                    v[0].u = 0; v[0].v = 0; v[0].color = 0xffffffffu; v[0].x = x * SX; v[0].y = y * SY; v[0].z = 0;
                    v[1].u = grf_w; v[1].v = grf_h; v[1].color = 0xffffffffu; v[1].x = (x + w) * SX; v[1].y = (y + h) * SY; v[1].z = 0;
                    sceGuEnable(GU_TEXTURE_2D); sceGuTexMode(GU_PSM_4444, 0, 0, 0); sceGuTexImage(0, grf_w, grf_h, grf_w, grf_px); sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA); sceGuTexFilter(GU_LINEAR, GU_LINEAR);
                    ge_submit_aux(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v); sceGuDisable(GU_TEXTURE_2D);
                }
            }
            gfx_end(); frames++; gsubframe++; if (grftest && gsubframe == 200) dump_shot("shot_gallery.raw"); if (grftest && gsubframe == 260) { state = 3; } continue;
        }
        if (state == 8) {   /* GRAFFITI top menu (task 0x223 mode 0: FUN_8c04b08c input, FUN_8c04b2c4 draw): 2 items, right/left (pad 0x80/0x40) move, A = 0 gallery / 1 editor, B or Start = back (FUN_8c04a5f4(7)) */
            if (!(pressed & PSP_CTRL_LEFT) && (pressed & PSP_CTRL_RIGHT) && gsub < 1) gsub++;
            else if ((pressed & PSP_CTRL_LEFT) && gsub > 0) gsub--;
            if (!(pressed & PSP_CTRL_CROSS) && (pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_START))) { state = 3; continue; }
            if (((pressed & PSP_CTRL_CROSS) && gsub == 0) || (grftest && gsubframe == 260)) {   /* FUN_8c04aad8 -> mode 1: gallery task 0x229 (FUN_8c09dd64 init: cursors from save bytes 0x3e5..0x3e7, VMU scan skipped on the PSP) */
                extern unsigned char story[0x400]; gpage = 0; gst = 0; for (int i = 0; i < 3; i++) gcur[i] = story[0x3e5 + i] < 34 ? story[0x3e5 + i] : 0;
                if (!grf_txt) { FILE *f = fopen("grf_names.txt", "r"); if (f) { for (int i = 0; i < 102 && fgets(grf_names[i], 24, f); i++) grf_names[i][strcspn(grf_names[i], "\r\n")] = 0; fclose(f); }
                    f = fopen("grf_desc.txt", "r"); if (f) { for (int i = 0; i < 204 && fgets(grf_desc[i], 24, f); i++) grf_desc[i][strcspn(grf_desc[i], "\r\n")] = 0; fclose(f); } grf_txt = 1; }
                gsubframe = 0; state = 9; continue; }
            /* A on item 1: editor (FUN_8c04aae4 -> pixel editor FUN_8c08e930) — not ported yet */
            gfx_begin(); gfx_clear(GCOLOR(0, 0, 0, 255)); gfx_mode2d();
            if (mlists_ok && mdisp_ok) {   /* markeditdat lists (bank @0x8c900000 words 0x59..0x5e) over texlist 0x8c1b4a88 = MARKEDIT#0 edittxr (6 x 512^2) while this screen is up, back-to-front by the DC z: 0x164 -10, 0x168 -9, highlight -8, 0x174 -7, 0x178 -5 */
                list2d_draw(&mlists, 0x164 / 4, &mdisp, 0.f, 0.f);
                list2d_draw(&mlists, 0x168 / 4, &mdisp, 0.f, 0.f);
                list2d_draw(&mlists, gsub == 0 ? 0x16c / 4 : 0x170 / 4, &mdisp, 0.f, 0.f);
                list2d_draw(&mlists, 0x174 / 4, &mdisp, 0.f, 0.f);
                list2d_draw(&mlists, 0x178 / 4, &mdisp, 0.f, 0.f);
            }
            gfx_end(); frames++; gsubframe++; if (grftest && gsubframe == 200) dump_shot("shot_grf.raw"); continue;
        }
        if (state == 7) {   /* INTERNET screen offline (FUN_8c0e123c: FUN_8c0b09b4 draws the bank texture 0x8c8004e8 = the 640x480 SegaNet notice); B / Start leave (FUN_8c0211b4(1)) */
            if ((pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_START)) || (nettest && nframe == 300)) { state = 3; continue; }
            gfx_begin(); gfx_clear(GCOLOR(0, 0, 0, 255)); gfx_mode2d();
            if (gdisp_ok) { int slot = -1; for (int i = 0; i < gdisp.n; i++) if (gdisp.t[i].w >= 512 && gdisp.t[i].h >= 480 && i != 0 && i != 3) slot = i; if (slot < 0) slot = gdisp.n - 1;
                tex2d_draw_entry(&gdisp.t[slot], 0.f, 0.f, 640.f, 480.f, 0.f, 0.f, 0xffffffffu, 0.f, 0.f, 640.f / 1024.f, 480.f / 1024.f); }   /* disp slot 5 = the 1024x1024 atlas holding the 640x480 notice */
            gfx_end(); frames++; nframe++; if (nettest && nframe == 120) dump_shot("shot_net.raw"); continue;
        }
        if (state == 6) {   /* RADIO: task 0fe75c, projection and captions 0fe98c. */
            if(radiotest && radio_ui.phase==RADIO_IDLE && rframe>=120){
                char shot[48];snprintf(shot,sizeof shot,"shot_radio_%d.raw",radio_test_action);dump_shot(shot);
                runtime_log("RADIO_IDLE step=%d track=%d stream=%d frame=%d models=%d\n",radio_test_action,radio_ui.track,bgm_current(),radio_ui.frame,radio_ok);
                pressed=radio_test_action==0 ? PSP_CTRL_LEFT:radio_test_action<3 ? PSP_CTRL_RIGHT:PSP_CTRL_START;
                radio_test_action++;rframe=0;
            }
            int song=radio_ui_input(&radio_ui,pressed&PSP_CTRL_LEFT,pressed&PSP_CTRL_RIGHT,
                                    pressed&(PSP_CTRL_CIRCLE|PSP_CTRL_START));
            if(song>=0)bgm_play(song);
            song=radio_ui_update(&radio_ui,radiotest ? 1.f/60.f:dt);
            if(song>=0)bgm_play(song);
            if(radio_ui.phase==RADIO_DONE){state=3;continue;}
            gfx_begin();gfx_clear(GCOLOR(0,0,0,255));
            float proj[16],view[16],root[16];
            m_perspective(proj,2.f*atanf(240.f/450.f),640.f/480.f,1.f,1000.f);
            gu_set(GU_PROJECTION,proj);m_ident(view);gu_set(GU_VIEW,view);
            m_ident(root);root[11]=-45.f;gfx_mode3d();chars_world_frustum(NULL);
            if(radio_ok){
                chars_draw_material(&radio,0,root,NULL,-1,0,1,NULL);
                chars_draw_material(&radio,1,root,NULL,0,(float)radio_ui.frame,1,NULL);
            }
            gfx_mode2d();
            if(radio_ui.phase==RADIO_IDLE && radio_names_n>=52){
                font_real_draw_xy(130.f*.75f,135.f*(272.f/480.f),.75f,272.f/480.f,0xffffffff,radio_names[radio_ui.track]);
                font_real_draw_xy(130.f*.75f,165.f*(272.f/480.f),.75f,272.f/480.f,0xffffffff,radio_names[26+radio_ui.track]);
            }
            if(radio_ui.fade)gfx_quad(0,0,480,272,(unsigned)(radio_ui.fade*255/25)<<24);
            gfx_end();frames++;rframe++;
            if(radiotest && radio_test_action==1 && rframe==60)dump_shot("shot_radio_turn.raw");
            continue;
        }
        if (state == 4) { /* Street maps: 022a8c (Tokyo-to) / 0f6d48 (Grind City). */
            if(street_city) {
                if(!grind_map_ok && !grind_map_load(&grind_map,"grindmap.bin")) {
                    grind_map_ok=1;sceKernelDcacheWritebackAll();
                }
                if(!grind_exit.base)chars_load(&grind_exit,"grind_exit.jgc");
                if(!grind_map_ok || !grind_exit.base) {
                    gfx_begin();gfx_clear(0xff000000);gfx_mode2d();
                    font_real_draw_center(240,118,.8f,0xffffffff,"GRIND CITY DATA MISSING - PREPARE GAME DATA");
                    font_real_draw_center(240,142,.8f,0xffffffff,"O: RETURN");gfx_end();
                    if(pressed&PSP_CTRL_CIRCLE){street_city=0;sentry=23;sframe=0;}
                    frames++;continue;
                }
            }
            /* 022a8c copies parent +88. The pending path renders the
               original map/banner and both fades before task 22a, once
               per garage visit. Declining does not consume membership. */
            if(!sframe) {
                street_rival_active=!street_city && !pending_checked && garage_pending_selector(story)>=0;
                if(street_rival_active){garage_rival_init(&street_rival);runtime_log("STREET_RIVAL begin selector=%d\n",garage_pending_selector(story));}
                pending_checked=1;
            }
            if(street_rival_active) {
                if(!street_rival_model.base && chars_load(&street_rival_model,"garage_rival.jgc")) {
                    gfx_begin();gfx_clear(0xff000000);gfx_mode2d();
                    font_real_draw_center(240,130,.8f,0xffffffff,"RIVAL MAP ASSET MISSING - O: RETURN");gfx_end();
                    if(pressed&PSP_CTRL_CIRCLE){pending_checked=0;state=3;gframe=0;}
                    frames++;continue;
                }
                street_rival.remainder+=dt;
                while(street_rival.remainder>=1.f/30.f && !street_rival.done) {
                    street_rival.remainder-=1.f/30.f;garage_rival_step(&street_rival);
                    if(street_rival.sound>=0){int error=audio_menu_sound(street_rival.sound);runtime_log("STREET_RIVAL sound=%d error=%d\n",street_rival.sound,error);}
                }
                if(street_rival.done){runtime_log("STREET_RIVAL done clock=%d\n",street_rival.clock);garage_pending_init(&pending,story);pending_axis=0;state=11;continue;}
            }
            cpending=0;
            unsigned char av[24];int entries=street_city ? 9:24;
            if(street_city)story_grind_entries(av);else story_entries(av);
            if((unsigned)sentry>=(unsigned)entries || av[sentry]) {
                sentry=0;for(int i=0;i<entries;i++)if(!av[i]){sentry=i;break;}
            }
            if(!sframe)memset(street_marker_frame,0,sizeof street_marker_frame);sframe++;
            int dirs[4] = { PSP_CTRL_RIGHT, PSP_CTRL_LEFT, PSP_CTRL_UP, PSP_CTRL_DOWN }; float dang[4] = { 0.f, 3.14159265f, -1.5707963f, 1.5707963f };
            for (int d = 0; d < 4; d++) if (!street_rival_active && (pressed & dirs[d])) {
                int best = -1; float bestd = 1e9f;
                for (int i = 0; i < entries; i++) { if (i == sentry || av[i]) continue;
                    float dx=street_city ? grind_map.entry[i].x-grind_map.entry[sentry].x:MARK[i][0]-MARK[sentry][0];
                    float dy=street_city ? grind_map.entry[i].y-grind_map.entry[sentry].y:MARK[i][1]-MARK[sentry][1]; float ang = atan2f(dy, dx), da = ang - dang[d]; while (da > 3.14159265f) da -= 6.2831853f; while (da < -3.14159265f) da += 6.2831853f;
                    if (fabsf(da) <= 1.1781f) { float dist = sqrtf(dx * dx + dy * dy) * (1.f + fabsf(da)); if (dist < bestd) { bestd = dist; best = i; } } }   /* 67.5 deg sector (0x8c023288) */
                if (best >= 0) sentry = best;
            }
            if (!street_rival_active && (pressed & PSP_CTRL_CIRCLE)) { state = 3; continue; }   /* B: back to the garage carousel */
            if (!street_rival_active && !av[sentry] && ((pressed & PSP_CTRL_CROSS) || ((autopilot || garagetest==7) && sframe==120))) {
                /* Both map exits are transitions, not mission records. */
                if(sentry==(street_city ? 8:23)) {
                    street_city=!street_city;sentry=0;sframe=0;continue;
                }
                int ch,sp,st=street_city ? story_grind_entry_stage(sentry,&ch,&sp):story_entry_stage(sentry,&ch,&sp);
                int file=story_chapter_file(st,ch);const char *nm=story_file_name(file);int rec=-1;
                if(nm)for(int i=0;i<nmis;i++)if(mis_head[i] && !strncmp(missions[i].name,nm,5)){rec=i;break;}
                if(rec>=0){bgm_stage_idx=st;bgm_slot=ch;cspecial=0;crec=rec;cframe=0;state=5;continue;}
            }
            gfx_begin(); gfx_clear(GCOLOR(64, 64, 64, 255)); gfx_mode2d();
            if (gdisp_ok) tex2d_draw_entry(&gdisp.t[street_city ? 4:0], 0.f, 0.f, 640.f, 480.f, 0.f, 0.f, 0xffffffffu, 0.f, 0.f, 640.f / 1024.f, 480.f / 1024.f);
            if (gmark_ok) {   /* 02381c: focal=450, centered perspective camera; original selected/unselected transforms. */
                /* Preserve source XY perspective; give the overlapping GG
                   letters/rings enough precision in the PSP's 16-bit Z. */
                float proj[16], view[16];m_perspective(proj,2.f*atanf(240.f/450.f),640.f/480.f,GARAGE_MARKER_NEAR,GARAGE_MARKER_FAR);m_ident(view);
                gu_set(GU_PROJECTION, proj); gu_set(GU_VIEW, view); gfx_mode3d(); sceGuClear(GU_DEPTH_BUFFER_BIT);
                for(int i=0;i<entries;i++) {
                    if(av[i])continue;
                    float root[16];int selected=!street_rival_active && i==sentry;
                    Chars *bank=&gmark;int model=MARK_MODEL[i];
                    if(street_city) {
                        garage_ui_marker_at(grind_map.entry[i].x,grind_map.entry[i].y,selected,root);
                        model=i<2 ? 4:5+(i-2)%3;
                        if(i==8){bank=&grind_exit;model=0;}
                    }else garage_ui_marker(i,selected,root);
                    if(selected)chars_draw_one_xf(bank,model,root,0,street_marker_frame[i]);
                    else chars_draw_one_rest(bank,model,root);
                }
                /* 023a22/0bdf30 advances only the selected marker at 30 Hz. */
                street_marker_frame[sentry]=fmodf(street_marker_frame[sentry]+(garagetest ? .5f:dt*30.f),60.f);
                gfx_mode2d();
            }
            if (gdisp_ok) { gfx_quad(0, 0, 480, 86 * 272 / 480, GCOLOR(0, 0, 0, 255)); gfx_quad(0, 394 * 272 / 480, 480, 272 - 394 * 272 / 480, GCOLOR(0, 0, 0, 255));   /* list 0x8c800fec: letterbox bars (drawn first) */
                if(!street_city)tex2d_draw_entry(&gdisp.t[1], 221.f, 30.f, 192.f, 47.f, 0.f, 0.f, 0xffffffffu, 0.348f, 0.391f, 0.723f, 0.482f); }   /* list 0x8c80366c: TOKYO-TO title */
            if(street_rival_active) {
                /* 0240bc: original chunk model, scale .0525 at z=-2.
                   Uniform projection preserves the lettering on PSP. */
                float proj[16],view[16],root[16];m_ident(view);m_ident(root);
                root[0]=root[5]=root[10]=.0525f;root[11]=-2;
                m_perspective(proj,2.f*atanf(240.f/450.f),480.f/272.f,.1f,10.f);
                gu_set(GU_PROJECTION,proj);gu_set(GU_VIEW,view);gfx_mode3d();sceGuClear(GU_DEPTH_BUFFER_BIT);
                chars_world_frustum(NULL);chars_draw_material(&street_rival_model,0,root,NULL,-1,0,1,NULL);gfx_mode2d();
                if(street_rival.fade>0)gfx_quad(0,0,480,272,(unsigned)(street_rival.fade*255.f)<<24);
            }else if(gdisp_ok){if(street_city)garage_ui_grind(&gdisp,&grind_map,sentry);else garage_ui_street(&gdisp,sentry);}
            gfx_end(); frames++;
            if(routetest==9 && !street_rival_active && sframe==60){runtime_log("ROUTE_DONE declined_map_return pending_checked=%d membership=%d\n",pending_checked,story[0x335]);dump_shot("shot_rival_declined_map.raw");}
            if(routetest && street_rival_active && (sframe==40 || sframe==54 || sframe==85)) {
                char name[48];snprintf(name,sizeof name,"shot_rival_map_%03d.raw",sframe);dump_shot(name);
            }
            if(garagetest==7 && (sframe==20 || sframe==40 || sframe==80 || sframe==100)) {
                char name[48];snprintf(name,sizeof name,"shot_street_%03d.raw",sframe);dump_shot(name);
            }
            if ((autopilot || garagetest==7) && sframe == 60) dump_shot("shot_street.raw"); continue;
        }
        if (state == 5) {   /* character select (garage mode 4, task 0x219 FUN_8c025044): left/right cycle the 15-slot table (wrap 0..14) over unlocked characters, A = pick, B = back */
            if (!garage_cast_active) { garage_cast_reset(&garage_cast);garage_cast_active=1; }
            if (!cframe) {
                if(!cspecial) {
                    int error=audio_load_garage_selection();
                    runtime_log("GARAGE_SELECTION_AUDIO load_error=%d\n",error);
                }
                garage_voice_tick=0;garage_queued_direction=0;garage_hold_time=0.f;
                garage_selection_init(&garage_selection,csel);
                memset(&garage_departure,0,sizeof garage_departure);
                if(garagetest==6)crec=mission_next(-1,1,1);
                if(cspecial) { /* 0f5e0c resets dance selection/frames, not the global RNG. */
                    memset(garage_cast.motion,0,sizeof garage_cast.motion);
                    memset(garage_cast.draw_motion,0,sizeof garage_cast.draw_motion);
                    memset(garage_cast.frame,0,sizeof garage_cast.frame);
                }
            }
            int departure_finished=garage_departure.phase==4;
            if(garage_departure.phase) {
                garage_exit.edges|=pressed&PSP_CTRL_CROSS;
                garage_exit.remainder+=garagetest ? 1.f/60.f:dt;
                while(garage_exit.remainder>=1.f/30.f) {
                    garage_exit.remainder-=1.f/30.f;
                    garage_exit_step(&garage_exit,garage_departure.phase,garage_exit.edges!=0);garage_exit.edges=0;
                    if(!garage_exit.done) {
                        int voice=garage_departure_voice(garage_departure.resident,garage_voice_tick++);
                        if(voice>=0) {
                            int error=audio_garage_selection_voice(voice);
                            runtime_log("GARAGE_SELECTION_AUDIO resident=%d tick=%u program=%d error=%d\n",garage_departure.resident,garage_voice_tick-1,voice,error);
                        }
                    }
                    garage_departure_step(&garage_departure,&garage_cast);
                }
            }
            else if(cspecial)garage_special_scene_update(&garage_selection,&garage_cast,story,100,garagetest ? 1.f/60.f:dt);
            else {
                /* 025044 case 3 triples the pan step while pad bit 0x10000 is
                   held (PSP: L). A direction held past a quarter second skims
                   the same way, so crossing the roster is not fifteen
                   two-second pans. Single taps keep the original timing. */
                int skim=(pad.Buttons&PSP_CTRL_LTRIGGER)!=0 || garage_hold_time>=.25f;
                garage_selection_scene_update(&garage_selection,&garage_cast,story,skim,100,garagetest ? 1.f/60.f : dt);
            }
            cframe++; int id = cspecial ? garage_special_character(csel):garage_cast_character(csel);
            #define CSEL_OK(i) (story[0x32c + (i)]==1) /* 024f04: resident flags, NOT graffiti unlocks. */
            if ((garagetest==3 || garagetest==4 || garagetest==5) && cframe==20) pressed|=PSP_CTRL_RIGHT;
            if(garagetest==4 && cframe==160)pressed|=PSP_CTRL_LEFT;
            if(garagetest>=4 && cframe==(garagetest==4 ? 300:160))pressed|=PSP_CTRL_CROSS;
            int direction=(pressed&PSP_CTRL_LEFT) ? -1 : (pressed&PSP_CTRL_RIGHT) ? 1 : 0;
            if(cspecial && (pressed&(PSP_CTRL_CROSS|PSP_CTRL_CIRCLE)))direction=0; /* 0f5eec: A, then B, then direction. */
            if(!cspecial && !garage_departure.phase) {
                /* PSP: the original drops direction presses during a pan and
                   never repeats a held direction. Keep the latest tap for the
                   next pan and let a held direction continue moving. */
                int held=(pad.Buttons&PSP_CTRL_LEFT) ? -1:(pad.Buttons&PSP_CTRL_RIGHT) ? 1:0;
                if(direction && garage_selection.moving)garage_queued_direction=direction;
                if(!garage_selection.moving) {
                    if(!direction && garage_queued_direction)direction=garage_queued_direction;
                    else if(!direction && held && garage_hold_time>=.25f)direction=held;
                    garage_queued_direction=0;
                }
                garage_hold_time=held ? garage_hold_time+(garagetest ? 1.f/60.f:dt):0.f;
            }
            if (direction && !garage_selection.moving && !garage_departure.phase) {
                if(cspecial)csel=1-csel; /* 0f5eec wraps both directions over exactly two entries. */
                else for (int k=0;k<15;k++) { csel=(csel+(direction>0 ? 1 : 14))%15;if (CSEL_OK(garage_cast_character(csel))) break; }
                garage_selection_move(&garage_selection,csel);
            }
            if (!cspecial && !CSEL_OK(garage_cast_character(csel))) { for (int k = 0; k < 15; k++) { csel = (csel + 1) % 15; if (CSEL_OK(garage_cast_character(csel))) break; } garage_selection_init(&garage_selection,csel); }
            id = cspecial ? garage_special_character(csel):garage_cast_character(csel);
            if (!garage_departure.phase && (pressed & PSP_CTRL_CIRCLE) && (!cspecial || (!garage_selection.moving && !(pressed&PSP_CTRL_CROSS)))) {
                audio_free_garage_selection();
                if(cpending){garage_pending_init(&pending,story);pending_axis=0;state=11;}
                else state = cspecial ? 3:4;
                continue;
            }
            int confirm=(garage_departure.phase ? garage_exit.done :
                ((pressed&PSP_CTRL_CROSS)!=0 || (autopilot && cframe==90))) && crec>=0;
            if(confirm && !cspecial && !garage_departure.phase && garage_departure_begin(&garage_departure,id)) {
                /* 025044 accepts even during a pan; 026234 owns a separate
                   authored camera and selected-only cast, not a frozen dance. */
                garage_exit_begin(&garage_exit);
                garage_departure_step(&garage_departure,&garage_cast);confirm=0;
                if(logfd>=0){char line[96];int n=snprintf(line,sizeof line,"GARAGE_CONFIRM begin character=%d motion=2\n",id);runtime_log_write(logfd,line,n);}
            }
            if (confirm) {
                audio_free_garage_selection();
                pchar = MODEL_OF_CHAR[id & 15]; int lvl = STAGE_OF_INDEX[missions[crec].st];
                /* Unsupported records must never silently load level 0. */
                if(!lvl) { printf("STORY unavailable stage=%u name=%.15s\n",missions[crec].st,missions[crec].name);state=4;continue; }
                if(cspecial && logfd>=0) { char line[96];int n=snprintf(line,sizeof line,"GARAGE_DEPART stage=%u character=%d name=%.15s\n",missions[crec].st,id,missions[crec].name);runtime_log_write(logfd,line,n); }
                if (garage_ok) { garage_cast_assets_free(&garage); garage_ok = 0; } if (gmark_ok) { chars_free(&gmark); gmark_ok = 0; } if (gdisp_ok) { tex2d_free(&gdisp); gdisp_ok = 0; }
                level_load(&L, lvl, &missions[crec]); mcur = crec; state = 1;
                if(garage_departure.phase && logfd>=0){char line[160];int n=snprintf(line,sizeof line,"GARAGE_CONFIRM loaded character=%d finished=%d frame=%.0f error=%d fade=%.9g exit=%d\n",id,departure_finished,garage_departure.frame,L.campaign_error,garage_exit.alpha,garage_exit.phase);runtime_log_write(logfd,line,n);}
                continue;
            }
            /* 025924: selected-slot camera objects from 10dff4 / 10e030. */
            float proj[16], view[16], eye[3] = { 29.9f, 13.4f, -48.6f }, tgt[3] = { 27.1f, 8.9f, -33.5f };
            int camera_roll=0;float camera_fov;
            if(garage_departure.phase)garage_departure_view(&garage_departure,eye,tgt,&camera_fov,&camera_roll);
            else if(cspecial)garage_special_view(&garage_selection,eye,tgt,&camera_fov);else garage_selection_view(&garage_selection,eye,tgt,&camera_fov);
            float fwd[3] = { tgt[0] - eye[0], tgt[1] - eye[1], tgt[2] - eye[2] }; { float l = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]); if (l > 0) { fwd[0] /= l; fwd[1] /= l; fwd[2] /= l; } }
            float garage_focal=player_camera_focal((int)lroundf(camera_fov*(65536.f/6.283185307f)),640,1);
            m_perspective(proj,2.f*atanf(240.f/garage_focal),640.f/480.f,1,garage_departure.phase ? 32768:3000);
            player_camera_matrix(eye,fwd,camera_roll,view);
            gfx_begin_room(); gfx_clear(GCOLOR(0, 0, 0, 255)); gu_set(GU_PROJECTION, proj); gu_set(GU_VIEW, view); gfx_mode3d();
            float room_clip[16],room_planes[6][4];m_mul(room_clip,proj,view);frustum_planes(room_clip,room_planes);
            chars_environment_view(view);garage_cast_frustum(room_planes);garage_light_begin();
            gfx_profile_section(GFX_STAGE);
            if (garage_ok) garage_room_draw_pass(&garage,garage_departure.phase ? garage_departure.room_frame:garage_cast.room_frame,0,room_planes);
            gfx_profile_section(GFX_EFFECTS);
            if(garage_ok) {
                if(garage_departure.phase)garage_departure_shadows(&garage_departure,&garage,view);
                else garage_cast_shadows(&garage_cast,story,cspecial ? -1:garage_cast_character(garage_selection_display(&garage_selection)),&garage,view);
            }
            gfx_profile_section(GFX_ACTORS);
            if (garage_ok) {
                if(garage_departure.phase)garage_departure_draw(&garage_departure,&garage);
                else if(cspecial)garage_cast_draw(&garage_cast,story,&garage);else garage_selection_draw(&garage_cast,story,garage_cast_character(garage_selection_display(&garage_selection)),&garage);
                if(!cspecial && !garage_departure.phase)garage_pet_draw(&garage_cast,story,&garage);
            }
            gfx_profile_section(GFX_STAGE);
            if (garage_ok) garage_room_draw_pass(&garage,garage_departure.phase ? garage_departure.room_frame:garage_cast.room_frame,1,room_planes);
            if(garage_ok && (cspecial || story[0x33e]==1))garage_special_prop_draw(&garage);
            garage_light_end();garage_cast_frustum(NULL);
            gfx_profile_section(GFX_HUD);gfx_mode2d();
            if (gdisp_ok) garage_cast_ui(&gdisp,garage_departure.phase ? garage_departure.resident:cspecial ? garage_special_character(garage_selection_display(&garage_selection)):garage_cast_character(garage_selection_display(&garage_selection)));
            if(gdisp_ok && !garage_departure.phase)garage_ui_selection(&gdisp,story,garage_selection_display(&garage_selection),cspecial,!garage_selection.moving);
            if(garage_departure.phase && garage_exit.alpha>0)
                gfx_quad(0,0,480,272,GCOLOR(0,0,0,(int)(255.f*garage_exit.alpha)));
            gfx_end(); frames++; if ((autopilot || garagetest) && cframe == 80) dump_shot("shot_csel.raw");
            if (garagetest && cframe==46) dump_shot("shot_csel_frame23.raw");
            if (garagetest && cframe==140) dump_shot("shot_csel_dance.raw");
            if((garagetest==4 || garagetest==5) && cframe==150)dump_shot("shot_csel_cube_idle.raw");
            if ((garagetest==3 || garagetest>=4) && cframe>=20 && cframe<=(garagetest==4 ? 290:140) && !((cframe-20)%30)) {
                char name[40];snprintf(name,sizeof name,"shot_csel_pan_%03d.raw",cframe);dump_shot(name);
            }
            if(garagetest==6 && cframe>=200 && cframe<=400 && cframe%100==0) {
                char name[40];snprintf(name,sizeof name,"shot_csel_depart_%03d.raw",cframe);dump_shot(name);
            }
            if(garagetest==6 && garage_exit.alpha>0 && (cframe==450 || cframe==470 || cframe==480)) {
                char name[48];snprintf(name,sizeof name,"shot_csel_fade_%03d.raw",cframe);dump_shot(name);
            }
            if (garagetest && garagetest!=4 && garagetest!=6 && cframe==180) break;
            continue;
        }
        if (state == 0) {
            if (pressed & PSP_CTRL_UP) { cursor = (cursor + NLEVELS - 1) % NLEVELS; mcur = mission_next(-1, LEVELS[cursor].id, 1); }
            if (pressed & PSP_CTRL_DOWN) { cursor = (cursor + 1) % NLEVELS; mcur = mission_next(-1, LEVELS[cursor].id, 1); }
            if (pressed & PSP_CTRL_RTRIGGER) mcur = mission_next(mcur, LEVELS[cursor].id, 1);
            if (pressed & PSP_CTRL_LTRIGGER) mcur = mission_next(mcur, LEVELS[cursor].id, -1);
            if (mcur < 0) mcur = mission_next(-1, LEVELS[cursor].id, 1);
            if (autoname[0]) { for (int i = 0; i < nmis; i++) if (mis_head[i] && !strncmp(missions[i].name, autoname, 6)) { int st = STAGE_OF_INDEX[missions[i].st < 7 ? missions[i].st : 6]; for (int l = 0; l < NLEVELS; l++) if (LEVELS[l].id == st) cursor = l; mcur = i; } autoname[0] = 0; }
            if (pressed & PSP_CTRL_LEFT) pchar = (pchar + 15) % 16;
            if (pressed & PSP_CTRL_RIGHT) pchar = (pchar + 1) % 16;
            if ((pressed & (PSP_CTRL_CROSS | PSP_CTRL_START)) || (autopilot && frames > 30)) {
                if(mcur>=0){bgm_stage_idx=missions[mcur].st;bgm_slot=atoi(missions[mcur].name+3)-1;}
                { char m[32]; runtime_log_write(logfd, m, snprintf(m, 32, "loading\n")); } level_load(&L, LEVELS[cursor].id, mcur >= 0 ? &missions[mcur] : 0); { char m[32]; runtime_log_write(logfd, m, snprintf(m, 32, "loaded\n")); } audio_set_music(1); state = 1; eye[0] = L.cam_eye[0]; eye[1] = L.cam_eye[1]; eye[2] = L.cam_eye[2];
                /* Isolated interactive inspection: boot the requested mission,
                   then hand all controls back. campaign_writable was disabled
                   while this fixture was active, so real saves stay untouched. */
                if(autopilot==7){autopilot=0;runtime_log("MANUAL_INSPECTION controls=live writable=%d\n",campaign_writable);}
            }
            gfx_begin(); gfx_clear(GCOLOR(12, 12, 28, 255)); gfx_mode2d();
            font_draw_center(SCR_W / 2 + 2, 42, 3, GCOLOR(20, 20, 30, 255), "JET GRIND RADIO"); font_draw_center(SCR_W / 2, 40, 3, GCOLOR(255, 214, 26, 255), "JET GRIND RADIO");
            font_draw_center(SCR_W / 2, 72, 1, GCOLOR(120, 200, 255, 255), "PSP PORT - SELECT STAGE");
            for (int i = 0; i < NLEVELS; i++) { char b[48]; snprintf(b, sizeof b, "%s%s", i == cursor ? "> " : "  ", LEVELS[i].name); font_draw(150, 110 + i * 20, 2, i == cursor ? GCOLOR(255, 255, 255, 255) : GCOLOR(140, 140, 160, 255), b); }
            { char b[48]; snprintf(b, sizeof b, "< %s >", SKATERS[pchar]); font_draw_center(SCR_W / 2, 214, 2, GCOLOR(120, 230, 255, 255), b); }
            { char b[64]; if (mcur >= 0) snprintf(b, sizeof b, "L/R MISSION: %.15s  KIND %u  SET %u", missions[mcur].name, missions[mcur].kind, missions[mcur].set); else snprintf(b, sizeof b, "FREE ROAM"); font_draw_center(SCR_W / 2, 236, 1, GCOLOR(255, 220, 120, 255), b); }
            font_draw_center(SCR_W / 2, 240, 1, GCOLOR(160, 160, 160, 255), "UP/DOWN STAGE  LEFT/RIGHT SKATER  X START");
            font_draw_center(SCR_W / 2, 254, 1, GCOLOR(160, 160, 160, 255), "STICK SKATE  X JUMP  SQUARE SPRAY  TRICKS IN AIR: SQUARE/TRIANGLE/CIRCLE");
            gfx_end(); frames++; continue;
        }

        /* A failed load leaves a partial Level, not a playable world.
           In particular its camera and mission/controller bank are unset.
           Keep all simulation, scene preload and 3D drawing out of it. */
        if(L.campaign_error) {
            if(pressed&PSP_CTRL_START){audio_pause_music(0);audio_set_music(0);level_unload(&L);title_mode=0;state=0;continue;}
            gfx_begin();gfx_clear(GCOLOR(12,12,28,255));gfx_mode2d();
            font_draw_center(240,106,1,GCOLOR(255,160,100,255),"CAMPAIGN DATA / HANDOFF UNAVAILABLE");
            char message[64];snprintf(message,sizeof message,"ERROR %d  SEE LOG  -  START: RETURN",L.campaign_error);
            font_draw_center(240,126,1,GCOLOR(255,255,255,255),message);
            gfx_end();runtime_log_flush();frames++;continue;
        }
        /* ---------- playing ---------- */
        Player *pl = &L.pl;
        if(L.ms.phase==MISSION_PHASE_EXIT && L.exit.black_presented) {
            int to_title=L.mrec && level_exit_returns_title(L.mrec->st,L.mrec->name);
            runtime_log("LEVEL_EXIT complete name=%.15s destination=%s clear=%d results=%d recorded=%d writable=%d\n",
                L.mrec ? L.mrec->name:"?",to_title ? "title":"garage",
                L.ms.f30,L.ms.res_done,L.recorded,campaign_writable);
            bgm_leave();audio_pause_music(0);audio_set_music(0);
            level_unload(&L);title_mode=0;state=to_title ? 2:3;
            runtime_log_safe_checkpoint("level_exit");
            gframe=0;pending_checked=0;cpending=0;
            if(exit_fixture.enabled)exit_fixture_destination=state;
            continue;
        }
        if(L.ms.phase==MISSION_PHASE_EXIT) {
            if(!L.exit.active) {
                level_exit_begin(&L.exit);
                runtime_log("LEVEL_EXIT begin name=%.15s xyz=%.6f,%.6f,%.6f\n",
                    L.mrec ? L.mrec->name:"?",pl->x,pl->y,pl->z);
            }
            level_exit_advance(&L.exit,dt);
            pressed=0;pad.Buttons=0;pad.Lx=pad.Ly=128;
        }
        if (title_mode) {
            title_frames++; unsigned tp = pressed; pressed = 0; pad.Buttons = 0; pad.Lx = pad.Ly = 128;
            if ((tp & (PSP_CTRL_START | PSP_CTRL_CROSS)) || (autopilot && title_frames > story_start_frame)) {
                title_mode=0;audio_set_music(0);audio_free_movie_voice();level_unload(&L);
                tex2d_free(&title_ui);list2d_free(&title_lists);
                if(tlogo_ok){chars_free(&tlogo);tlogo_ok=0;}
                state=3;gframe=0;
                if(new_story) {
                    /* 015ef4 task 19 / 0153a4 mode 8: Beat, stage 0,
                       chapter 10 (S1M11). Existing saves still enter garage.
                       This entry restores lessons, not the missing title menu. */
                    const char *name=story_file_name(story_chapter_file(0,10));int rec=-1;
                    for(int i=0;i<nmis;i++)if(mis_head[i] && !strcmp(missions[i].name,name)){rec=i;break;}
                    if(rec>=0) {
                        /* 05183c: fresh game first plays DEMO30 (id 29),
                           then 0153a4 returns to the original lesson route. */
                        char missing[64];mcur=rec;campaign_movie=1;campaign_movie_return=1;
                        endingplay_begin_movie(&ending,29);ending_previous=-1;ending_save_error=0;state=15;
                        if(!movie_assets_available(29,story,missing)){ending.error=1;runtime_log("CAMPAIGN_MOVIE missing=%s\n",missing);}
                        runtime_log("CAMPAIGN_MOVIE start=29 tutorial=%s\n",name);
                    } else printf("STORY new-game mission unavailable\n");
                }
                continue;
            }
        }
        if(pausehudtest && !sceneplay_resident(&L.scene) && frames>400 && !L.ms.phase &&
           (L.pause.active || pl->logic_tick-L.pause_resume_tick>15)){
            if(!pausehud_phase){
                if(pausehudtest==4){
                    /* Isolated map fixture: supply completed local graffiti,
                       then use the ordinary pause/render/resume path. */
                    int area=L.zerr ? 0:zone_area(&L.zones,L.cur_zone),marked=0;
                    for(int i=0;i<L.gf.n;i++){
                        Spot *s=L.gf.spots+i;
                        if(s->area==area && s->active && s->required && !s->tagged){
                            s->tagged=1;s->complete=1;marked++;
                        }
                    }
                    L.gf.tagged+=marked;L.ms.ntagged+=marked;
                    runtime_log("PAUSE_MAP_FIXTURE supplied area=%d marked=%d remaining=%d\n",area,marked,L.ms.nreq-L.ms.ntagged);
                }
                pressed|=PSP_CTRL_START;pausehud_phase=1;
            }
            if(L.pause.active){
                if(pausehudtest==3){
                    /* Exercise the nub at 30 Hz, including release/history;
                       only START/confirm are buttons in this fixture. */
                    if(L.pause.clock>=35 && L.pause.clock<38){pad.Ly=255;pausehud_phase=2;}
                    if(L.pause.clock>=40 && L.pause.clock<50){pad.Lx=255;pad.Ly=112;}
                    if(L.pause.clock>=55 && L.pause.clock<58){pad.Ly=0;pausehud_phase=3;}
                }else{
                    if(L.pause.clock>=35 && pausehud_phase==1){pressed|=PSP_CTRL_DOWN;pausehud_phase=2;}
                    if(L.pause.clock>=40 && L.pause.clock<50)pad.Buttons|=PSP_CTRL_RIGHT;
                    if(L.pause.clock>=55 && pausehud_phase==2){pressed|=PSP_CTRL_UP;pausehud_phase=3;}
                }
                if(pausehudtest==2 && L.pause.clock>=65 && pausehud_phase==3){pressed|=PSP_CTRL_RIGHT;pausehud_phase=6;}
                if(L.pause.clock>=75 && (pausehud_phase==3 || pausehud_phase==6)){pressed|=PSP_CTRL_CROSS;pausehud_phase=4;}
            }
        }
        if (L.campaign_error && (pressed & PSP_CTRL_START)) { audio_pause_music(0);audio_set_music(0); level_unload(&L); state = 0; continue; }
        audio_player_suspend(L.pause.active || L.ms.phase || sceneplay_blocks_gameplay(&L.scene));
        audio_world_suspend(L.pause.active || L.ms.phase || sceneplay_blocks_gameplay(&L.scene));
        if((pressed&PSP_CTRL_START) && !L.pause.active && !L.ms.phase && !sceneplay_blocks_gameplay(&L.scene) &&
           L.mrec && L.mrec->kind!=6 && pl->logic_tick-L.pause_resume_tick>15){
            /* World profiles already retire on pause (play limit 1).
               Release their now-unused sample banks while traffic and the
               pause/map/font assets coexist. Normal audio binding reloads
               them before the first resumed gameplay tick; actor handles
               remain monotonically unique across bank reloads. */
            if(L.traffic && world_audio_selection>=0) {
                audio_world_suspend(1);audio_free_world();world_audio_selection=-1;
                runtime_log("WORLD_AUDIO pause bank released heap=%u\n",(unsigned)mallinfo().fordblks);
            }
            /* 03345c selects 03527c for task-6 stage 0 / chapter 4.
               S1M05 is that chapter's original file (its public mid is 32). */
            int practice=L.id==1 && !strcmp(L.mrec->name,"S1M05");
            if(practice && practice_hud_load(&L.practice_hud)){
                L.campaign_error=10;runtime_log("HUD original practice assets unavailable\n");continue;
            }
            if(!practice){
            if(!pause_models.h && chars_load(&pause_models,"pausehud.jgc")){
                L.campaign_error=10;printf("HUD original pause assets unavailable\n");continue;
            }
            if(pause_map_load(&L.pause_map_models)){
                L.campaign_error=10;runtime_log("HUD original pause map assets unavailable\n");continue;
            }
            if(!font_real_wide_ready()){
                if(font_real_load_wide("fontwide.bin")){chars_free(&L.pause_map_models);L.campaign_error=10;continue;}
                L.pause_map_wide_owned=1;
            }
            int area=L.zerr ? 0:zone_area(&L.zones,L.cur_zone);
            L.pause_map=(PauseMap){.stage=L.id-1,.chapter=L.mrec->mid,.kind=L.mrec->kind,.remaining=L.ms.nreq-L.ms.ntagged};
            pause_map_select(&L.pause_map,area,&L.gf);
            if(!pause_map_entry(&L.pause_map)){L.campaign_error=10;runtime_log("HUD invalid source pause map stage=%d chapter=%d area=%d\n",L.id,L.mrec->mid,area);continue;}
            }
            pause_hud_init(&L.pause,pause_volume);memset(&L.pause_wipe,0,sizeof L.pause_wipe);L.pause_tick=0;L.pause_edge=0;
            L.pause.practice=practice;
            if(pausehudtest){pausehud_tick=pl->logic_tick;pausehud_pos[0]=pl->x;pausehud_pos[1]=pl->y;pausehud_pos[2]=pl->z;pausehud_time=L.ms.time_left;pausehud_health=pl->health;}
            audio_menu_sound(1);audio_pause_music(1);pressed&=~PSP_CTRL_START;
            runtime_release_health();runtime_log_safe_checkpoint("pause");
        }
        if(L.pause.active){
            int quit_ready=0;
            L.pause_edge|=(pressed&PSP_CTRL_CROSS ? 4:0)|(pressed&PSP_CTRL_START ? 8:0)|
                (pressed&PSP_CTRL_UP ? 16:0)|(pressed&PSP_CTRL_DOWN ? 32:0)|
                (pressed&PSP_CTRL_LEFT ? 64:0)|(pressed&PSP_CTRL_RIGHT ? 128:0);
            unsigned held=(pad.Buttons&PSP_CTRL_LEFT ? 64:0)|(pad.Buttons&PSP_CTRL_RIGHT ? 128:0);
            /* 0b2828 -> 103df0: pause uses full BAM direction, not the
               player replay's 8-bit angle. Keep menu directions independent
               of the explicitly swapped gameplay nub/D-pad mapping. */
            float pause_x=(int)pad.Lx-128,pause_y=(int)pad.Ly-128;
            if(pause_x*pause_x+pause_y*pause_y<20.f*20.f)pause_x=pause_y=0;
            int pause_angle=player_bam_atan2(pause_x,-pause_y);
            float pause_mag=fminf(1.f,sqrtf(pause_x*pause_x+pause_y*pause_y)/127.f);
            L.pause_tick+=dt;
            while(L.pause_tick>=1.f/30.f && L.pause.active){
                if(pausehudtest==3){
                    /* The fixture's authored tick ranges must not gain an
                       extra held-volume sample when a slow frame batches
                       two source updates. Normal physical input is unchanged. */
                    float x=0,y=0;int clock=L.pause.clock;
                    if(clock>=35 && clock<38)y=127;
                    if(clock>=40 && clock<50){x=127;y=-16;}
                    if(clock>=55 && clock<58)y=-128;
                    pause_angle=player_bam_atan2(x,-y);pause_mag=fminf(1.f,sqrtf(x*x+y*y)/127.f);
                }
                int action=pause_hud_tick_analog(&L.pause,held,L.pause_edge,L.mrec && L.mrec->mid!=0,pause_angle,pause_mag,player_fsca_sin);
                pause_hud_present(&L.pause);
                L.pause_edge=0;L.pause_tick-=1.f/30.f;
                if(L.pause.sound>=0)audio_menu_sound(L.pause.sound);
                pause_volume=L.pause.volume;
                if(L.pause.volume_command>=0)audio_music_volume(L.pause.volume_command);
                if(pausehudtest==3 && (L.pause.sound>=0 || L.pause.volume_command>=0) && logfd>=0){
                    char line[160];int n=snprintf(line,sizeof line,"PAUSE_ANALOG clock=%d angle=%d magnitude=%.9g choice=%d sound=%d volume=%d\n",
                        L.pause.clock,pause_angle,pause_mag,L.pause.selection,L.pause.sound,L.pause.volume_command);
                    runtime_log_write(logfd,line,n);
                }
                if(action==1){
                    audio_pause_music(0);L.pause_resume_tick=pl->logic_tick;chars_free(&L.pause_map_models);
                    chars_free(&pause_models);
                    practice_hud_free(&L.practice_hud);
                    if(L.pause_map_wide_owned){font_real_free_wide();L.pause_map_wide_owned=0;}
                }
                if(action==2){pause_wipe_init(&L.pause_wipe,0);}
                /* 033efc waits for child 218 to reach phase 0, then spends
                   another parent update in phase 4 before stage cleanup. */
                else if(L.pause.phase==4){quit_ready=1;break;}
                else if(L.pause.phase==3){
                    if(!L.pause_wipe.phase)L.pause.phase=4;
                    else pause_wipe_tick(&L.pause_wipe);
                }
            }
            /* The resume A edge must not also jump or alter debug toggles. */
            pad.Buttons=pressed=0;pad.Lx=pad.Ly=128;
            if(quit_ready){
                if(pausehudtest && logfd>=0){const char *line="PAUSE_HUD garage return phase=4 wipe=0\n";runtime_log_write(logfd,line,strlen(line));}
                bgm_leave();audio_pause_music(0);audio_set_music(0);level_unload(&L);state=3;gframe=0;pending_checked=0;cpending=0;continue;
            }
        }
        /* 018a0c -> 032968: failure owns confirmation, not an immediate
           X shortcut. The original presentation increments +78 as well as
           the phase-1 update; run both once per source-rate HUD frame. */
        if(failurehudtest>0 && !sceneplay_resident(&L.scene) && frames>400 && !L.ms.phase){
            if(failurehudtest==3) {
                /* Deplete health, then let the ordinary mission tick detect
                   defeat. A supplied phase alone missed zero-health retries. */
                if(pl->health>0)runtime_log("FAILURE_HEALTH supplied depletion from=%d character=%d; no campaign progress\n",pl->health,pl->character_id);
                pl->health=0;
            } else {
                L.ms.phase=2;L.ms.f40=failurehudtest!=2;L.ms.f3c=failurehudtest==2;
                printf("FAILURE_HUD supplied outcome cause=%d; no campaign progress\n",failurehudtest);
            }
        }
        if(failurehudtest==3 && L.failure.active && L.failure.phase==3){
            if(L.failure.clock>=35 && L.failure.clock<40)pad.Lx=0;
            if(L.failure.clock>=65 && L.failure.clock<70)pad.Lx=255;
            if(L.failure.clock>=130){pressed|=PSP_CTRL_CROSS;failurehudtest=-3;}
        }
        if(L.ms.phase==2 && !L.campaign_error){
            if(!L.failure.active && !L.failure_action){
                level_retire_gameplay_resources(&L);
                failure_hud_init(&L.failure,L.ms.f3c!=0);
                int voice_error=audio_load_hud_voice(CHAR_ID_OF_MODEL[pchar&15]);
                runtime_log("HUD_AUDIO failure load=%d character=%d\n",voice_error,CHAR_ID_OF_MODEL[pchar&15]);
                police_alert_kill(&L.police); /* 032968 deletes task 5a */
                if(!failure_models.models.h && failure_models_load(&failure_models,"failurehud.jgc","failurehud.bin")){
                    L.campaign_error=10;printf("HUD original failure assets unavailable\n");continue;
                }
            }
            unsigned edge=(pressed&PSP_CTRL_CROSS ? 4:0)|(pressed&PSP_CTRL_LEFT ? 0x40:0)|(pressed&PSP_CTRL_RIGHT ? 0x80:0);
            L.failure_edge|=edge;
            float failure_x=(int)pad.Lx-128,failure_y=(int)pad.Ly-128;
            if(failure_x*failure_x+failure_y*failure_y<400.f)failure_x=failure_y=0;
            int failure_angle=player_bam_atan2(failure_x,-failure_y);
            float failure_mag=fminf(1.f,sqrtf(failure_x*failure_x+failure_y*failure_y)/127.f);
            L.failure_tick+=dt;
            while(L.failure_tick>=1.f/30.f){
                int sound,action=failure_hud_tick_analog(&L.failure,L.failure_edge,L.mrec && L.mrec->mid!=0,L.mrec ? L.mrec->mid:0,L.failure_fade>=1.f,
                    failure_angle,failure_mag,player_fsca_sin,&sound);
                L.failure_edge=0;failure_hud_present(&L.failure);L.failure_tick-=1.f/30.f;
                if(sound>=100)audio_menu_sound(sound-100);
                else if(sound>=0)runtime_log("HUD_AUDIO failure program=%d error=%d\n",sound,audio_hud_voice(sound));
                if((failurehudtest==3 || failurehudtest==-3) && sound>=100)
                    runtime_log("FAILURE_ANALOG clock=%d angle=%d choice=%d program=%d action=%d\n",L.failure.clock,failure_angle,L.failure.selection,sound-100,action);
                if(action){L.failure_action=action;L.failure_fade=action==FAILURE_RETRY ? 1.f:0.f;}
                if(L.failure_action && L.failure_fade<1.f){L.failure_fade+=.04f;if(L.failure_fade>1.f)L.failure_fade=1.f;}
                if(failurehudtest)failurehud_age++;
            }
            pressed&=~PSP_CTRL_CROSS; /* no early confirm leaks to result routing */
        }
        if (!L.campaign_error && L.ms.active && L.ms.phase == 2 && L.mrec && L.failure_action==FAILURE_RETRY && L.failure.phase>=5) {   /* 0ca1dc: copy the retry pair, then run the same loader selection */
            const Mission *n=NULL;int ns=L.ms.retry_set;
            int result=mission_handoff(missions,nmis,L.mrec,L.ms.retry_rec,ns,L.ctl.nsub,&n);
            if(result>0){
                if(failurehudtest==-3)runtime_log("FAILURE_ANALOG retry ready record=%d set=%d\n",L.ms.retry_rec,ns);
                L.ms.next_rec=L.ms.retry_rec;L.ms.next_set=ns;mission_begin(&L,n,ns);
                if(failurehudtest==-3)runtime_log("FAILURE_HEALTH retry health=%d maximum=%d character=%d error=%d\n",pl->health,pl->health_max,pl->character_id,L.campaign_error);
                player_restart_score(pl);L.score=0;continue;
            }
            L.campaign_error=6;printf("CAMPAIGN invalid retry name=%.15s record=%d set=%d result=%d\n",L.mrec->name,L.ms.retry_rec,ns,result);
        }
        if(!L.campaign_error && L.ms.phase==2 && L.failure_action!=FAILURE_RETRY && L.failure_action && L.failure.phase>=5){
            int destination=L.failure_action;bgm_leave();audio_set_music(0);level_unload(&L);state=destination==FAILURE_TITLE ? 2:3;gframe=0;pending_checked=0;cpending=0;continue;
        }
        if(failurehudtest==-3 && !L.campaign_error && !sceneplay_resident(&L.scene) && !L.ms.phase && ++failure_retry_age==90)
            runtime_log("FAILURE_HEALTH resumed health=%d maximum=%d phase=%d frames=%d\n",pl->health,pl->health_max,L.ms.phase,failure_retry_age);
        int recruitment_result=L.mrec && L.mrec->kind!=6 && L.id!=4;
        if(resulthudtest>0 && !sceneplay_resident(&L.scene) && frames>400 && !L.ms.phase){
            L.ms.phase=1;L.score=321;pl->health=50;L.gf.cancount=7;resulthudtest=-1;
            /* Explicit presentation-only acquisition events. Do not invent
               world pickups or unlock save flags to exercise this row. */
            if(resulthudsouls>0 && resulthudsouls<=99)for(int i=0;i<resulthudsouls;i++)story_graffiti_acquired(0,0);
            printf("RESULT_HUD supplied clear/score/health/cans; no earned progression or save writes\n");
        }
        if(L.ms.phase==1 && (pressed&PSP_CTRL_CROSS))L.result_confirm=1;
        int result_scene_blocked=L.ms.phase==1 &&
            ((recruitment_result && (!L.result.active || L.result.phase!=0xf)) ||
             (level_result_scene_id(&L)>=0 && L.result_scene!=2));
        if (result_terminal_ready(&L.ms,&L.final,L.id,L.campaign_error,result_scene_blocked,
            (pressed & PSP_CTRL_CROSS) || (recruitment_result && L.ms.phase==1 && L.result.phase==0xf) || (autopilot && !title_mode && L.ms.res_done))) {
            if(recruitresulttest<0 && L.ms.phase==1 && L.result_scene==2) {
                recruit_garage_pending=-recruitresulttest;
                if(logfd>=0){char line[80];int n=snprintf(line,sizeof line,"RECRUIT_TERMINAL ready=%d\n",recruit_garage_pending);runtime_log_write(logfd,line,n);}
            }
            int destination=7,movie_id=-1,ending_character=CHAR_ID_OF_MODEL[pchar&15];
            if(L.ms.phase==1 && L.mrec) {
                if(resulthudtest && logfd>=0){char line[160];int n=snprintf(line,sizeof line,"RESULT_HUD destination ready clock=%d displayed=%d total=%d expected=%d phase=%d\n",L.result.clock,L.result.displayed,L.result.total,L.ms.res_total,L.result.phase);runtime_log_write(logfd,line,n);}
                level_result_totals(&L);
                destination=story_result_destination(L.mrec->st,L.mrec->mid,(int)mission_word(&L.ms,0x18),L.mrec->kind,L.record_place==0);
                if(destination==14){char missing[64];if(!ending_assets_available(story,pchar&15,missing)){
                    L.campaign_error=8;printf("CAMPAIGN ending asset unavailable: %s; result retained\n",missing);continue;}}
                level_story_retire(&L);
                if(destination==7) {
                    movie_id=movie_for_result(L.mrec->mid,story);
                    if(movie_id==-2){L.campaign_error=9;runtime_log("CAMPAIGN_MOVIE invalid result context mid=%d writable=%d\n",L.mrec->mid,campaign_writable);continue;}
                    char missing[64];
                    if(movie_id>=0 && !movie_assets_available(movie_id,story,missing)){
                        L.campaign_error=8;runtime_log("CAMPAIGN_MOVIE missing=%s; live result retained\n",missing);continue;}
                }
                {int id=story_notification_take();if(id>=0)hud_notice_acquired(&garage_notice,id);}
            }
            if(movie_id<0)audio_set_music(0);
            level_unload(&L);state=3;gframe=0;pending_checked=0;cpending=0;
            if(movie_id>=0){campaign_movie=1;campaign_movie_return=3;endingplay_begin_movie(&ending,movie_id);ending_previous=-1;ending_save_error=0;state=15;
                runtime_log("CAMPAIGN_MOVIE start=%d writable=%d\n",movie_id,campaign_writable);}
            if(destination==14){endingplay_init(&ending,ending_character);ending_previous=-1;ending_save_error=0;state=15;}
            if(destination==15){save_prompt_init(&save_prompt);save_error=0;save_return=3;state=10;}
            continue;
        } /* Result choreography is still partial. Fixtures never write progress. */
        int render_bench_request=JGR_DIAGNOSTICS && viewer_select_state.stats && (pressed&PSP_CTRL_TRIANGLE);
        if(render_bench_request){pressed&=~PSP_CTRL_TRIANGLE;pad.Buttons&=~PSP_CTRL_TRIANGLE;}
        if (pressed & PSP_CTRL_SELECT) { gamemode ^= 1; if (!gamemode) { eye[0] = L.cam_eye[0]; eye[1] = L.cam_eye[1]; eye[2] = L.cam_eye[2]; yaw = L.cam_yaw; pitch = L.cam_pitch; } }
        viewer_controls(gamemode,pressed,pad.Buttons,&cullmode,&wire,&freeze,&vsync);

        /* Digital steering fallback for PPSSPP keyboard/default mappings.  Do
           this before graffiti and player input consume the analog axes. */
        int spraying = 0, scene_frozen=0; float fwd_override[3] = {0, 0, 1}; int use_fwd = 0;
        if (autopilot && gamemode && !title_mode && !L.pause.active) { logt += dt; pad.Lx = 128 + (int)(40.f * sinf(logt * 0.7f)); pad.Ly = 20; if (fmodf(logt, 4.f) < dt) pressed |= PSP_CTRL_CROSS;
            if (!title_mode && autopilot == 3 && L.ms.active && !L.ms.phase && L.mrec && L.mrec->kind != 6 && L.ctl.sub[L.set_idx].ntrial && L.trial.index < L.ctl.sub[L.set_idx].ntrial && fmodf(logt, 2.f) < dt) {   /* self-test: teleport into the current A volume */
                const TrialEntry *e = &L.ctl.sub[L.set_idx].trial[L.trial.index];
                if (e->type == 5 && !L.gferr) { for (int i = 0; i < L.gf.n; i++) { Spot *sp = &L.gf.spots[i]; if (sp->active && !sp->tagged) { sp->tagged = 1; sp->yaw = pl->yaw; L.gf.tagged++; break; } } }
                if (e->na) { float cx = 0, cy = 0, cz = 0; float top = -1e9f; for (int k = 0; k < 6; k++) { cx += e->quads[k*3]; cy += e->quads[k*3+1]; cz += e->quads[k*3+2]; if (e->quads[k*3+1] > top) top = e->quads[k*3+1]; } cx /= 6; cy /= 6; cz /= 6;
                    float g; pl->x = cx; pl->z = cz; pl->y = cy; if (!L.hferr && ground_at(&L.col, cx, cz, top + 10.f, &g) == 0) pl->y = g; pl->speed = 0; pl->vy = 0; pl->onground = 1; pl->grinding = 0; pl->state = 1; player_cam_reset(pl); pad.Lx = pad.Ly = 128; pressed &= ~PSP_CTRL_CROSS; } }
            if (autopilot == 3 && L.mrec && L.mrec->kind != 6) { pad.Lx = pad.Ly = 128; pressed &= ~PSP_CTRL_CROSS; }
            if (!title_mode && autopilot == 4 && L.nrv && logt < 1.2f) { struct Rival *r = &L.rv[0]; pl->x = r->pl.x - 20.f * sinf(r->pl.yaw); pl->z = r->pl.z - 20.f * cosf(r->pl.yaw); pl->y = r->pl.y; pl->yaw = r->pl.yaw; }
            if (autopilot == 4) { pad.Lx = pad.Ly = 128; pressed &= ~PSP_CTRL_CROSS; }
            if (autopilot == 5) { pad.Lx = pad.Ly = 128; pressed &= ~PSP_CTRL_CROSS; L.play.active = 0; if (frames < (lookat_live ? lookat_live:380)) { static float lk[4] = {1700.f, 2.f, 1300.f, 9999.f}; static int lk_read = 0; if (!lk_read) { lk_read = 1; FILE *lf = fopen("lookat", "r"); if (lf) { fscanf(lf, "%f %f %f %f", &lk[0], &lk[1], &lk[2], &lk[3]); fclose(lf); } } pl->x = lk[0]; pl->y = lk[1]; pl->z = lk[2]; pl->yaw = lk[3] < 9000.f ? lk[3] : atan2f(1756.f - 1700.f, 1344.f - 1300.f); pl->speed = 0; player_cam_reset(pl); }
                if (frames > 380) { pl->speed = 0; } }
            if (!title_mode && autopilot == 3 && L.ms.active && !L.ms.phase && L.mrec && L.mrec->kind == 3) { for (int i = 0; i < L.nrv; i++) { struct Rival *r = &L.rv[i]; if (!r->active) continue;
                if (fmodf(logt, 1.f) < dt) { pl->x = r->pl.x - 6.f * sinf(r->pl.yaw); pl->z = r->pl.z - 6.f * cosf(r->pl.yaw); pl->y = r->pl.y; pl->yaw = r->pl.yaw; pl->speed = r->pl.speed; }
                if (((int)(logt * 10.f)) % 3 == 0) pressed |= GAME_SPRAY; break; } }
            if (!title_mode && autopilot == 2 && !L.gferr && L.ms.active && !L.ms.phase && fmodf(logt, 2.f) < dt) { for (int i = 0; i < L.gf.n; i++) { Spot *sp = &L.gf.spots[i]; if (sp->active && sp->required && !sp->tagged) { sp->tagged = 1; sp->yaw = pl->yaw; L.gf.tagged++; L.gf.score += 500; break; } } } }
        if(L.pause.active || L.ms.phase==MISSION_PHASE_EXIT || (L.failure.active && L.failure.phase>=3)){
            /* Pause suspends simulation tasks, not just the controller.
               The terminal failure prompt also owns input. Retain the
               last camera and player/mission/actor clocks. */
            memcpy(fwd_override,L.render_forward,sizeof fwd_override);use_fwd=1;
        } else if (gamemode) {
            if(autopilot==5 && lookat_live)dt=1.f/60.f;
            if (autopilot==6) { if(!realtime_profile)dt=1.f/60.f;pad.Lx=pad.Ly=128;pad.Buttons=pressed=0; }
            if(fixed_final_ticks && autopilot==6 && L.id==4 && !title_mode &&
               !sceneplay_resident(&L.scene) && !L.ms.phase)dt=(float)fixed_final_ticks/30.f;
            if(skateinputtest && autopilot==6 && !title_mode && !sceneplay_resident(&L.scene) && !L.ms.phase){
                /* Input-only effect check: walk, press/hold R, jump, release.
                   No supplied speed, pose, collision, effects or progress. */
                int before=(int)(skateinputclock*30.f);skateinputclock+=dt;int tick=(int)(skateinputclock*30.f);
                pad.Ly=tick<90 ? 0:128;
                if(tick>=20 && tick<65)pad.Buttons|=GAME_BOOST;
                if(before<20 && tick>=20)pressed|=GAME_BOOST;
                if(tick>=50 && tick<60)pad.Buttons|=PSP_CTRL_CROSS;
                if(before<50 && tick>=50)pressed|=PSP_CTRL_CROSS;
            }
            if(autopilot==6 && realtime_profile==2 && !sceneplay_resident(&L.scene) && !L.ms.phase) {
                /* Profiling input only: no placements, speed, state, pose,
                   collision or campaign overrides; use normal wall time. */
                int before=(int)(realtime_profile_clock*30.f);
                realtime_profile_clock+=dt;int tick=(int)(realtime_profile_clock*30.f),phase=tick%240;
                pad.Ly=phase<180 ? 0:128;pad.Lx=phase>=90 && phase<180 ? 205:128;
                if(tick/90!=before/90){pad.Buttons|=PSP_CTRL_CROSS;pressed|=PSP_CTRL_CROSS;}
            }
            if(lessoninputtest && lesson_started==1 && lesson_record!=L.mrec) {
                runtime_log("LESSON_INPUT handoff kind=%u set=%d error=%d writable=%d\n",L.mrec ? L.mrec->kind:99,L.set_idx,L.campaign_error,campaign_writable);
                dump_shot("shot_lesson_handoff.raw");lesson_started=2;
            }
            /* Diagnostic input source only: run the shipped demonstration
               controls through one normal attempt. Never place the actor,
               grant cans, or supply trial/result/progression state. */
            if(lessoninputtest && !lesson_started && L.mrec && L.mrec->kind!=6 &&
               !strcmp(L.mrec->name,"S1M11") && L.ms.active && !L.ms.phase) {
                const CtrlSub *cs=ctrl_sub(&L.ctl,L.set_idx);
                if(cs && cs->nstream){ctrl_play_start(&lesson_input,cs->streams);ctrl_play_tick(&lesson_input);
                    lesson_record=L.mrec;lesson_started=1;
                    runtime_log("LESSON_INPUT begin record=%d set=%d position=%.6f,%.6f,%.6f\n",(int)(L.mrec-missions),L.set_idx,pl->x,pl->y,pl->z);}
            }
            float render_dt=dt;
            int fixed_replay=!sceneplay_resident(&L.scene) && (L.demo_on || L.play.active || (lessoninputtest && lesson_input.active));
            unsigned replay_steps=fixed_replay ? ctrl_frame_steps(&L.replay_clock,dt):1;
            if(!fixed_replay)L.replay_clock.remainder=0;
            const Mission *replay_record=L.mrec;
            for(unsigned replay_step=0;replay_step<replay_steps;replay_step++) {
            if(fixed_replay)dt=1.f/30.f;
            const CtrlPlay *input_play=L.play.active ? &L.play:NULL;
            if(lessoninputtest && lesson_record==L.mrec && !L.ms.phase && lesson_input.active){input_play=&lesson_input;dt=1.f/30.f;}
            int turninputactive=turninputtest && frames>=400 && !L.ms.phase && !input_play;
            if(turninputactive)dt=1.f/30.f;
            if(finalprogresstest || spraytest==3 || spraytest==5 || (spraytest==4 && bus_event_large))dt=1.f/30.f;
            if(rivaldroptest && autopilot==6 && frames>1200 && !sceneplay_resident(&L.scene)) {
                /* Supplied arrival above a real authored bus tagger, then
                   normal falling/contact/AI. Never set contacts, drop state,
                   actor positions, pickup flags or campaign progress. */
                int candidate=0;
                if(!rival_drop_tick)for(int i=0;i<L.ngangs;i++) {
                    Gang *g=&L.gangs[i];if(g->kind!=rival_drop_kind || g->state!=2 || (g->flags&0x20000))continue;
                    if(candidate++!=rival_drop_index)continue;
                    float at[3]={g->pos[0],g->pos[1]+12.f,g->pos[2]};
                    player_reposition(pl,at);pl->yaw=g->m[0].yaw;player_cam_reset(pl);
                    /* A depleted-health fixture makes the authored recovery measurable. */
                    if(rival_drop_kind==20)pl->health=10;
                    rival_drop_tick=1;rival_drop_initial=L.gf.cancount;rival_drop_health=pl->health;rival_drop_count=g->nwp;
                    runtime_log("RIVAL_DROP_TEST begin actor=%d kind=%d inventory=%d health=%d maximum=%d count=%d writable=%d\n",i,g->kind,rival_drop_initial,pl->health,pl->health_max,rival_drop_count,campaign_writable);break;
                }
                if(rival_drop_tick>=80) {
                    int index=(rival_drop_tick-80)/30;
                    if(index<rival_drop_count && index!=rival_drop_pick) {
                        CanDrop *d=L.gf.can_drops;for(int i=0;d && i<index;i++)d=d->next;
                        if(d && d->flight.state==3) {
                            float at[3]={d->can.x,d->can.y,d->can.z};player_reposition(pl,at);player_cam_reset(pl);rival_drop_pick=index;
                        }
                    }
                }
            }
            /* Explicit isolated fixture: source Onishima record/descriptor,
               player placement and ONE R edge. No mission/tag/result flags
               are set, and no enemy transform or AI state is overwritten. */
            if((rivalroutetest==2 || rivalroutetest==4) && L.id>=1 && L.id<=3 && frames>=1200 && L.mrec && L.mrec->kind==3 && !sceneplay_resident(&L.scene) && L.nrv==3) {
                /* Tag fixture supplies only player placement, can budget and
                   spray edges. Every moving GF volume/counter stays live. */
                if(rivalroute_phase<0)rivalroute_phase=0;
                static int supplied_member=-1;
                int completed=0;
                for(int i=0;i<3;i++) {
                    struct Rival *r=L.rv+i;GfoObject *o=gfo_find(&L.object_graffiti,r->graffiti);
                    if(r->tagged){completed++;continue;}
                    if(completed!=i || !o)continue;
                    if(supplied_member!=i){L.gf.cancount=20;supplied_member=i;}
                    float point[3]={0};for(int j=0;j<6;j++)for(int k=0;k<3;k++)point[k]+=o->volume[j*3+k]/6.f;
                    player_reposition(pl,point);pl->yaw=r->pl.yaw;player_cam_reset(pl);
                    if(frames%6==0){pad.Buttons|=GAME_SPRAY;pressed|=GAME_SPRAY;}
                }
                if(frames%12==0)for(int i=0;i<3;i++) {
                    struct Rival *r=L.rv+i;GfoObject *o=gfo_find(&L.object_graffiti,r->graffiti);
                    if(o)runtime_log("POISON_TAG member=%d tick=%u started=%d tagged=%d reveal=%u complete=%u cans=%d xyz=%.9g,%.9g,%.9g writable=%d\n",
                        i,r->pl.logic_tick,r->started,r->tagged,o->progress.reveal,o->progress.complete,L.gf.cancount,r->pl.x,r->pl.y,r->pl.z,campaign_writable);
                }
                if(completed==3 && ++rivalroute_phase==30) {
                    runtime_log("POISON_TAG complete all three live tags cans=%d phase=%d writable=%d\n",L.gf.cancount,L.ms.phase,campaign_writable);
                    dump_shot("shot_poison_tags_complete.raw");
                }
            }
            if(objecttagtest && autopilot==6 && L.mission_set_bank==1 && frames>1200) {
                if(!objecttag_phase) {
                    unsigned parent=group_parent_spawn(&L,2,0);
                    objecttag_handle=group_parent_graffiti(&L,parent,0);objecttag_phase=1;
                }
                GfoObject *o=gfo_find(&L.object_graffiti,objecttag_handle);
                if(o && o->registered && objecttag_phase<3) {
                    float point[3]={0};for(int k=0;k<3;k++)for(int j=0;j<6;j++)point[k]+=o->volume[j*3+k]/6.f;
                    player_reposition(pl,point);
                    for(int j=0;j<L.ngangs;j++)if(L.gangs[j].graffiti_handle==objecttag_handle)pl->yaw=L.gangs[j].m[0].yaw;
                    player_cam_reset(pl);
                    if(L.object_graffiti.target==objecttag_handle){pressed|=GAME_SPRAY;objecttag_phase=3;}
                    else objecttag_phase=2;
                }
            }
            if(finalfalltest==1 && L.final.loaded && logframes>=60) {
                const float fixture[3]={1200,-101,1200};player_reposition(pl,fixture);finalfalltest=2;
            }
            if(finalcollapsetest==1 && L.final.loaded && logframes>=60) {
                for(int i=0;i<4;i++)finalarena_inspect_tag(&L.final,i);
                finalcollapsetest=2;
            }
            if(finalformtest==1 && L.final.loaded && finalarena_inspect_finalform(&L.final)==0)
                finalformtest=2;
            if(eventinspect>=0) {
                level_retire_gameplay_resources(&L);audio_free_hud_voice();
                int result=sceneplay_event_begin(&L.scene,eventinspect,CHAR_ID_OF_MODEL[pchar&15],eventinspect_mode,0);
                runtime_log("EVENT_INSPECTION id=%d mode=%d load=%d\n",eventinspect,eventinspect_mode,result);
                if(result>0){L.scene.vm.ctx=&L;L.scene.vm.command=scene_command;}
                eventinspect=-1;
            }
            if(L.ms.scene_preload>=0){int id=L.ms.scene_preload,result=level_scene_begin(&L,id,1);L.ms.scene_preload=-1;
                if(result<0){L.campaign_error=8;runtime_log("CAMPAIGN event preload failed EVENT=%d result=%d\n",id,result);}}
            if(L.ms.scene_request>=0){int id=L.ms.scene_request,result=level_scene_begin(&L,id,0);L.ms.scene_request=-1;
                if(result<0){L.campaign_error=8;runtime_log("CAMPAIGN event load failed EVENT=%d result=%d\n",id,result);}else if(result>0)L.ms.nsub=0;}
            if(!cherr)player_bind_animation(pl,&chars,pchar);
            level_final_progress(&L,dt);
            /* Explicit result fixture: inject a completed lesson outcome,
               NOT movement or an earned clear. Never enabled in a package. */
            if(recruitresulttest>0 && recruitresulttest!=12 && pl->logic_tick>30 && !sceneplay_resident(&L.scene) &&
               L.mrec && level_result_scene_id(&L)==recruitresulttest && !L.ms.phase) {
                if(L.mrec->kind==5)mission_request_clear(&L.ms);else mission_trial_result(&L.ms,&L.gf,1);recruitresulttest=-recruitresulttest;
                if(logfd>=0)runtime_log_write(logfd,"RECRUIT_RESULT injected\n",24);
            }
            if((recruitresulttest==12 || recruitresulttest==-56) && frames>900 &&
               L.mrec && L.mrec->mid==15 && mission_word(&L.ms,0x18)==28 &&
               !L.ms.phase && !sceneplay_resident(&L.scene) && ++recruit_handoff_frames>90) {
                /* S1M07 record 2's +40 rival list is empty. Its +3c
                   threshold is ONE tag in area 1 (required index 11).
                   Reuse the small-tag fixture: placement/can supplied,
                   ordinary R edge, original reveal and mission script. */
                spraytest=1;L.gf.cancount=1;recruitresulttest=-12;
                if(logfd>=0)runtime_log_write(logfd,"RECRUIT_TARGET input fixture\n",29);
            }
            if(recruitinspect>0 && L.id==1 && frames>900 && !sceneplay_resident(&L.scene)) {
                int result=sceneplay_recruit_begin(&L.scene,recruitinspect,CHAR_ID_OF_MODEL[pchar&15],recruitinspect==13 ? 2:3,0);
                if(logfd>=0){char b[80];runtime_log_write(logfd,b,snprintf(b,sizeof b,"RECRUIT_INSPECT begin=%d\n",result));}
                recruitinspect=-recruitinspect;
            }
            int scene_was_resident=sceneplay_resident(&L.scene);
            scene_frozen=sceneplay_tick(&L.scene,pl,dt);
            int scene_retired=scene_was_resident && !sceneplay_resident(&L.scene);
            /* Release the worker's reserve before the spray mesh/HUD try
               to allocate. A shortened scene may still have a pending read. */
            if(scene_retired)level_collect_gameplay_audio(&L);
            if(scene_frozen && L.mrec) {
                /* Scene actor placement is not a skated boundary crossing. */
                const float position[3]={pl->x,pl->y,pl->z};
                stage_exit_reset(&L.stage_exit,L.mrec->st,L.mrec->mid,position);
                memcpy(L.ms.quad_previous,position,sizeof position);
            }
            /* Intro EVENT/actor/voice banks occupy the same 32-MiB heap.
               Do not pin a failed can-bank load at the intro memory peak:
               load it after the intro releases its temporary resources. */
            if(!cherr && !sceneplay_resident(&L.scene))bind_spray_character(pl,pchar,L.final.loaded);
            if(scene_frozen && L.scene.vm.phase==0x14 && L.scene.transferred && logfd>=0) {
                char sb[240];int n=snprintf(sb,sizeof sb,"SCENE id=%d phase=%d clock=%.0f vm_error=%d audio_loaded=%d voice_serial=%u voice=%d audio_error=%d transferred=%d\n",
                    L.scene.vm.id,L.scene.vm.phase,L.scene.vm.clock,L.scene.vm.error,L.scene.audio_loaded,L.scene.voice_serial,L.scene.voice_ref,L.scene.audio_error,L.scene.transferred);
                runtime_log_write(logfd,sb,n);
            }
            if(scene_frozen && L.final.loaded)finalarena_scene_update(&L.final,dt);
            if(!scene_frozen && !L.campaign_error) {
            if(pickuproadblocktest && autopilot==6 && L.id==1 && (pickuproadblocktest==2 || frames>1200) && pickuproadblocktick<480) {
                /* Isolated visual/contact inspection: supplied placements,
                   camera and initial HP only. Real pickup/animation tasks;
                   no collected flags, awarded health or campaign writes. */
                int phase=pickuproadblocktick/120,age=pickuproadblocktick%120;
                float target[3];
                if(pickuproadblocktest==2) {
                    /* Supplied encounter activation/camera only: original
                       Love Trap record 10, live squad/vehicle/gas/audio tasks. */
                    const float positions[4][3]={{1390,0,110},{1270,94,-620},{1168,94,-680},{1315,94,-613}};
                    memcpy(target,positions[phase],12);
                    if(!pickuproadblocktick) {
                        /* A shortcut must retire the earlier encounters as
                           normal progression does before this roadblock. */
                        for(unsigned i=0;i<64;i++)if(L.group_parent[i].active)group_parent_delete(&L,L.group_parent[i].handle);
                        group_parent_spawn(&L,10,0);
                    }
                    pl->x=target[0];pl->y=target[1];pl->z=target[2]-75.f;
                    camtest[0]=target[0]+75.f;camtest[1]=target[1]+40.f;camtest[2]=target[2]-105.f;
                    if(phase==0){camtest[0]=1480;camtest[2]=190;}
                    if(phase==3){camtest[0]=1250;camtest[2]=-660;}
                    target[1]+=12.f;
                } else if(phase<2) {
                    if(!pickuproadblocktick){npcs_set_list(&L.npcs,1004,1);pl->health=10;}
                    const float positions[2][3]={{1770,0,1240},{1310,0,2060}};
                    memcpy(target,positions[phase],12);
                    pl->x=target[0];pl->y=target[1];pl->z=target[2]-100.f;
                    camtest[0]=target[0]+(phase ? -90.f:100.f);camtest[1]=target[1]+70.f;camtest[2]=target[2]-110.f;
                    target[1]+=12.f;
                } else {
                    const Can *c=NULL;for(int i=0;i<L.gf.ncans;i++)if(L.gf.cans[i].set==(unsigned)L.set_idx && L.gf.cans[i].kind==(unsigned)phase){c=L.gf.cans+i;break;}
                    if(c){
                        memcpy(target,&c->x,12);
                        pl->x=c->x+(age<90 ? 25.f:0.f);pl->y=c->y;pl->z=c->z;
                        camtest[0]=c->x+40.f;camtest[1]=c->y+15.f;camtest[2]=c->z-65.f;
                    }else memcpy(target,&pl->x,12);
                }
                memcpy(camtest+3,target,12);camtest[6]=536.f;camtest_on=1;L.dcam_on=0;
                pl->speed=pl->vy=0;pl->grinding=0;pl->state=1;pl->onground=1;
                pad.Lx=pad.Ly=128;pad.Buttons=pressed=0;pickuproadblocktick++;
            }
            if(L.final.loaded && L.final.error)L.hferr=-1;
            if (movementtest) {
                dt=1.f/60.f;pad.Lx=pad.Ly=128;pad.Buttons=pressed=0;L.play.active=0;
                if (!movementframe) {
                    player_trace_fixture(pl,movementtest!=1);
                }
                movementframe++;
            }
{ if (logframes < 3) { char m[8]; runtime_log_write(logfd, m, snprintf(m, 8, "A\n")); } }
            if (!L.gferr) {
                if(finalprogresstest)finalfixture_input(&finalfixture,&L.final,&pad,&pressed);
                if(policegrabtest && police_grab_first && police_grab_tick>police_grab_first+80) {
                    /* Supplied stick/jump inputs only; live grabbing and
                       release conditions remain in the ordinary controllers. */
                    pad.Ly=0;
                    if(police_grab_tick%30<8)pad.Buttons|=PSP_CTRL_CROSS;
                    if(police_grab_tick%30==0)pressed|=PSP_CTRL_CROSS;
                }
                if(spraytest==3 && autopilot==6 && frames>1200)citysprayfixture_input(&cityspray,&L.gf,pl,&pad,&pressed);
                /* S3M02 regression: supplied positions/cans and real spray
                   input only. Both arrivals must come from the untouched
                   JMDc tag thresholds, GRP1 sentinels and object-flag lists. */
                int arrival_tagging=0;
                if(spraytest==5 && autopilot==6 && frames>1200 && L.mrec && !strcmp(L.mrec->name,"S3M02")) {
                    float anchor[3]={1402.031372f,19.232038f,-285.932953f};
                    if(arrival_area){anchor[0]=395.471436f;anchor[2]=137.890106f;}
                    if(!arrival_started){player_reposition(pl,anchor);arrival_started=1;}
                    arrival_tagging=L.gf.tagged<3 || ((arrival_scenes&1) && L.gf.tagged<11);
                    if(!arrival_tagging && !pl->spray_active)player_reposition(pl,anchor);
                    if(arrival_tagging && spraytest_phase==3 && !pl->spray_active && ++bus_event_wait>=30) {
                        spraytest_phase=0;spraytest_ticks=0;bus_event_wait=0;
                    }
                    if(arrival_tagging && bus_event_large) {
                        if(cityspray.done && !pl->spray_active && ++bus_event_wait>=90) {
                            int next=cityspray.done<0 ? cityspray.target+1:0;
                            cityspray=(CitySprayFixture){.target=-1,.search_from=next};bus_event_wait=0;
                        }
                        citysprayfixture_input(&cityspray,&L.gf,pl,&pad,&pressed);
                    }
                }
                /* Opt-in EBOOT input regression. One fixture placement and
                   one R edge, then return to the spawn while the real tag
                   task finishes. Never sets tagged/progress/result flags. */
                /* Seven actual tag inputs exercise JMDc tag 1/4's
                   preparation, tag 3/6's EVENT and spraying after cleanup.
                   Only positions/cans/input are supplied, never tag flags. */
                if(spraytest==4 && !bus_event_large && spraytest_phase==3 && L.gf.tagged<7 && !pl->spray_active && ++bus_event_wait>=90){
                    spraytest_phase=0;spraytest_ticks=0;bus_event_wait=0;
                }
                if((spraytest==1 || spraytest==2 || (spraytest==4 && !bus_event_large) || (spraytest==5 && arrival_tagging && !bus_event_large)) && autopilot==6 && frames>1200 && !spraytest_phase) {
                    for(int si=0;si<L.gf.n;si++) {
                        Spot *sp=L.gf.spots+si;
                        if(!sp->active || !sp->required || !sp->source_data || sp->nstrokes || !sp->hasvol || sp->tagged)continue;
                        for(int k=0;k<3;k++){spraytest_point[k]=0;for(int q=0;q<6;q++)spraytest_point[k]+=sp->pts[q*3+k]/6.f;}
                        int inside=1;
                        for(int q=0;q<6;q++)if((spraytest_point[0]-sp->pts[q*3])*sp->nrm[q*3]+(spraytest_point[1]-sp->pts[q*3+1])*sp->nrm[q*3+1]+(spraytest_point[2]-sp->pts[q*3+2])*sp->nrm[q*3+2]>0)inside=0;
                        if(!inside)continue;
                        spraytest_origin[0]=pl->x;spraytest_origin[1]=pl->y;spraytest_origin[2]=pl->z;
                        if(!sp->source)spraytest_point[1]-=7.6f;
                        spraytest_spot=si;spraytest_phase=1;
                        /* Explicit test resource, not earned inventory.
                           Normal 018fb0 mission spawns start with zero. */
                        if(L.gf.cancount<1)L.gf.cancount=1;
                        runtime_log("SPRAY_TEST supplied_can=1 target=%d\n",si);
                        if(recruitresulttest==-12 && logfd>=0){char b[80];int n=snprintf(b,sizeof b,"RECRUIT_TARGET area=%d index=%d\n",sp->area,sp->idx);runtime_log_write(logfd,b,n);}
                        pressed|=GAME_SPRAY;break;
                    }
                    if(spraytest==5 && !spraytest_phase)bus_event_large=1;
                }
                if(spraytest==4 && !spraytest_phase && L.gf.tagged>=4)bus_event_large=1;
                if(spraytest==4 && bus_event_large && L.gf.tagged<7) {
                    if(cityspray.done==1 && !pl->spray_active && ++bus_event_wait>=90){
                        cityspray=(CitySprayFixture){.target=-1};bus_event_wait=0;
                    } else if(cityspray.done==-2 && !pl->spray_active && ++bus_event_wait>=90) {
                        /* Live police can interrupt this input fixture now.
                           Try another authored volume, without completing the
                           interrupted tag or suppressing police contacts. */
                        int next=cityspray.target+1;
                        runtime_log("BUS_EVENT_RETRY interrupted=%d grips=%d player=%d next=%d\n",cityspray.target,police_grip_count(&pl->police_grips),pl->state,next);
                        cityspray=(CitySprayFixture){.target=-1,.search_from=next};bus_event_wait=0;
                    }
                    citysprayfixture_input(&cityspray,&L.gf,pl,&pad,&pressed);
                }
                if(spraytest_phase==1) {pl->x=spraytest_point[0];pl->y=spraytest_point[1];pl->z=spraytest_point[2];pl->speed=pl->vy=0;}
                float spray_x, spray_y; player_input_axes(&pad, &spray_x, &spray_y, 1);
                int recorded_paint=recruit_demo_active(&L);
                int recorded_spray_edge=input_play && ((input_play->cur[1]&~input_play->prev)&4);
                L.gf.actor=recorded_paint ? recruit_paint_actor:player_spray_actor;L.gf.actor_context=pl;
                L.gf.hud_sine=player_fsca_sin;
                L.gf.hud_player_state=pl->state;
                L.gf.block_large=recorded_paint ?
                    recruit_paint_block_large(pl,recorded_spray_edge,police_grip_count(&pl->police_grips)):
                    pl->state!=1 || police_grip_count(&pl->police_grips);
                int can_area=L.zerr ? 0:zone_area(&L.zones,L.cur_zone);
                graffiti_cans_select(&L.gf,L.set_idx,(unsigned)can_area<5 ? 1u<<can_area:0);
                gfx_profile_logic(LOGIC_CANS);
                L.gf.can_area=L.cur_zone>=0 && !L.zerr ? zone_area(&L.zones,L.cur_zone):0;
                canplay_update(&L.gf,pl,cans_err ? NULL:&cans,dt);
                gfx_profile_logic(LOGIC_OTHER);
                hang_mark(HANG_GRAFFITI);
                if(input_play) {
                    unsigned edges=input_play->cur[1]&~input_play->prev;
                    /* Spray reads 027788/0277a0 directly. Locomotion's
                       mirrored X vector is not the recorded gesture. */
                    spraying=graffiti_update_input(&L.gf,pl->x,pl->y,pl->z,pl->yaw,
                        !!(edges&4) && !police_grip_count(&pl->police_grips) && (!L.object_graffiti.target || L.gf.mg_active),!!(edges&1),(int)(signed char)input_play->cur[2]*256,
                        input_play->cur[3]*(1.f/255.f),CHAR_ID_OF_MODEL[pchar&15],dt);
                    if(recorded_paint)recruit_paint_after_input(&L.gf,pl);
                } else spraying = graffiti_update(&L.gf, pl->x, pl->y, pl->z, pl->yaw,
                    (pressed & GAME_SPRAY) != 0 && !police_grip_count(&pl->police_grips) && (!L.object_graffiti.target || L.gf.mg_active) &&
                    (L.gf.mg_active || (pl->state!=7 && pl->state!=9 && pl->health>0)), (pressed & PSP_CTRL_CROSS) != 0,
                    spray_x, spray_y, CHAR_ID_OF_MODEL[pchar&15], dt);
                /* Distinct source calls: small-tag press 01abb8 -> 08e7a8;
                   deferred reveal 03b828 -> 08e7f0; minigame completion
                   03bfa8 -> 08e760. Minigame ENTRY does not advance a trial. */
                if(L.gf.events&GRAFFITI_DEFER) {
                    level_trial_event(&L,TRIAL_TAG_DEFER);
                    runtime_log("SPRAY_ACCEPT record=%d set=%d replay=%d clock=%u cans=%d tagged=%d pos=%.3f,%.3f,%.3f\n",L.mrec ? (int)(L.mrec-missions):-1,L.set_idx,L.play.active,pl->logic_tick,L.gf.cancount,L.gf.tagged,pl->x,pl->y,pl->z);
                }
                if(L.gf.events&GRAFFITI_TAG)level_trial_event(&L,TRIAL_TAG);
                if(L.gf.events&GRAFFITI_FINISH)level_trial_event(&L,TRIAL_FINISH);
                if(L.gf.events&GRAFFITI_RIVAL_FINISH) {
                    L.ms.f40=1; /* original 03b8a8 -> 0186b4(1) */
                    runtime_log("CHALLENGE_RIVAL tag finished name=%s\n",L.mrec->name);
                    if(rivalroutetest==3){rivalroute_phase=2;runtime_log("CHALLENGE_RIVAL complete name=%s rival-tag loss writable=%d\n",L.mrec->name,campaign_writable);dump_shot("shot_rival_tag_complete.raw");}
                }
                if(spraytest_phase==1 && L.gf.spots[spraytest_spot].revealing) {
                    spraytest_phase=2;
                    if(spraytest!=2)player_reposition(pl,spraytest_origin);
                }
                if(spraytest_phase==2) {
                    Spot *sp=L.gf.spots+spraytest_spot;spraytest_ticks++;
                    if(logfd>=0){char b[160];runtime_log_write(logfd,b,snprintf(b,sizeof b,"SPRAY_TEST frame=%d spot=%d reveal=%u tagged=%d total=%d cans=%d events=%u\n",spraytest_ticks,spraytest_spot,(unsigned)sp->reveal,sp->tagged,L.gf.tagged,L.gf.cancount,L.gf.events));}
                    if(sp->tagged)spraytest_phase=3;
                }
            }
            SceCtrlData ppad = pad; unsigned ppressed = pressed; float pax = 0, pay = 0;
            player_set_character(pl, CHAR_ID_OF_MODEL[pchar & 15]);
            /* EVENT publishes its handoff one callback before freeing its
               assets. Wait for cleanup before rebinding gameplay audio. */
            int audio_ready=!L.demo_on && !L.ms.phase && !L.result_scene && !sceneplay_resident(&L.scene);
            hang_mark(HANG_WORLD_BIND);
            /* Complete loading after EVENT cleanup, before player/enemy
               ticks. The same path handles missions without an intro. */
            if(audio_ready && !level_prepare_gameplay_resources(&L))continue;
            /* Traffic tasks exist before the intro, but their gameplay
               renderer is not sampled until its cleanup. Keep the source
               data/owners; defer only the separate render-bank allocation. */
            if(!sceneplay_resident(&L.scene))traffic_load_models(&L);
            if(pl->camera_tick!=player_follow_tick || pl->camera_context!=&L.follow)
                player_follow_bind(&L.follow,pl,&L.col);
            if (!cherr) player_bind_animation(pl,&chars,pchar);
            if (input_play) {   /* recorded input replaces the pad (controller task mode 4): raw stick from dir/mag, 1 = jump, 4 = spray, 8 = dash */
                float ax, ay; unsigned b; ctrl_play_input(input_play, &ax, &ay, &b); pax = ax; pay = ay;
                ppad.Lx = 128 + (int)(ax * 127.f); ppad.Ly = 128 + (int)(ay * 127.f);
                ppad.Buttons = (b & 1 ? PSP_CTRL_CROSS : 0) | (b & 4 ? GAME_SPRAY : 0) | (b & 8 ? GAME_BOOST : 0);   /* controller byte: 1 A jump, 2 B, 4 L spray, 8 R dash (FUN_8c027c74) */
                unsigned pb = (input_play->prev & 1 ? PSP_CTRL_CROSS : 0) | (input_play->prev & 4 ? GAME_SPRAY : 0) | (input_play->prev & 8 ? GAME_BOOST : 0); ppressed = ppad.Buttons & ~pb;
            }
            if(ambient_audio_test && frames>=1200 && !L.ms.phase && !sceneplay_resident(&L.scene)){
                /* Supply only a stationary listener placement near an
                   authored actor. Do not set its timer, state or sound. */
                if(ambient_audio_index<0)for(int i=0;i<L.npcs.count;i++){
                    Ped *p=L.npcs.p+i;
                    if(p->active && p->list<1000 && p->type<43 && p->nwp==1 &&
                       (p->route==10 || p->route==12 || p->route==13)){
                        float point[3]={p->x,p->y,p->z-20};player_reposition(pl,point);pl->speed=pl->vy=0;pl->yaw=0;player_cam_reset(pl);
                        ambient_audio_index=i;
                        runtime_log("AMBIENT_FIXTURE index=%d type=%d route=%d clock=%u\n",i,p->type,p->route,(unsigned)p->ambient_clock);break;
                    }
                }
                if(ambient_audio_index>=0 && !(ambient_audio_ticks++%60)){
                    Ped *p=L.npcs.p+ambient_audio_index;
                    runtime_log("AMBIENT_CLOCK clock=%u depth=%.6f active=%d sound=%u\n",(unsigned)p->ambient_clock,p->audio_depth,p->active,p->sound);
                }
            }
            if(world_audio_test && frames>=1200 && L.traffic && L.traffic->world.clock && !L.ms.phase && !sceneplay_resident(&L.scene)){
                /* Explicit audio integration placements, not earned travel:
                   no sound call, wedge flag or NPC reaction is injected. */
                if(world_audio_test_ticks==0){
                    const PathInstance *p=&L.traffic->world.slots[L.traffic->world.order[0]];
                    int yaw=p->task.rotation[1];float point[3]={p->task.pos[0]+50*player_fsca_sin(yaw),
                        p->task.pos[1],p->task.pos[2]+50*player_fsca_sin(yaw+16384)};
                    player_reposition(pl,point);pl->speed=pl->vy=0;
                    runtime_log("WORLD_FIXTURE traffic program=%d task=%u\n",p->sound_program,p->task.handle);
                } else if(world_audio_test_ticks==90){
                    for(int i=0;i<L.npcs.count;i++){Ped *p=L.npcs.p+i;
                        if(p->active && p->list<1000 && p->type<43 && p->knocked<=0){
                            float point[3]={p->x,p->y,p->z-5};player_reposition(pl,point);pl->speed=4;pl->vy=0;
                            runtime_log("WORLD_FIXTURE npc type=%d index=%d speed=4\n",p->type,i);break;
                        }
                    }
                }
                world_audio_test_ticks++;
            }
            if(passagefixture.enabled && L.id==passagefixture.enabled && frames>=1200 && !L.ms.phase && !sceneplay_resident(&L.scene) && !passagefixture.started)
                if(passage_fixture_start(&passagefixture,pl)<0)
                    runtime_log("PASSAGE_INPUT disabled=1 stage=%d reason=missing_or_invalid_waypoints\n",L.id);
            if(openingtest && (L.gates || L.openings) && frames>=1200 && !L.ms.phase && !sceneplay_resident(&L.scene) && !opening_started){
                const float positions[7][4]={{1647.2f,-11.5f,-145.9f,5.325f},{1184.626f,-163.3f,-153,0},
                    {1233,12,-518.8f,3.2405f},{136.5f,20.2f,-90,0},{125,92,-92,0},{1467.18f,-44.2f,-354.4f,.214f},
                    {715,0,66.6f,1.57079633f}};
                player_reposition(pl,positions[openingtest-1]);pl->yaw=positions[openingtest-1][3];
                player_cam_reset(pl);opening_started=1;opening_tick=pl->logic_tick;
            }
            if(traintest && L.trains && frames>=1200 && !L.ms.phase && !sceneplay_resident(&L.scene) && !train_started){
                const float benten[3]={340,-29.8f,-300.2f},bantam[3]={-7,300,-900};
                player_reposition(pl,L.id==5 ? bantam:benten);pl->yaw=L.id==5 ? -1.57079633f:1.57079633f;
                player_cam_reset(pl);train_started=1;train_tick=pl->logic_tick;
            }
            if(koganehousetest && L.house && frames>=1200 && !L.ms.phase && !sceneplay_resident(&L.scene) && !house_started){
                /* Supplied arrival only: ordinary input/collision earns every
                   door contact and wall break. No prop state is injected. */
                const float positions[4][4]={{-275,32,-566,1.445f},{-145,164,130,0},{-235,69,-94,1.445f},{224,-318,530,0}};
                player_reposition(pl,positions[koganehousetest-1]);
                pl->yaw=positions[koganehousetest-1][3];player_cam_reset(pl);house_started=1;house_tick=pl->logic_tick;
            }
            if(water_test && L.id==2 && frames>=1200 && !L.ms.phase && !sceneplay_resident(&L.scene) &&
               (water_test_case==0 || (water_test_case==1 && water_test_ticks>=180) || (water_test_case==2 && water_test_ticks>=360))) {
                const float drops[3][3]={{200,20,-100},{550,20,180},{-700,20,250}};
                player_reposition(pl,drops[water_test_case]);pl->yaw=-1.5707963268f;player_cam_reset(pl);water_test_case++;
            }
            pl->spray_hint_ready=0;
            if(rivalroutetest==1 && L.id>=1 && L.id<=3 && frames>=1200 && L.mrec && L.mrec->kind==3 && !L.ms.phase && !sceneplay_resident(&L.scene) && L.nrv==3 && rivalroute_phase<3){
                /* Some Love Attack / Behind the Mask groups span more than 100 units.
                   Supply an approach to any still-waiting member, leaving
                   the controller's own activation and movement untouched. */
                int mask_route=L.mrec && !strcmp(L.mrec->name,"S3M05");
                if((L.id==1 || mask_route) && rivalroute_phase>=0 && frames%240==0)for(int i=0;i<3;i++) {
                    struct Rival *r=&L.rv[i];
                    if(r->active && !r->started && r->seg==rivalroute_phase) {
                        const float position[3]={r->pl.x,r->pl.y,r->pl.z+40.f};
                        player_reposition(pl,position);player_cam_reset(pl);break;
                    }
                }
                int next=(rivalroute_phase+1)%3,ready=1;
                for(int i=0;i<3;i++)if(!L.rv[i].active || L.rv[i].started || L.rv[i].seg!=next)ready=0;
                if(ready){
                    if(rivalroute_phase==2){rivalroute_phase=3;runtime_log("POISON_ROUTE complete all nine segments and return-to-start writable=%d\n",campaign_writable);dump_shot("shot_routes_complete.raw");}
                    else {const float positions[3][3][3]={{{-400,493,-1380},{-174,207,-515},{-195,100,-130}},
                            {{226.43286f,-415,619.736f},{-39,-325,920},{-20,-320,1540}},
                            {{-80,94.5f,-699},{-528,104.5f,-1072},{618,74.5f,-1071}}};
                        const float mask_positions[3][3]={{1340,0.6806788444519043f,-720},{970,-70.1910629272461f,-950},{1340,-70.19103240966797f,-1050}};
                        rivalroute_phase++;player_reposition(pl,mask_route ? mask_positions[rivalroute_phase]:positions[L.id-1][rivalroute_phase]);
                        pl->yaw=3.14159265f;player_cam_reset(pl);
                        runtime_log("POISON_ROUTE approach segment=%d writable=%d\n",rivalroute_phase,campaign_writable);
                    }
                }
            }
            if(rivalroutetest==3 && frames>=1200 && !L.ms.phase && !sceneplay_resident(&L.scene) && L.nrv==1 && rivalroute_phase<2) {
                struct Rival *r=&L.rv[0];
                /* Observation fixture: keep the player with the rival,
                   matching the source capture, under normal mission rules. */
                if(r->started) {
                    const float pos[3]={r->pl.x-sinf(r->pl.yaw)*60.f,r->pl.y,r->pl.z-cosf(r->pl.yaw)*60.f};
                    pl->x=pl->px=pos[0];pl->y=pl->py=pos[1];pl->z=pl->pz=pos[2];
                    pl->speed=pl->vy=0;player_cam_reset(pl);
                    rivalroute_phase=r->seg;
                }
                else if(r->active && r->seg==(rivalroute_phase+1)%2) {
                    if(rivalroute_phase==1) {
                        rivalroute_phase=2;
                        runtime_log("CHALLENGE_RIVAL complete name=%s two sections and return-to-start writable=%d\n",L.mrec->name,campaign_writable);
                        dump_shot("shot_routes_complete.raw");
                    } else {
                        const CtrlSub *set=ctrl_sub(&L.ctl,L.set_idx);
                        const CtrlStream *stream=set->streams+set->segs[0][r->seg].stream;
                        float yaw=(stream->yaw+32768)*(6.283185307f/65536.f);
                        const float pos[3]={stream->x-sinf(yaw)*30.f,stream->y,stream->z-cosf(yaw)*30.f};
                        player_reposition(pl,pos);pl->speed=pl->vy=0;player_cam_reset(pl);
                        rivalroute_phase=r->seg;
                        runtime_log("CHALLENGE_RIVAL approach section=%d writable=%d\n",r->seg,campaign_writable);
                    }
                }
            }
            if(sewerwatertest && L.id==2 && frames>=1200 && !L.ms.phase && !sceneplay_resident(&L.scene) && !sewerwater_started){
                /* Supplied arrival; ordinary forward input earns water
                   contacts, sounds and splashes. Never sets an effect. */
                const float position[3]={50,-322,1536};player_reposition(pl,position);
                pl->yaw=1.5707963268f;player_cam_reset(pl);sewerwater_started=1;sewerwater_tick=pl->logic_tick;
            }
            if(!L.gferr && L.gf.cancount>0 && !L.gf.mg_active) {
                GfoObject *target=gfo_find(&L.object_graffiti,L.object_graffiti.target);
                if(target && !target->progress.complete && !target->progress.busy) {
                    pl->spray_hint_ready=1;pl->spray_hint_kind=target->tpl->kind;
                } else if(!L.object_graffiti.target && L.gf.near>=0 && L.gf.near<L.gf.n) {
                    const Spot *target=L.gf.spots+L.gf.near;
                    if(target->active && !target->complete && !target->revealing) {
                        pl->spray_hint_ready=1;pl->spray_hint_kind=target->size;
                    }
                }
            }
            {const float position[3]={pl->x,pl->y,pl->z};
                hang_player_context(pl->logic_tick,L.cur_zone,pl->state,position);}
            gfx_profile_logic(LOGIC_PLAYER);
            if(L.demo_on) {
                if(L.demo_tick_n>=(int)L.title_route.count){L.demo_tick_n=0;memset(&pl->effects,0,sizeof pl->effects);bgm_play(0);}
                if(pl->before_tick)pl->before_tick(pl->before_tick_context);
                title_apply(&L,(unsigned)L.demo_tick_n,0);
                if(!L.campaign_error){
                    {unsigned phase=hang_mark(HANG_STREET_PROP_CONTACTS);
                        street_props_contacts(L.street_props,pl);hang_mark(phase);}
                    if(L.title_sample.tick==1)memset(&pl->effects,0,sizeof pl->effects);
                    player_effects_tick(pl,&L.title_sample.effects);
                    if(autopilot){
                        const SkateEffects *fx=&pl->effects;int sparks=0;
                        for(int k=0;k<24;k++)sparks+=fx->dash_sparks[k].active;
                        runtime_log("TITLE_FX row=%d flags=%x grind=%d%d dash=%d sparks=%d trail=%d,%d\n",
                            L.demo_tick_n,L.title_sample.effects.flags,fx->grind[0].active,fx->grind[1].active,
                            fx->dash_age,sparks,fx->trail[0].count,fx->trail[1].count);
                    }
                }
            }
            else if(recruit_demo_active(&L)) {
                if(pl->before_tick)pl->before_tick(pl->before_tick_context);
                recruit_demo_apply(&L,(unsigned)L.recruit_demo_tick++,0);
                if(!L.campaign_error){
                    {unsigned phase=hang_mark(HANG_STREET_PROP_CONTACTS);
                        street_props_contacts(L.street_props,pl);hang_mark(phase);}
                    recruit_audio_tick(&L.recruit_audio,L.recruit_demo,L.title_sample.tick,pl->sound_event,pl->sound_context);
                    if(pl->sound_spatial)pl->sound_spatial(pl->sound_context);
                }
                if(!L.campaign_error)player_effects_tick(pl,&L.title_sample.effects);
            }
            else if(pl->spray_active || L.gf.actor_ticks) { /* graffiti's 30 Hz callback owns actor/world/pose for this tick */ }
            else if (L.stun > 0.f) { L.stun -= dt; SceCtrlData still = ppad; still.Lx = still.Ly = 128; player_update(pl, &L.col, &still, 0, L.cam_yaw, dt); }
            else if (spraying) { SceCtrlData still = ppad; still.Lx = still.Ly = 128;
                /* The digital fallback must not also steer locomotion while
                   those directions are being consumed by the spray task. */
                still.Buttons &= ~(PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT|GAME_BOOST|PSP_CTRL_CROSS);
                player_update(pl, &L.col, &still, 0, L.cam_yaw, dt); }
            else if(passagefixture.started)passage_fixture_update(&passagefixture,pl,&L.col,L.cam_yaw,dt);
            else if(water_test_case)player_update_axes(pl,&L.col,0,0,0,0,L.cam_yaw,dt);
            else if(turninputactive){
                /* Input-only test: normal spawn, follow camera and collision.
                   No injected speed, brake, pose, HUD event or score. */
                float y=turninputticks>=80 ? 0:turninputticks>=40 && turninputticks<42 ? 1:-1;
                player_update_axes(pl,&L.col,0,y,0,0,L.cam_yaw,dt);
                if(turninputticks<=85)runtime_log("TURN_INPUT tick=%d state=%d motion=%d frame=%.9g mirror=%d speed=%.9g brake=%d lock=%d delay=%d marker=%d award=%d writable=%d\n",
                    turninputticks,pl->state,pl->logical_motion,pl->frame,pl->mirrored,pl->speed,pl->braking,
                    pl->turn_lock,pl->turn_delay,pl->practice_event,pl->score_add,campaign_writable);
                turninputticks++;
            }
            else if(openingtest && opening_started){
                unsigned age=pl->logic_tick-opening_tick;
                const float targets[7][2]={{1581.7f,-99.8f},{1184.626f,50},{1212,-720},{136.5f,100},{125,30},{1500,-200},{875,66.6f}};
                float heading=atan2f(targets[openingtest-1][0]-pl->x,targets[openingtest-1][1]-pl->z),angle=heading-L.cam_yaw;
                int moving=age>=40 && age<(openingtest==1 ? 150:240);
                unsigned buttons=openingtest==7 && pl->x>735 && pl->x<800 ? PSP_CTRL_CROSS:0;
                player_update_axes(pl,&L.col,moving ? sinf(angle):0,moving ? -cosf(angle):0,buttons,buttons&~opening_buttons,L.cam_yaw,dt);
                opening_buttons=buttons;
                if(!(age%10)){
                    unsigned info[4]={0},barrier[3]={0};city_openings_info(L.openings,info);shibuya_gates_info(L.gates,barrier);
                    runtime_log("OPENING_INPUT kind=%d age=%u xyz=%.6f,%.6f,%.6f sector=%d opened=%u broken=%u contacts=%u reactions=%u error=%d writable=%d\n",
                        openingtest,age,pl->x,pl->y,pl->z,L.cur_zone,info[0],info[1],info[2]+barrier[1],barrier[0],L.campaign_error,campaign_writable);
                }
            }
            else if(traintest && train_started){
                unsigned age=pl->logic_tick-train_tick;float angle=(L.id==5 ? -1.57079633f:1.57079633f)-L.cam_yaw;
                /* After the ordinary boundary crossing has launched both
                   trains, supply a stationary platform view for texture QA.
                   This changes only the diagnostic player/camera placement. */
                if(L.id==5 && age>=100 && !train_view_placed){
                    const float position[3]={-40,121,-900};unsigned tick=pl->logic_tick;
                    player_reposition(pl,position);pl->logic_tick=tick;pl->yaw=0;
                    player_cam_reset(pl);train_view_placed=1;
                    runtime_log("TRAIN_VIEW supplied platform arrival after crossing\n");
                }
                int moving=L.id==5 ? !train_view_placed:age>=40 && age<130;
                player_update_axes(pl,&L.col,moving?sinf(angle):0,moving?-cosf(angle):0,0,0,L.cam_yaw,dt);
                if(!(age%10)){
                    unsigned info[6];benten_train_info(L.trains,info);
                    runtime_log("TRAIN_INPUT age=%u xyz=%.6f,%.6f,%.6f sector=%d ticks=%u spawns=%u draws=%u hits=%u surfaces=%u active=%u error=%d writable=%d\n",
                        age,pl->x,pl->y,pl->z,L.cur_zone,info[0],info[1],info[2],info[3],info[4],info[5],L.campaign_error,campaign_writable);
                    unsigned audio[5];audio_world_info(audio);
                    runtime_log("TRAIN_AUDIO age=%u starts=%u steals=%u errors=%u owners=%u live=%u\n",age,audio[0],audio[1],audio[2],audio[3],audio[4]);
                }
            }
            else if(sewerwatertest && sewerwater_started){
                unsigned age=pl->logic_tick-sewerwater_tick;float angle=1.5707963268f-L.cam_yaw;
                player_update_axes(pl,&L.col,age<70 ? sinf(angle):0,age<70 ? -cosf(angle):0,0,0,L.cam_yaw,dt);
                if(!(age%10)){
                    int splashes=0;for(int k=0;k<16;k++)splashes+=!!pl->effects.water_sparks[k].active;
                    runtime_log("SEWER_WATER age=%u xyz=%.6f,%.6f,%.6f state=%d wet=%d splashes=%d writable=%d\n",age,pl->x,pl->y,pl->z,pl->state,pl->sound_contact_override,splashes,campaign_writable);
                }
            }
            else if(koganehousetest && house_started){
                unsigned age=pl->logic_tick-house_tick;
                const float targets[4][2]={{185,-510},{-145,375},{-80,-77},{224,620}};
                const unsigned stop[4]={370,240,140,90};
                float heading=atan2f(targets[koganehousetest-1][0]-pl->x,targets[koganehousetest-1][1]-pl->z),angle=heading-L.cam_yaw;
                float ax=age<stop[koganehousetest-1] ? sinf(angle):0,ay=age<stop[koganehousetest-1] ? -cosf(angle):0;
                player_update_axes(pl,&L.col,ax,ay,0,0,L.cam_yaw,dt);
                if(!(age%10)){unsigned info[4];kogane_house_info(L.house,info);
                    runtime_log("HOUSE_INPUT age=%u xyz=%.6f,%.6f,%.6f sector=%d opened=%u broken=%u contacts=%u ticks=%u error=%d writable=%d\n",age,pl->x,pl->y,pl->z,L.cur_zone,info[0],info[1],info[2],info[3],L.campaign_error,campaign_writable);}
            }
            else if (input_play) player_update_axes(pl, &L.col, pax, pay, ppad.Buttons, ppressed, L.cam_yaw, dt);
            else player_update(pl, &L.col, &ppad, ppressed, L.cam_yaw, dt);
            player_follow_spray(&L.follow);
            if(rivalroutetest==3 && L.mrec && !strcmp(L.mrec->name,"S1M07") && frames>=1200 && rivalroute_phase<2) {
                const SceneCamera *v=&L.follow.view;
                runtime_log("PASSAGE_CAMERA tick=%u region=%d state=%d xyz=%.9g,%.9g,%.9g ground=%.9g eye=%.9g,%.9g,%.9g forward=%.9g,%.9g,%.9g fov=%d roll=%d writable=%d\n",
                    L.follow.ticks,L.follow.region!=NULL,L.follow.camera.state,pl->x,pl->y,pl->z,pl->ground_y,
                    v->pos[0],v->pos[1],v->pos[2],v->forward[0],v->forward[1],v->forward[2],v->fov,v->roll,campaign_writable);
                if(L.follow.region) {
                    if(++passage_test_ticks==2)dump_shot("shot_challenge_passage_enter.raw");
                    if(passage_test_ticks==120)dump_shot("shot_challenge_passage_middle.raw");
                    passage_test_departure=0;
                } else if(passage_test_ticks && ++passage_test_departure==2)dump_shot("shot_challenge_passage_exit.raw");
            }
            gfx_profile_logic(LOGIC_RIVALS);
            if(input_play==&lesson_input) {
                lesson_ticks++;
                if(lesson_ticks==180)dump_shot("shot_lesson_skating.raw");
                if(lesson_ticks%30==0 || L.trial.result)
                    runtime_log("LESSON_INPUT tick=%d result=%d index=%d state=%d position=%.6f,%.6f,%.6f camera=%u heading=%.6f cans=%d spray=%d\n",lesson_ticks,L.trial.result,L.trial.index,pl->state,pl->x,pl->y,pl->z,L.follow.ticks,pl->cam_yaw,L.gf.cancount,pl->spray_active);
                if(L.trial.result){dump_shot("shot_lesson_result.raw");lesson_input.active=0;}
                else ctrl_play_tick(&lesson_input);
            }
{ if (logframes < 3) { char m[8]; runtime_log_write(logfd, m, snprintf(m, 8, "B\n")); } }
            /* Drain only retired score tasks. Large-tag points remain in
               their visible tally until it settles or its owner retires. */
            if (!L.gferr) { L.score += L.gf.score; L.gf.score = 0; L.last_tags = L.gf.tagged; }
            L.score += pl->score_add; pl->score_add = 0;   /* grind / trick points (FUN_8c035f70 -> 0x8c1793f0) */
            for (int i = 0; i < L.nrv; i++) { struct Rival *r = &L.rv[i]; if (!r->active) continue;
                if(!r->tagged){r->arrow_acc+=dt;while(r->arrow_acc>=1.f/30.f){r->arrow_acc-=1.f/30.f;r->arrow_frame+=1.f;if(r->arrow_frame>=120.f)r->arrow_frame-=120.f;}}
                unsigned rival_steps=ctrl_frame_steps(&r->clock,dt);
                while(rival_steps-- && r->active) {
                /* Mode-3 parent controller runs before its skater child. */
                if(r->table>=0){rival_tick(&L,r);if(!r->active)break;}
                /* Source 01abb8 queries the previous completed position,
                   before this tick's movement. Controller edges alone do
                   not guarantee a tag: live volume/claim checks decide. */
                if(L.mrec && L.mrec->kind==5 && r->started && r->play.active && !r->tagged && !L.gferr) {
                    int tag=graffiti_rival_spray(&L.gf,r->pl.x,r->pl.y,r->pl.z,&r->spray_cans,
                                               (r->play.cur[1]&~r->play.prev)&4);
                    if(tag>=0)runtime_log("CHALLENGE_RIVAL spray name=%s area=%d index=%d section=%d tick=%u\n",L.mrec->name,L.gf.spots[tag].area,L.gf.spots[tag].idx,r->seg,r->pl.logic_tick+1);
                }
                TitleRoute *route=(unsigned)r->table<3 && L.rival_route[r->table].image ? &L.rival_route[r->table]:NULL;
                if(route) {
                    if(!r->tagged) {
                        if(r->started) {
                            if(!recorded_rival_apply(route,r->seg,r->pl.logic_tick,&r->pl,&chars,MODEL_OF_CHAR[r->chr],r->tag_pose)) {
                                L.campaign_error=12;runtime_log("RIVAL route read failed member=%d section=%d tick=%u\n",r->table,r->seg,r->pl.logic_tick);
                                break;
                            }
                        } else {
                            if(!cherr)player_bind_animation(&r->pl,&chars,MODEL_OF_CHAR[r->chr]);
                            player_rival_wait(&r->pl);
                        }
                    }
                } else {
                SceCtrlData rp; memset(&rp, 0, sizeof rp); rp.Lx = rp.Ly = 128; unsigned rpress = 0; float rax = 0, ray = 0;
                if (r->play.active && !r->tagged) { float ax, ay; unsigned b; ctrl_play_input(&r->play, &ax, &ay, &b); rax = ax; ray = ay;
                    rp.Buttons = (b & 1 ? PSP_CTRL_CROSS : 0) | (b & 4 ? GAME_SPRAY : 0) | (b & 8 ? GAME_BOOST : 0); unsigned pb = (r->play.prev & 1 ? PSP_CTRL_CROSS : 0) | (r->play.prev & 4 ? GAME_SPRAY : 0) | (r->play.prev & 8 ? GAME_BOOST : 0); rpress = rp.Buttons & ~pb; }
                if (!cherr) player_bind_animation(&r->pl,&chars,MODEL_OF_CHAR[r->chr]);
                if(r->pl.camera_tick!=player_follow_tick || r->pl.camera_context!=&r->follow)
                    player_follow_bind(&r->follow,&r->pl,&L.col);
                if(!r->tagged) {
                    if(r->started)player_update_axes(&r->pl,&L.col,rax,ray,rp.Buttons,rpress,r->cam_yaw,1.f/30.f);
                    else player_rival_wait(&r->pl);
                }
                city_openings_contacts(L.openings,&r->pl,(unsigned)i+1);
                }
                if((rivalroutetest==1 && frames>=1200 && rivalroute_phase>=0 && rivalroute_phase<3) || (rivalroutetest==3 && rivalroute_phase<2))
                    runtime_log("ROUTE_SAMPLE slot=%d segment=%d started=%d record=%d repeat=%d tick=%u xyz=%.9g,%.9g,%.9g state=%d motion=%d frame=%.9g yaw=%d speed=%.9g camera_yaw=%d\n",
                        r->table,r->seg,r->started,r->play.pos,r->play.repeat,r->pl.logic_tick,
                        r->pl.x,r->pl.y,r->pl.z,r->pl.state,r->pl.logical_motion,r->pl.frame,
                        ((int)lroundf(r->pl.yaw*(65536.f/6.283185307f))-32768)&65535,r->pl.speed,r->follow.camera.yaw);
                r->cam_yaw = r->pl.cam_yaw;
                if(r->table<0)rival_tick(&L,r);
                }
            }
            L.npcs.sound_event=level_npc_sound;L.npcs.sound_context=&L;
            gfx_profile_logic(LOGIC_NPCS);
            L.npcs.police_attack = L.police.attack; /* advanced by the shared 30 Hz clock below */   /* pedestrians on their routes, squads, waves */
            gfx_profile_logic(LOGIC_MISSION);
            /* NPCs that see the player report to the police manager (FUN_8c0d8080 from the group-member / route-object states):
               armed police squad members (types 0x18/0x19) and wave lineups within their 100-unit arm radius */
            for (int i = 0; i < L.npcs.count; i++) { const Ped *q = &L.npcs.p[i]; if (!q->active) continue;
                int police = (q->list >= 1000 && q->list < 2000 && (q->type == 0x18 || q->type == 0x19) && q->armed) || q->list >= 2000;
                if (!police) continue; float ddx = pl->x - q->x, ddz = pl->z - q->z, d = sqrtf(ddx * ddx + ddz * ddz); if (d < 100.f) police_report(&L.police, d, i, q->type); }
            /* Original child AI advances at 30 Hz, not once per rendered
               frame. Retain button edges across render-only frames. */
            int group_steps=gang_clock_steps(&L.group_clock,dt,ppressed);
            gfx_profile_logic(LOGIC_GANGS);
            while(group_steps--) {
            unsigned group_pressed=gang_clock_pressed(&L.group_clock);
            if(rival_drop_tick)rival_drop_tick++;
            if(policegrabtest && frames>1200) {
                if(!police_grab_tick && L.ngangs<LEVEL_GANG_CAPACITY) {
                    if(policegrabtest==2) {
                        const float origin[3]={-50.f,160.f,380.742676f};
                        player_reposition(pl,origin);pl->yaw=-1.570796327f;player_cam_reset(pl);
                        gangs_spawn_record(&L,3); /* source set10: two groups of three dogs */
                    } else {
                    float origin[3]={1800.f,2.f,1340.f};ColHit hit;
                    if(col_ground(&L.col,origin[0],origin[1],origin[2],30,50,&hit))origin[1]=hit.p[1];
                    player_reposition(pl,origin);pl->yaw=0;player_cam_reset(pl);
                    origin[2]-=28;
                    int area=L.zerr?0:zone_area(&L.zones,L.cur_zone);
                    gang_spawn(&L.gangs[L.ngangs],0,0,origin,0,policegrabtest,
                        L.graphs_ok?graphs_pick(&L.graphs,area,origin):NULL,4,NULL);
                    L.gangs[L.ngangs++].flags|=0x1000;
                    }
                    runtime_log("POLICE_GRAB_TEST spawn count=%d writable=%d\n",policegrabtest,campaign_writable);
                }
                police_grab_tick++;
            }
            enemycombat_bind(&L.combat,pl,&L.col,cherr?NULL:&chars);
            L.combat.gas_skin=L.mrec && L.mrec->st==2; /* 041fac / 1b5c44 */
            L.combat.gang.police_attack=L.police.attack;
            enemycombat_tick(&L.combat);
            if(combattest && ++combat_test_tick==60) {
                /* Supplied projectile/placement inspection, not an earned
                   enemy encounter. All movement/collision/damage is live. */
                GangCombat *c=&L.combat.gang;float a[3],b[3]={pl->x,pl->y,pl->z};
                for(int j=0;j<3;j++)a[j]=b[j];a[0]+=100;a[1]+=30;
                for(int variant=0;variant<3;variant++)c->gas(c->context,a,variant);
                if(combattest==1) {
                c->shot(c->context,a,c->target,8,1);
                c->missile(c->context,a,b,0);a[0]-=200;c->missile(c->context,a,b,1);
                if(!L.combat.error)enemy_shell_create(&L.combat.shells,a,b);
                a[0]=b[0];a[1]=b[1];a[2]=b[2]-60;
                if(!L.combat.error)enemy_wave_create(&L.combat.waves,a,0);
                }
                runtime_log("COMBAT_TEST injected error=%d models=%u health=%d heap=%u writable=%d\n",L.combat.error,L.combat.models.h?L.combat.models.h->n_chars:0,pl->health,(unsigned)mallinfo().fordblks,campaign_writable);
            }
            if(combattest && combat_test_tick%30==0)runtime_log("COMBAT_TEST tick=%d received=%u explosions=%u health=%d error=%d heap=%u audio_errors=%u last_audio=%d\n",combat_test_tick,L.combat.received,L.combat.explosions,pl->health,L.combat.error,(unsigned)mallinfo().fordblks,L.combat.audio_errors,L.combat.last_audio_error);
            L.npcs.combat=&L.combat.gang;
            {float pos[3]={pl->x,pl->y,pl->z};int area=L.zerr?0:zone_area(&L.zones,L.cur_zone);
             L.npcs.on_graph=L.graphs_ok && graphs_pick(&L.graphs,area,pos)!=NULL;}
            gfx_profile_logic(LOGIC_NPCS);
            npcs_update(&L.npcs,&L.col,1.f/30.f,pl->x,pl->y,pl->z,pl->speed);
            gfx_profile_logic(LOGIC_GANGS);
            /* Explicit crash integration fixture: original route record,
               forced activation and completed-GF signal ONLY. No aircraft
               position/state/timing overrides; not an input/playthrough test. */
            if(helitest && !heli_test_parent && frames>800 &&
               (L.mission_set_bank==10 || L.mission_set_bank==56)) {
                heli_test_parent=group_parent_spawn(&L,L.mission_set_bank==10 ? 17:3,0);
            }
            unsigned tag_events=gfo_tick(&L.object_graffiti);
            if(tag_events&1)level_trial_event(&L,TRIAL_FINISH);
            if(tag_events&2)L.ms.f40=1; /* original enemy reveal completion */
            /* Attached progress is sampled by mission slot 1, NOT counted
               as a wall tag. The source has independent reveal tasks. */
            if(!L.gferr && !L.gf.mg_active) {
                GfoPress e=gfo_spray(&L.object_graffiti,&L.gf.cancount,0,(group_pressed&GAME_SPRAY)!=0,L.stun>0 || police_grip_count(&pl->police_grips));
                L.score+=e.score;if(e.trial)level_trial_event(&L,TRIAL_TAG_DEFER);
                if(e.empty)player_spray_actor(pl,&L.gf,NULL,SPRAY_ACT_SOUND,SPRAY_SOUND_EMPTY);
                if(e.sound)player_spray_actor(pl,&L.gf,NULL,SPRAY_ACT_SOUND,SPRAY_SOUND_SMALL);
                if(e.accepted) {
                    GfoObject *o=gfo_find(&L.object_graffiti,L.object_graffiti.target);
                    if(o)player_small_spray_begin(pl,o->center);
                }
                if(e.accepted)printf("GROUP spray object=%08x cans=%d score=%d task=%d\n",L.object_graffiti.target,L.gf.cancount,e.score,e.task);
            }
            for(unsigned k=0;k<GFO_CAPACITY;k++){L.object_graffiti.objects[k].registered=0;L.object_graffiti.objects[k].visible=0;}
            group_parents_tick(&L);
            if(helitest && heli_test_parent && heli_test_index<0) {
                for(int j=0;j<L.ngangs;j++)if(L.gangs[j].parent_handle==heli_test_parent) {
                    if(heli_test_index<0)heli_test_index=j;
                    L.gangs[j].flags|=0x1000|0x2000;
                }
            }
            /* 073454: only one ordinary-police squad owns the attack
               sequence. Resolve before updates so group compaction/order
               cannot steal an in-progress sequence's ownership. */
            for(int i=0;i<L.ngangs;i++)if(L.gangs[i].active && L.gangs[i].kind==0 && L.gangs[i].mode==0 && L.gangs[i].fire_state>=2) {
                L.combat.gang.police_attacking_group=&L.gangs[i];break;
            }
            for(int i=0;i<L.ngangs;i++)if(L.gangs[i].active && L.gangs[i].kind==2 && L.gangs[i].mode==0 && L.gangs[i].fire_state>=2) {
                L.combat.gang.dog_attacking_group=&L.gangs[i];break;
            }
            { int area = L.cur_zone >= 0 && !L.zerr ? zone_area(&L.zones, L.cur_zone) : 0; float pp[3] = { pl->x, pl->y, pl->z };
              /* One immutable player position/area for this tick. A missing
                 graph used to repeat the same full search for every owner. */
              const GGraph *player_graph=L.graphs_ok ? graphs_pick(&L.graphs,area,pp):NULL;
              int off=player_graph==NULL;
              L.combat.gang.formation_groups=L.gangs;L.combat.gang.formation_group_count=L.ngangs;
              for (int i = 0; i < L.ngangs; i++) { Gang *g = &L.gangs[i]; g->combat=&L.combat.gang; gang_player_graph(g,player_graph); gang_tick(g, &L.graphs, area, pp, pl->speed, off, L.police.attack, gang_ground, &L); if (gang_reports_police(g)) { float dx = pl->x - g->pos[0], dz = pl->z - g->pos[2]; police_report(&L.police, sqrtf(dx * dx + dz * dz), i, g->kind); }
                group_graffiti_pose(&L,g,i+1);
                if(helitest && i==heli_test_index && logfd>=0) {
                    char line[256];int n=snprintf(line,sizeof line,"HELI_TEST tick=%d state=%d anim=%u frame=%.0f clock=%.0f parent=%u y=%.6f boom=%d bank=%d\n",
                        heli_test_tick++,g->state,g->wreck.anim,g->wreck.frame,g->wreck.clock[0],
                        g->parent_flags ? *g->parent_flags:0,g->pos[1],g->wreck_events.explosion,!heliwreck_err);
                    runtime_log_write(logfd,line,n);
                }
                if(g->solo_drops) {
                    g->solo_drops=0;int spawned=0;
                    for(int j=0;j<g->nwp;j++) {
                        if(g->wp_action[j]>=5 && !L.health_models.h && chars_load(&L.health_models,"healthpickups.jgc")) {
                            L.campaign_error=10;runtime_log("RIVAL_DROP health models unavailable\n");break;
                        }
                        int error=graffiti_can_drop(&L.gf,g->target,g->wp[j],g->wp_action[j]);
                        if(error)runtime_log("RIVAL_DROP kind=%d item=%d error=%d\n",g->kind,g->wp_action[j],error);
                        else spawned++;
                    }
                    runtime_log("RIVAL_DROP kind=%d spawned=%d origin=%.3f,%.3f,%.3f\n",g->kind,spawned,g->target[0],g->target[1],g->target[2]);
                }
                if (g->hit) { float l = sqrtf(g->hit_dx * g->hit_dx + g->hit_dz * g->hit_dz) + 1e-3f; L.npcs.hits += g->hit; L.npcs.hit_dx = g->hit_dx / l; L.npcs.hit_dz = g->hit_dz / l; g->hit = 0; } } }
            for(int i=0;i<L.nrv;i++) {
                struct Rival *r=L.rv+i;GfoObject *o=gfo_find(&L.object_graffiti,r->graffiti);
                if(!r->active || !o)continue;
                float root[16],matrix[16];player_root_matrix(&r->pl,root);
                for(int a=0;a<4;a++)for(int b=0;b<4;b++)matrix[b*4+a]=root[a*4+b];
                gfo_transform(o,matrix);o->registered=o->progress.complete ? 0:128+i;
                o->visible=o->progress.started;
            }
            if(enemycombat_receive(&L.combat) && !L.gferr) {
                if(graffiti_interrupt(&L.gf)&GRAFFITI_TAG)level_trial_event(&L,TRIAL_TAG);
                L.score+=L.gf.score;L.gf.score=0;L.last_tags=L.gf.tagged;
            }
            if(policegrabtest && police_grab_tick) {
                int count=police_grip_count(&pl->police_grips);
                if(policegrabtest==2 && police_grab_tick%15==0) {
                    int dogs=0;unsigned states=0;
                    for(int k=0;k<L.ngangs;k++)if(L.gangs[k].active && L.gangs[k].kind==2) {
                        dogs+=L.gangs[k].nmem;
                        for(int j=0;j<L.gangs[k].nmem;j++)states|=1u<<L.gangs[k].m[j].state;
                    }
                    runtime_log("DOG_TEST tick=%d dogs=%d states=%x audio_errors=%u\n",police_grab_tick,dogs,states,L.combat.audio_errors);
                }
                if(count>police_grab_max)police_grab_max=count;
                if(count && !police_grab_first)police_grab_first=police_grab_tick;
                if(!count && police_grab_first && !police_grab_release)police_grab_release=police_grab_tick;
                if(police_grab_tick%10==0) {
                    runtime_log("POLICE_GRAB_TEST tick=%d grips=%d max=%d player=%d motion=%d health=%d first=%d release=%d xyz=%.3f,%.3f,%.3f\n",police_grab_tick,count,police_grab_max,pl->state,pl->logical_motion,pl->health,police_grab_first,police_grab_release,pl->x,pl->y,pl->z);
                    for(int i=0;i<L.ngangs;i++)if(L.gangs[i].kind==0)for(int j=0;j<L.gangs[i].nmem;j++) {
                        GangMember *m=L.gangs[i].m+j;
                        runtime_log("POLICE_GRAB_MEMBER tick=%d i=%d,%d state=%d anim=%d frame=%.2f slot=%d flags=%x speed=%.3f xyz=%.3f,%.3f,%.3f\n",police_grab_tick,i,j,m->state,m->anim,m->frame,m->police.slot,m->flags,m->speed,m->pos[0],m->pos[1],m->pos[2]);
                    }
                }
            }
            { float p[3]={pl->x,pl->y,pl->z};gfo_select(&L.object_graffiti,p); }
            if(objecttag_phase==3 || objecttag_phase==4) {
                GfoObject *o=gfo_find(&L.object_graffiti,objecttag_handle);
                if(o && o->progress.started) {
                    int ai=-1,anim=-1;for(int j=0;j<L.ngangs;j++)if(L.gangs[j].graffiti_handle==objecttag_handle){ai=L.gangs[j].state;anim=L.gangs[j].unit_state;}
                    if(logfd>=0){char b[224];runtime_log_write(logfd,b,snprintf(b,sizeof b,"OBJECT_TAG_TEST tick=%d reveal=%u complete=%u cans=%d walltags=%d state=%d anim=%d texture=%d\n",objecttag_ticks++,o->progress.reveal,o->progress.complete,L.gf.cancount,L.gf.tagged,ai,anim,L.object_graffiti_texture!=NULL));}
                    objecttag_phase=o->progress.complete?5:4;
                }
            }
            }
            { extern int gang_force_activate; if (gangtest_rec >= 0 && L.ms.active && frames > 400) { int gr=gangtest_rec,handled=0;
                /* Reuse an already-created mission record during inspection;
                   spawning it twice masks formation/attack arbitration. */
                for(int pi=0;pi<64;pi++)if(L.group_parent[pi].active && L.group_parent[pi].state>0 && L.group_parent[pi].record==gr)handled=1;
                if(!handled)handled=gangs_spawn_record(&L,gr); if (!handled) { npcs_set_list(&L.npcs, 1000 + gr, 1); gang_force_activate = 0; } gangtest_rec = -1; } if (gang_force_activate && frames == 900) { dump_shot("shot_gang.raw"); for (unsigned oi = 0; oi < L.st.h->n_obj; oi++) { JgsObj *o = &L.st.obj[oi]; float dx = o->cx - pl->x, dz = o->cz - pl->z; if (dx * dx + dz * dz < o->r * o->r && fabsf(o->cy - pl->y) < 80.f && o->r < 400.f) { JgsBatch *b0 = &L.st.batch[o->first_batch]; printf("under: obj %u c %.0f,%.0f,%.0f r %.0f nb %u tex %d flags %x col %08x\n", oi, o->cx, o->cy, o->cz, o->r, o->n_batch, b0->tex, b0->flags, stage_batch_vertices(&L.st,b0)->color); } } } if (poltest && frames == 500) printf("poltest: level %d lvis %d lstate %d hudpol_err %d player %.0f,%.0f,%.0f yaw %.2f\n", L.police.level, L.police.lvis, L.police.lstate, hudpol_err, pl->x, pl->y, pl->z, pl->yaw); if (gang_force_activate && (frames % 60) == 0) for (int i = 0; i < L.ngangs; i++) if (L.gangs[i].active) { Gang *g = &L.gangs[i]; GangMember *m = &g->m[0]; runtime_log("gangpos%d: kind %d st %d action %d/%d wp %d/%d grp %.0f,%.0f,%.0f m0 %.0f,%.0f,%.0f spd %.3f mst %d anim %d ov %d/%d mf %x\n", i, g->kind, g->state, g->action, g->next_action, g->wpi, g->nwp, g->pos[0], g->pos[1], g->pos[2], m->pos[0], m->pos[1], m->pos[2], g->speed, m->state, m->anim, m->overlay, m->secondary_anim, m->flags);
                if(g->type==7 || g->kind==4)for(int j=0;j<g->nmem;j++) {
                    GangMember *p=g->m+j;
                    runtime_log("ENEMY_MEMBER kind=%d frame=%d group=%d member=%d state=%d anim=%d clip=%d clock=%.2f speed=%.6f flags=%x xyz=%.3f,%.3f,%.3f\n",g->kind,frames,i,j,p->state,p->anim,gang_police_motion(p),p->frame,p->speed,p->flags,p->pos[0],p->pos[1],p->pos[2]);
                }
            } }
            { extern int gang_force_activate; if (logfd >= 0 && gang_force_activate && (frames % 60) == 0) {
                for (int i = 0; i < L.ngangs; i++) if (L.gangs[i].active) {
                    char gb[256]; Gang *g = &L.gangs[i];
                    int gn = snprintf(gb, sizeof gb,
                        "GANG,%d,kind=%d,state=%d,unit=%d,action=%d,next=%d,wp=%d/%d,pos=%.2f,%.2f,%.2f,speed=%.6f,mstate=%d,anim=%d,mflags=%x,mpos=%.2f,%.2f,%.2f,mspeed=%.6f,aim=%d/%d\n",
                        i, g->kind, g->state, g->unit_state, g->action,
                        g->next_action, g->wpi, g->nwp, g->pos[0], g->pos[1],
                        g->pos[2], g->speed, g->m[0].state, g->m[0].anim,
                        g->m[0].flags, g->m[0].pos[0], g->m[0].pos[1],
                        g->m[0].pos[2], g->m[0].speed, g->m[0].aim,
                        g->m[0].aim_step);
                    runtime_log_write(logfd, gb, gn);
                }
            } }
            gfx_profile_logic(LOGIC_MISSION);
            if (poltest) { police_report(&L.police, 50.f, 0, 0); if (L.police.level < 1) police_alert_place(&L.police, pl->x, pl->y, pl->z, 2); if (frames == 500) dump_shot("shot_police.raw"); }
            police_tick(&L.police, pl->x, pl->y, pl->z);
            if (L.npcs.hits && player_bullet_vulnerable(pl)) {
                /* 03b0fc -> 03c05c BEFORE collision dispatch, not 03bfa8's
                   voluntary return-to-control path. Do not dispatch the
                   already-consumed normal graffiti events a second time. */
                if(!L.gferr) {
                    if(graffiti_interrupt(&L.gf)&GRAFFITI_TAG)level_trial_event(&L,TRIAL_TAG);
                    L.score+=L.gf.score;L.gf.score=0;L.last_tags=L.gf.tagged;
                }
                player_receive_bullet(pl);
            }
            /* Per-tick replay traces are test output, not shipping Memory
               Stick traffic during the title demo and campaign tutorials. */
            if (L.ms.active && !L.gferr) { L.mtick += dt; if (L.mtick > 4.f / 30.f) L.mtick = 4.f / 30.f; L.ms.score = L.score; while (L.mtick >= 1.f / 30.f) { if ((L.play.active || L.demo_on) && logfd >= 0 && (autopilot || movementtest || lessoninputtest)) { char pb[96]; runtime_log_write(logfd, pb, snprintf(pb, sizeof pb, "P,%d,%.2f,%.2f,%.2f,%.4f,%.4f,%d,%d,%.4f\n", L.play_tick++, pl->x, pl->y, pl->z, pl->yaw, pl->speed, pl->motion, L.play.pos, L.cam_yaw)); }
                if (L.play.active && !ctrl_play_tick(&L.play))mission_advance(&L);
                if(L.demo_on)++L.demo_tick_n; /* Keep the final sample visible until the next tick. */
                if(L.mrec!=replay_record || L.campaign_error)break;
                if (!L.zerr) {
                    int zc=zone_of(&L.zones,pl->x,pl->y+5.f,pl->z);
                    int changed=zc>=0 && zc!=L.cur_zone;
                    if(changed)L.cur_zone=zc;
                    if(changed)zone_eval(&L);
                }
                if (L.demo_on && L.demo_cam_script) {   /* camera timeline: entries {tick, 1, code, camrec}; the latest entry whose tick has passed is in force */
                    const DemoSkater *k = &L.demo.sk[0]; L.demo_tick_n++;
                    while (L.demo_evi + 1 < k->nev && k->ev[L.demo_evi].tick <= (unsigned)L.demo_tick_n) L.demo_evi++;
                    if (L.demo_evi >= 0) { const DemoEv *e = &k->ev[L.demo_evi]; L.dcam_on = 1; L.dcam_code = e->code; L.dcam_pos[0] = e->pos[0]; L.dcam_pos[1] = e->pos[1]; L.dcam_pos[2] = e->pos[2]; }
                }
                if(exit_fixture.enabled && !L.demo_on && !L.play.active && !L.ms.phase) {
                    float position[3];int supplied=exit_fixture_tick(&exit_fixture,&L.ms,position);
                    if(supplied<0){L.campaign_error=13;runtime_log("EXIT_FIXTURE failed phase=%d\n",exit_fixture.phase);break;}
                    if(supplied>0) {
                        pl->x=position[0];pl->y=position[1];pl->z=position[2];
                        pl->speed=pl->vy=0;
                        if(exit_fixture.just_started) {
                            stage_exit_reset(&L.stage_exit,L.mrec->st,L.mrec->mid,position);
                            memcpy(L.ms.quad_previous,position,sizeof position);
                            runtime_log("EXIT_FIXTURE selected stage=%u entry=%u origin=%s event=%08x direction=%x writable=%d\n",
                                L.mrec->st,exit_fixture.entry,exit_fixture.stage_boundary ? "stage":"mission",
                                exit_fixture.event,exit_fixture.direction,campaign_writable);
                        }
                        if(exit_fixture.just_crossed)runtime_log("EXIT_FIXTURE crossing entry=%u xyz=%.6f,%.6f,%.6f\n",
                            exit_fixture.entry,pl->x,pl->y,pl->z);
                    }
                }
                mission_position(&L.ms,pl->x,pl->y,pl->z);
                if(!L.demo_on && !L.ms.phase) {
                    if(stage_exit_tick(&L.stage_exit,L.ms.position) && mission_request_exit(&L.ms))
                        runtime_log("LEVEL_EXIT stage boundary=%d crossing=%x\n",L.stage_exit.last_quad,L.stage_exit.last_cross);
                    mission_tick(&L.ms,&L.gf,pl->health<=0);
                }
                if(L.ms.phase==MISSION_PHASE_EXIT){L.mtick=0;break;}
                if (L.ms.phase != bgm_phase_seen) { bgm_phase_seen = L.ms.phase; if (L.ms.phase == 1) bgm_clear((int)mission_word(&L.ms, 0x18)); else if (L.ms.phase == 2) bgm_fail(); }   /* FUN_8c018a0c: clear -> FUN_8c02e290 jingle, fail -> FUN_8c02e738 */
                if(!L.campaign_error && L.ms.phase==1) {
                    int ready=0;
                    if(L.mrec && L.mrec->kind!=6 && L.id!=4) {
                        if(!L.result.active) {
                            float result_pos[3];int32_t result_angles[3];
                            int result_area=L.cur_zone>=0 && !L.zerr ? zone_area(&L.zones,L.cur_zone):-1;
                            if(!mission_result_pose(&L.ms,result_area,result_pos,result_angles)) {
                                L.campaign_error=8;runtime_log("CAMPAIGN clear pose unavailable area=%d\n",result_area);break;
                            }
                            player_result_begin(pl,result_pos,result_angles);
                            runtime_log("RESULT_ACTOR state=%d motion=%d position=%.6f,%.6f,%.6f angle=%d,%d,%d\n",pl->state,pl->motion,pl->x,pl->y,pl->z,result_angles[0],result_angles[1],result_angles[2]);
                            level_retire_gameplay_resources(&L);
                            if(result_camera_load(&L.result_camera,pl->character_id)) {
                                L.campaign_error=8;runtime_log("CAMPAIGN clear camera unavailable character=%d\n",pl->character_id);break;
                            }
                            L.follow.result_tracks=&L.result_camera.tracks;L.follow.result_frame=0;
                            if(!failure_models.models.h && failure_models_load(&failure_models,"failurehud.jgc","failurehud.bin")){
                                L.campaign_error=10;runtime_log("HUD original result assets unavailable heap=%u\n",(unsigned)mallinfo().fordblks);break;
                            }
                            if(!font_real_wide_ready()){
                                int font_error=font_real_load_wide("fontwide.bin");
                                if(font_error){L.campaign_error=10;runtime_log("HUD original result font unavailable error=%d heap=%u\n",font_error,(unsigned)mallinfo().fordblks);break;}
                                L.result_wide_owned=1;
                            }
                            police_alert_kill(&L.police); /* 036068 */
                            level_result_totals(&L);
                            result_hud_start(&L.result,L.mrec->kind,L.score,pl->health,pl->health_max,L.ms.time_left,L.ms.time_total,L.gferr ? 0:L.gf.cancount);
                            result_hud_context(&L.result,L.record_place,story_mission_souls(),
                                story_result_route(L.mrec->mid,(int)mission_word(&L.ms,0x18),L.mrec->kind,0).branch);
                            runtime_log("HUD_AUDIO result load=%d character=%d\n",audio_load_hud_voice(CHAR_ID_OF_MODEL[pchar&15]),CHAR_ID_OF_MODEL[pchar&15]);
                            /* 050238: deleting an ordinary timer must not
                               stop a radio message on the shared handle. */
                            if(L.timer_hud.owned && L.timer_hud.countdown)audio_countdown_voice(-1);
                            timer_hud_init(&L.timer_hud,L.mrec->kind,L.mrec->kind==8 ? 2:1);
                        }
                        int cue=result_hud_voice(&L.result,CHAR_ID_OF_MODEL[pchar&15],L.result_confirm);
                        if(cue>=0)runtime_log("HUD_AUDIO result count=%d program=%d error=%d\n",L.result.count,cue,audio_hud_voice(cue));
                        int action=recruit_result_step(&L.result,L.result_confirm,sceneplay_resident(&L.scene));
                        L.result_confirm=0;
                        recruit_result_fade_step(&L.result);
                        recruit_result_hud_tick(&L.result,&L.health_hud,pl->tun->health_low);
                        recruit_result_cans(&L.result,&L.gf.cancount);
                        result_hud_present(&L.result);
                        if(action==1 || (L.result_scene==1 && !sceneplay_resident(&L.scene)))level_result_scene_ready(&L);
                        ready=action==2;
                    } else ready=++L.done_ticks>45 && level_result_scene_ready(&L);
                    if(ready && L.ms.next_rec>=0 && L.ms.next_set>=0)mission_advance(&L);
                } /* Final-stage ending is a separate earned progression consumer. */
                if(L.mrec!=replay_record || L.campaign_error)break;
                L.mtick -= 1.f / 30.f; } }
{ if (logframes < 3) { char m[8]; runtime_log_write(logfd, m, snprintf(m, 8, "C\n")); } }
            }
            if(L.mrec!=replay_record || L.campaign_error || L.ms.phase==MISSION_PHASE_EXIT)break;
            }
            dt=render_dt;
            gfx_profile_logic(LOGIC_OTHER);
            hang_mark(HANG_CAMERA_VIEW);
            /* The completed 01dc40 callback owns camera motion, obstruction
               history and the next controller heading. Rendering must not
               move it a second time or invent a separate floor-height snap. */
            if(L.follow.view.active)memcpy(L.cam_eye,L.follow.view.pos,12);
            L.cam_yaw=pl->cam_yaw;
            eye[0] = L.cam_eye[0]; eye[1] = L.cam_eye[1]; eye[2] = L.cam_eye[2];
            if (camtest_on) { eye[0] = camtest[0]; eye[1] = camtest[1]; eye[2] = camtest[2]; }
            if (L.dcam_on) {   /* demo camera (camera mode 3): 0x20 = fixed world point, 0x2a = offset in the skater's DC-local frame (+x left, +y up, +z backward); target = skater + 10 (traced) */
                if (L.dcam_code == 0x20) { eye[0] = L.dcam_pos[0]; eye[1] = L.dcam_pos[1]; eye[2] = L.dcam_pos[2]; }
                else { float sy = sinf(pl->yaw), cy = cosf(pl->yaw); eye[0] = pl->x - L.dcam_pos[0] * cy - L.dcam_pos[2] * sy; eye[1] = pl->y + L.dcam_pos[1]; eye[2] = pl->z + L.dcam_pos[0] * sy - L.dcam_pos[2] * cy; }
            }
{ if (logframes < 3) { char m[8]; runtime_log_write(logfd, m, snprintf(m, 8, "D\n")); } }
            float lx = pl->x - eye[0], ly = pl->y + (L.dcam_on ? 10.f : 14.f) - eye[1], lz = pl->z - eye[2], ll;   /* target = skater + 14 (+0xa0); demo camera: +10 */
            if (camtest_on) { lx = camtest[3] - eye[0]; ly = camtest[4] - eye[1]; lz = camtest[5] - eye[2]; }
            /* Optional inspection yaw sweep, degrees per rendered frame.
               Only the camera moves; actor lists, waits and AI stay live. */
            if(camtest_on && camtest_sweep && frames>=1000) {
                float a=(frames-1000)*camtest_sweep*(3.14159265f/180.f),c=cosf(a),s=sinf(a),x=lx;
                lx=x*c+lz*s;lz=lz*c-x*s;
            }
            ll = sqrtf(lx*lx + ly*ly + lz*lz);
            fwd_override[0] = lx / ll; fwd_override[1] = ly / ll; fwd_override[2] = lz / ll; use_fwd = 1;
            if(!camtest_on && !L.dcam_on && L.follow.view.active)
                memcpy(fwd_override,L.follow.view.forward,12);
        } else {
            float ax = (pad.Lx - 128) / 128.f, ay = (pad.Ly - 128) / 128.f; if (fabsf(ax) < 0.15f) ax = 0; if (fabsf(ay) < 0.15f) ay = 0;
            float speed = (pad.Buttons & PSP_CTRL_CROSS) ? 600.f : 200.f;
            if (!(pad.Buttons & PSP_CTRL_LTRIGGER)) { if (pad.Buttons & PSP_CTRL_LEFT) yaw += 1.8f * dt; if (pad.Buttons & PSP_CTRL_RIGHT) yaw -= 1.8f * dt; if (pad.Buttons & PSP_CTRL_UP) pitch += 1.2f * dt; if (pad.Buttons & PSP_CTRL_DOWN) pitch -= 1.2f * dt; }
            if (pitch > 1.4f) pitch = 1.4f; if (pitch < -1.4f) pitch = -1.4f;
            float f[3] = { sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) }, r[3] = { cosf(yaw), 0, -sinf(yaw) };
            for (int i = 0; i < 3; i++) eye[i] += (f[i] * -ay + r[i] * ax) * speed * dt;
            if (pad.Buttons & PSP_CTRL_RTRIGGER) eye[1] += speed * dt;
            if ((pad.Buttons & PSP_CTRL_LTRIGGER) && !(pad.Buttons & (PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) eye[1] -= speed * dt;
        }
        if(L.demo_on && L.title_route.image) {
            title_apply(&L,L.demo_tick_n ? (unsigned)L.demo_tick_n-1:0,L.replay_clock.remainder*30.f);
            memcpy(eye,L.title_sample.camera.pos,12);memcpy(fwd_override,L.title_sample.camera.forward,12);use_fwd=1;
        }
        else if(recruit_demo_active(&L) && !scene_frozen && L.recruit_demo_tick) {
            recruit_demo_apply(&L,(unsigned)L.recruit_demo_tick-1,L.replay_clock.remainder*30.f);
            memcpy(eye,L.title_sample.camera.pos,12);memcpy(fwd_override,L.title_sample.camera.forward,12);use_fwd=1;
        }
        if(scene_frozen && L.scene.vm.camera.active) {
            memcpy(eye,L.scene.vm.camera.pos,12);memcpy(fwd_override,L.scene.vm.camera.forward,12);use_fwd=1;
        }
        else if(L.pl.spray_active && L.pl.spray_camera.active && !L.follow.region) {
            memcpy(eye,L.pl.spray_camera.eye,12);memcpy(fwd_override,L.pl.spray_camera.forward,12);use_fwd=1;
        }
        if(group_camera_test && autopilot && !scene_frozen)for(int i=0;i<L.ngangs;i++) {
            const Gang *g=L.gangs+i;
            if(!g->active || (g->kind!=11 && g->mode!=4 && g->mode!=7) || gang_mover_hidden(g))continue;
            /* Explicit material-inspection camera; no actor state changes. */
            const float car_offset[3]={80,40,0},heli_offset[3]={150,60,-180},rocket_offset[3]={40,25,-65};
            const float *offset=g->mode==4?heli_offset:g->mode==7?rocket_offset:car_offset;float length=0;
            for(int k=0;k<3;k++) {
                eye[k]=g->pos[k]+offset[k];
                fwd_override[k]=-offset[k]+(k==1 ? 6.f:0.f);
                length+=fwd_override[k]*fwd_override[k];
            }
            length=sqrtf(length);for(int k=0;k<3;k++)fwd_override[k]/=length;
            use_fwd=1;
            if(frames==900 || frames==1300)runtime_log("GROUP_MATERIAL frame=%d model=%d pos=%.3f,%.3f,%.3f motion=%d\n",frames,gang_model(g->kind),g->pos[0],g->pos[1],g->pos[2],g->m[0].motion);
            break;
        }
        if(helitest && !camtest_on && heli_test_index>=0 && heli_test_index<L.ngangs) {
            const Gang *g=L.gangs+heli_test_index;
            /* Diagnostic camera only, NOT recovered gameplay choreography.
               Try clear sightlines so the fixture does not photograph the
               inside of a Grind Square building instead of the wreck. */
            static const float offsets[8][3]={{160,60,-220},{-160,60,-220},
                {160,60,220},{-160,60,220},{0,60,-270},{0,60,270},{-270,60,0},{270,60,0}};
            float target[3]={g->pos[0],g->pos[1]+10.f,g->pos[2]},best=-1.f;
            for(int j=0;j<8;j++) {
                float candidate[3],clear=1.f;ColHit hit;
                for(int k=0;k<3;k++)candidate[k]=g->pos[k]+offsets[j][k];
                if(col_probe(&L.col,target,candidate,0,&hit))clear=hit.t;
                if(col_probe(&L.col,candidate,target,0,&hit))clear=fminf(clear,1.f-hit.t);
                if(clear>best) {
                    best=clear;float distance=0.f;
                    for(int k=0;k<3;k++){float d=candidate[k]-target[k];distance+=d*d;}
                    float fraction=fmaxf(0.01f,clear-5.f/sqrtf(distance));
                    for(int k=0;k<3;k++)eye[k]=target[k]+fraction*(candidate[k]-target[k]);
                }
                if(clear==1.f)break;
            }
            float v[3],len=0.f;
            for(int k=0;k<3;k++){v[k]=target[k]-eye[k];len+=v[k]*v[k];}
            len=sqrtf(len);for(int k=0;k<3;k++)fwd_override[k]=v[k]/len;
            use_fwd=1;
        }
        float fwd[3] = { sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) }; if (use_fwd) { fwd[0] = fwd_override[0]; fwd[1] = fwd_override[1]; fwd[2] = fwd_override[2]; }
        memcpy(L.render_forward,fwd,sizeof fwd);
        /* Supplied alert-level fixture precedes the real alert update, so
           its first draw has the constructor's matrix, not a zero matrix. */
        if(policehudtest && frames>=400 && !scene_frozen && policehud_tick!=pl->logic_tick) {
            policehud_tick=pl->logic_tick;policehud_age++;
            if(policehud_age<=270 && policehud_age%90==0) {
                police_alert_place(&L.police,pl->x,pl->y,pl->z,policehud_age/90);
                policehud_shots=0;
            }
        }
        if(!scene_frozen && L.police.level>=0 && L.police.arrow_tick!=pl->logic_tick) {
            const float pos[3]={pl->x,pl->y,pl->z};L.police.arrow_tick=pl->logic_tick;
            police_arrow_tick(&L.police.arrow,&L.police.lvis,L.police.marker,pos,fwd,player_fsca_sin,player_bam_atan2);
        }
        float proj[16], view[16], clip[16], up[3] = {0, 1, 0};
        /* 27a7c8 derives focal distance from the active source camera's
           BAM angle through FSCA tangent (27d6a8). Inspection/demo views
           without a recovered camera record retain the legacy lens. */
        float focal=camtest_on ? camtest[6]:536.f;
        int view_fov=2*player_bam_atan2(320.f,focal);
        float near_clip=5.f,far_clip=L.final.loaded ? 20000.f:6000.f;
        if((L.demo_on || (recruit_demo_active(&L) && !scene_frozen && L.recruit_demo_tick)) && L.title_route.image) {
            SceneCamera *camera=&L.title_sample.camera;view_fov=camera->fov;focal=player_camera_focal(camera->fov,640.f,1.f);
            near_clip=camera->near_clip;far_clip=camera->far_clip;player_camera_matrix(camera->pos,camera->forward,camera->roll,view);
        } else if(scene_frozen && L.scene.vm.camera.active && L.scene.vm.camera.fov) {
            SceneCamera *camera=&L.scene.vm.camera;
            view_fov=camera->fov;
            focal=player_camera_focal(camera->fov,640.f,1.f);
            near_clip=camera->near_clip;far_clip=camera->far_clip;
            player_camera_matrix(camera->pos,camera->forward,camera->roll,view);
        } else if(L.pl.spray_active && L.pl.spray_camera.active && !L.follow.region) {
            view_fov=L.pl.spray_camera.fov;focal=player_camera_focal(L.pl.spray_camera.fov,640.f,1.f);
            player_camera_matrix(eye,fwd,L.pl.spray_camera.roll,view);
        } else if(gamemode && !camtest_on && !L.dcam_on && !helitest && !group_camera_test && L.follow.view.active) {
            SceneCamera *camera=&L.follow.view;
            view_fov=camera->fov;
            focal=player_camera_focal(camera->fov,640.f,1.f);
            near_clip=camera->near_clip;far_clip=camera->far_clip;
            player_camera_matrix(eye,fwd,camera->roll,view);
        } else m_lookat(view,eye,fwd,up);
        /* Dreamcast uses near=.1 in the bus intro and near=1 in gameplay.
           PSP's 16-bit depth buffer needs a larger near distance to keep
           close building layers distinct. This is a GE projection adapter;
           keep authored camera tracks/FOV and the distant scenery range. */
        near_clip=ge_world_near(near_clip);
        /* STAGELAST's animated background includes a 9529-unit sky shell;
           the city geometry's 6000-unit clip distance cuts that shell away. */
        m_perspective(proj,2.f*atanf(240.f/focal),(float)SCR_W/SCR_H,near_clip,far_clip);m_mul(clip,proj,view);
        if(!scene_frozen && !L.pause.active){
            npcs_view(&L.npcs,view,view_fov,(float)SCR_W/SCR_H);
            gang_solo_view(L.gangs,L.ngangs,view,view_fov,(float)SCR_W/SCR_H);
        }
        camtest_edge_capture=0;
        if(camtest_on && camtest_sweep && !scene_frozen && frames>=1000 && frames<=1720) {
            NpcVisibility source_view=npc_visibility_params(view_fov,player_fsca_sin);
            for(int i=0;i<L.npcs.count;i++) {
                const Ped *p=L.npcs.p+i;if(!p->active || p->list>=1000)continue;
                float y=p->y+8.5f;
                float cx=view[0]*p->x+view[1]*y+view[2]*p->z+view[3];
                float cy=view[4]*p->x+view[5]*y+view[6]*p->z+view[7];
                float z=p->audio_depth;if(z<=0.f)continue;
                float nx=proj[0]*cx/z,ny=proj[5]*cy/z;
                int old=npc_visibility_class(&source_view,cx,cy,z);
                if(!(frames%3))runtime_log("PED_VIEW frame=%d list=%d type=%d ready=%d mode=%d old=%d draw=%d alpha=%.6f ndc=%.6f,%.6f depth=%.6f\n",
                    frames,p->list,p->type,p->seen,p->visible,old,p->near,p->opacity,nx,ny,z);
                if(!old && p->near && p->opacity>.2f && fabsf(nx)<1.f && fabsf(ny)<1.f)camtest_edge_capture=1;
            }
        }
        if (gamemode || !freeze) frustum_planes(clip, planes);

        int render_saved_freeze=freeze,render_log_deferred=runtime_log_deferred;float render_saved_dt=dt;
        float render_saved_cpu=cpu_avg;int render_bench_capture=0,render_bench_completed=0;
        int render_bench_available=gamemode && !title_mode && !scene_frozen && !L.pause.active && !L.ms.phase &&
           L.ms.active && !L.result.active && !L.final.loaded && !L.campaign_error;
        if(JGR_DIAGNOSTICS && (render_bench_request || (render_bench_auto>0 && frames>=render_bench_auto)) && render_bench_available) {
            render_bench_capture=render_bench_auto>0;render_bench_auto=0;render_bench_begin(&render_bench,pl->logic_tick);
            render_bench.detail=render_bench_detail_mode>=0 && render_bench_detail_mode<=RENDER_BENCH_PED_OCCLUSION_PROXIES ? render_bench_detail_mode:RENDER_BENCH_PED_OCCLUSION_PROXIES;
            if(render_bench.detail==RENDER_BENCH_PED_SHARED)render_bench.detail=RENDER_BENCH_PED_NATIVE;
            render_bench_diagnostics=render_bench.detail==RENDER_BENCH_PED_NATIVE && (!render_bench_capture || render_bench_force_diagnostics);
            /* Explicit frozen benchmark: keep its large result reports,
               then restore quiet logging before the next gameplay frame. */
            gfx_wait_idle();runtime_log_checkpoint("benchmark");
            runtime_log("RENDER_BENCH_BEGIN run=%u feature=%s stage=%d zone=%d tick=%u player=%.5f,%.5f,%.5f eye=%.5f,%.5f,%.5f warmup=%u samples=%u order=ABBA me=%d cpu_mhz=%d bus_mhz=%d\n",
                render_bench.run,render_bench_feature(render_bench.detail),L.id,L.cur_zone,pl->logic_tick,pl->x,pl->y,pl->z,eye[0],eye[1],eye[2],
                RENDER_BENCH_WARMUP,RENDER_BENCH_SAMPLES,audio_me_status(),scePowerGetCpuClockFrequencyInt(),scePowerGetBusClockFrequencyInt());
            /* The first warmup renders this frame normally, including the
               sky's draw-owned clock. Then redraw without re-entering the
               simulation or advancing render-owned clocks. Audio continues. */
        }
render_bench_redraw:
        ;GfxProfile render_before;unsigned write_before[3],render_started=0;
        CharPlanProfile plan_before;StageProfile clip_before;CharArrowProfile arrow_before;
        CharLodProfile detail_before;
        CharWorkStats actor_before[CHAR_WORK_COUNT];
        CharPedPacketProfile ped_packet_before;
        CharPedSharedProfile ped_shared_before;NpcOcclusionProfile ped_occlusion_before;
        int render_bench_repeat=render_bench.active && (render_bench.target || render_bench.phase || render_bench.frame);
        if(render_bench.active) {
            if(render_bench_repeat){freeze=1;dt=0;}
            gfx_wait_idle();vfpu_profile_frame_begin();
            chars_draw_plan_mode(1);stage_clip_mode(1);chars_arrow_mode(render_bench.detail ? 1:render_bench_mode(&render_bench));
            chars_lod_mode(render_bench.detail==1 ? render_bench_mode(&render_bench):npc_detail);
            stage_set_scenery_mode(render_bench.detail==2 ? render_bench_mode(&render_bench):1);
            stage_set_plane_occlusion(render_bench.detail==3 ? render_bench_mode(&render_bench):1);
            chars_pose_span_mode(render_bench.detail==RENDER_BENCH_POSE_SPAN ? render_bench_mode(&render_bench):1);
            chars_ped_packet_mode(render_bench.detail==RENDER_BENCH_PED_PACKETS ? render_bench_mode(&render_bench):1);
            /* Older experiments must still exercise their original renderer. */
            chars_ped_shared_mode(render_bench_native_mode(&render_bench));
            chars_ped_emit_mode(render_bench_emit_mode(&render_bench));
            chars_actor_timing_mode(render_bench_actor_timing(&render_bench));
            chars_ped_math_mode(render_bench_ped_math(&render_bench));
            npcs_occlusion_mode(render_bench_ped_occlusion(&render_bench));
            chars_ped_transform_mode(render_bench_mode(&render_bench) ?
                (render_bench.detail==RENDER_BENCH_PED_TRANSFORM_SCALAR ? 1:
                 render_bench.detail==RENDER_BENCH_PED_TRANSFORM_VFPU ? 2:0):0);
            chars_plan_totals(&plan_before);stage_profile_totals(&clip_before);
            chars_arrow_totals(&arrow_before);
            chars_lod_totals(&detail_before);
            if(render_bench.detail>=RENDER_BENCH_POSE_SPAN)chars_work_totals(actor_before);
            if(render_bench.detail==RENDER_BENCH_PED_PACKETS)chars_ped_packet_totals(&ped_packet_before);
            if(render_bench.detail>=RENDER_BENCH_PED_NATIVE)chars_ped_shared_totals(&ped_shared_before);
            if(render_bench.detail>=RENDER_BENCH_PED_OCCLUSION)npcs_occlusion_totals(&ped_occlusion_before);
            t0=sceKernelGetSystemTimeLow();gfx_frame_start(t0);
            gfx_profile_totals(&render_before);ge_writeback_totals(write_before);
            render_started=sceKernelGetSystemTimeLow();
        }
        /* 03a5d4 shares 03a38c's exact sector branches: the refinery's
           backdrop becomes black with the sky hidden. Consume the pending
           sector before clearing, not one draw after the sky changes. */
        int sky_previous=L.sky.sector;
        citysky_step(&L.sky,freeze || L.pause.active || L.ms.phase==MISSION_PHASE_EXIT ? 0.f:dt);
        stage_water_step(&L.st,freeze || L.pause.active || L.ms.phase==MISSION_PHASE_EXIT ? 0.f:dt);
        int spike_eligible=state==1 && L.loaded && gamemode && !title_mode && !L.demo_on && !recruit_demo_active(&L) && !scene_frozen && !L.pause.active && !L.ms.phase && !L.result.active && !L.campaign_error && !render_bench.active && !freeze;
        if(JGR_DIAGNOSTICS && spike_enabled){
            if(spike_fixture==3 && spike_capture.armed_streak>=SPIKE_CAPTURE_WARMUP) {
                spike_fixture=4;pad.Buttons|=PSP_CTRL_HOME;
                runtime_log("SPIKE_FIXTURE phase=home frame=%u delay_us=55000\n",frames);
                sceKernelDelayThread(55000);
            } else if((spike_fixture==1 || spike_fixture==4) && spike_capture.armed_streak>=SPIKE_CAPTURE_WARMUP) {
                spike_fixture=spike_fixture==4 ? 5:2;
                runtime_log("SPIKE_FIXTURE phase=clean frame=%u armed_streak=%u delay_us=55000\n",frames,spike_capture.armed_streak);
                sceKernelDelayThread(55000);
            }
            gfx_prepare_frame();if(spike_check(&L,frames,spike_eligible,pad.Buttons))break;
            if(spike_fixture==4 && (pad.Buttons&PSP_CTRL_HOME))
                runtime_log("SPIKE_FIXTURE phase=home_rejected frame=%u fired=%u\n",frames,spike_capture.fired);
        }
        gfx_begin(); gfx_clear(L.id==2 && !L.sky.visible ? GCOLOR(0,0,0,255):stage_back_color(L.id)); gu_set(GU_PROJECTION, proj); gu_set(GU_VIEW, view); gfx_mode3d();
        /* Resolve the current city once, before queuing pedestrian poses.
           stage_draw consumes these same masks; no previous-frame occlusion. */
        if(!L.err) {
            gfx_profile_section(GFX_STAGE);
            stage_city_profile_begin(&L.st,0);
            if(!freeze && !L.pause.active && L.ms.phase!=MISSION_PHASE_EXIT && !L.zerr) {
                const Scene *scene=&L.scene.vm;
                const float *event_forward=scene_frozen && scene->camera.active ? scene->camera.forward:NULL;
                const float *handoff=event_forward && L.scene.transferred && L.follow.view.active ? L.follow.camera.eye:NULL;
                float point[3];for(unsigned k=0;k<3;k++)point[k]=handoff ? handoff[k]:eye[k]+(event_forward ? event_forward[k]:0.f);
                stage_render_sector(&L.st,zone_of(&L.zones,point[0],point[1],point[2]));
            }
            {const float actor[3]={L.pl.x,L.pl.y,L.pl.z};
             stage_render_view(&L.st,eye,fwd,1.f/proj[0],actor);
             street_props_set_view(L.street_props,eye,fwd,1.f/proj[0]);}
            stage_render_background(&L.st,L.sky.visible);
            stage_prepare_draw(&L.st,planes,clip,cullmode);
            stage_city_profile_end();
            gfx_profile_section(GFX_OTHER);
        }
        ObjectView objects;object_view_setup(&objects,L.id,eye,fwd,1.f/proj[0],1.f);
        if(scene_frozen)objects.valid=0;
        /* 0c3498 standard task circle with the 0c3344 wedge, shared by
           091f64 set objects and 06b920 traffic. A frozen scene keeps
           drawing everything, like the pickup view above. */
        ObjectView range_view;
        {float ahead=INFINITY,radius=INFINITY;stage_render_medium_circle(&L.st,&ahead,&radius);
         traffic_view_setup(&range_view,&objects,ahead,radius);}
        if(!cherr && !scene_frozen) {
            chars_world_frustum(planes);
            prepare_ped_proxies(&L,eye,planes,&range_view,
                autopilot!=5 || lookat_live || frames<700);
            npcs_prepare_occlusion(&L.npcs,people_err ? NULL:&people,L.err ? NULL:&L.st,
                L.hferr ? NULL:&L.col,eye);
            unsigned prepare_started=JGR_PROFILE_CLOCK();
            prepare_gang_poses(&chars,L.gangs,L.ngangs,view);
            gang_prepare_us+=JGR_PROFILE_CLOCK()-prepare_started;
            prepare_started=JGR_PROFILE_CLOCK();
            npcs_prepare_poses(&L.npcs,&chars,people_err ? NULL:&people);
            npc_prepare_us+=JGR_PROFILE_CLOCK()-prepare_started;
            prepare_started=JGR_PROFILE_CLOCK();chars_pose_submit();
            pose_submit_us+=JGR_PROFILE_CLOCK()-prepare_started;
            chars_world_frustum(NULL);
        }
        if (wire) sceGuDisable(GU_TEXTURE_2D);
        chars_final_lighting(L.final.loaded);
        if(L.final.loaded){
            /* Final Groove's animated architecture uses the character
               renderer. Reject complete off-view models and batches before
               their CPU lighting/skinning, preserving intersecting geometry. */
            chars_world_frustum(planes);chars_batch_frustum(planes);
            chars_final_floor_clip(&L.final.models,clip);
            chars_environment_view(view);finalarena_draw(&L.final);
            chars_final_floor_clip(NULL,NULL);
            const float direction[3]={-view[8],-view[9],-view[10]};finalarena_draw_fire(&L.final,direction);
            chars_batch_frustum(NULL);chars_world_frustum(NULL);
        }
        gfx_profile_section(GFX_SKY);
        if (!L.err) {   /* Original background roots, before depth-tested architecture. */
            float skyproj[16],skyclip[16],skyplanes[6][4];
            m_perspective(skyproj,2.f*atanf(240.f/focal),(float)SCR_W/SCR_H,near_clip,fmaxf(far_clip,20000.f));
            m_mul(skyclip,skyproj,view);frustum_planes(skyclip,skyplanes);
            gu_set(GU_PROJECTION,skyproj);sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();
            { static int nosky=-1;
              if(nosky<0){SceUID t=fixture_open("nosky",PSP_O_RDONLY,0777);nosky=t>=0;if(t>=0)sceIoClose(t);}
              if(!nosky) {
                  if(L.sky.models.h) {
                      int previous=sky_previous;
                      citysky_draw(&L.sky,eye,skyplanes,0.f);
                      if(previous!=L.sky.sector)runtime_log("CITY_SKY sector=%d previous=%d visible=%d event=%d yaw=%u\n",
                          L.sky.sector,previous,L.sky.visible,scene_frozen ? L.scene.vm.id:-1,L.sky.yaw[0][1]);
                  }
                  else if(!L.st.sky_explicit)stage_draw_sky(&L.st);
              }
            }
            if(!freeze && !L.pause.active && L.ms.phase!=MISSION_PHASE_EXIT && !L.zerr) {
                const Scene *scene=&L.scene.vm;
                /* Source camera sampling is separate from the legacy
                   player-based pedestrian gate. EVENT cleanup queries
                   task 1's gameplay-camera +7c, not the skater's feet. */
                const float *event_forward=scene_frozen && scene->camera.active ? scene->camera.forward:NULL;
                const float *handoff=event_forward && L.scene.transferred && L.follow.view.active ? L.follow.camera.eye:NULL;
                citysky_camera(&L.sky,&L.zones,eye,event_forward,handoff);
            }
            gu_set(GU_PROJECTION,proj);
            gfx_profile_section(GFX_STAGE);
            stage_city_profile_begin(&L.st,1);
            stage_draw(&L.st, planes, clip, cullmode);
            stage_city_profile_end();
            if (poltest && frames == 500) for (int k = 1278; k <= 1279; k++) { JgsObj *o = &L.st.obj[k]; printf("obj %d c %.0f,%.0f,%.0f r %.0f:", k, o->cx, o->cy, o->cz, o->r); for (int i = 0; i < 6; i++) printf(" %.0f", planes[i][0] * o->cx + planes[i][1] * o->cy + planes[i][2] * o->cz + planes[i][3]); for (unsigned b = o->first_batch; b < o->first_batch + o->n_batch; b++) printf(" | b%u n%u off%u v%u", b, L.st.batch[b].n_idx, L.st.batch[b].idx_off, L.st.batch[b].vert_off); printf(" drawn_obj %d\n", L.st.drawn_obj); } }
        gfx_profile_section(GFX_PROPS);
        kogane_house_draw(L.house,eye,fwd,planes,&objects,&range_view,0);
        kogane_cover_draw(&L.cover,L.zerr ? NULL:&L.zones,L.cur_zone,eye,planes,0);
        shibuya_gates_draw(L.gates,eye,fwd,planes,0);
        city_openings_draw(L.openings,eye,fwd,planes,0);
        benten_train_draw(L.trains,eye,planes,0);benten_crane_draw(L.crane,planes,&range_view,0);
        grind_props_draw(L.grind_props,eye,planes,clip,&range_view,0,!freeze && !L.pause.active && L.ms.phase!=MISSION_PHASE_EXIT);
        street_props_draw(L.street_props,eye,fwd,1.f/proj[0],clip,0);
        if (L.sobst_ok) for (int i = 0; i < L.nsobrec; i++) {   /* set-placed objects (task 0x8a9): near models first, matching far LODs second */
            int nt = (int)L.sobst.h->n_obj / 2, oi = L.sobrec[i].type;
            if (oi < 0 || oi >= nt) continue;
            /* FUN_8c091f64 first rejects the placement point outside the
               0c3498 circle or the 0c3344 wedge; nothing is drawn or LOD
               selected for it. Shibuya reaches 2000 ahead, Kogane 1100
               (sector overrides 800/2500/3500), the other cities 1650. */
            if(!object_prop_visible(&range_view,L.sobrec[i].x,L.sobrec[i].y,L.sobrec[i].z))continue;
            /* FUN_8c091f64 selects the second model when the squared view
               distance reaches 90000 (300 world units).  Keeping both
               original tables avoids the old damaged-model shortcut and
               preserves the Dreamcast vehicle/prop LOD transition. */
            { float dx = L.sobrec[i].x - eye[0], dy = L.sobrec[i].y - eye[1], dz = L.sobrec[i].z - eye[2]; if (dx*dx + dy*dy + dz*dz >= 90000.f) oi += nt; }
            if (!setobj_visible(&L.sobst, oi, L.sobrec[i].x, L.sobrec[i].y,
                                L.sobrec[i].z, L.sobrec[i].yaw, planes)) continue;
            if (autopilot == 5 && !lookat_live && frames >= 700) break;   /* self-test: second screenshot without the objects */
            sceGumMatrixMode(GU_MODEL); sceGumPushMatrix(); sceGumLoadIdentity(); ScePspFVector3 tv = { L.sobrec[i].x, L.sobrec[i].y, L.sobrec[i].z }; sceGumTranslate(&tv); sceGumRotateY(L.sobrec[i].yaw);
            float model[16],local_clip[16];m_identity(model);
            float c=cosf(L.sobrec[i].yaw),s=sinf(L.sobrec[i].yaw);
            model[0]=model[10]=c;model[2]=s;model[8]=-s;
            model[3]=tv.x;model[7]=tv.y;model[11]=tv.z;m_mul(local_clip,clip,model);
            stage_draw_obj_clipped(&L.sobst,oi,local_clip); sceGumPopMatrix();
        }
        if(!scene_frozen && L.traffic)traffic_draw_world(&L.traffic_models,&L.traffic->world,&range_view,planes);
        if(!scene_frozen)enemycombat_draw(&L.combat,view,planes);
        gfx_profile_section(GFX_ACTORS);
        chars_world_frustum(planes);
        if (!cherr && !scene_frozen) for (int i = 0; i < L.nrv; i++) if (L.rv[i].active) draw_player(&chars,MODEL_OF_CHAR[L.rv[i].chr],&L.rv[i].pl);
        /* The bank's default positions are a model-audit lineup, not source
           mission actors. Normal gameplay draws only actual owned actors. */
        if (!cherr && !scene_frozen) { chars_update(&chars, freeze || L.pause.active || L.ms.phase==MISSION_PHASE_EXIT ? 0.f : dt); chars.drawn_tris = 0; draw_player(&chars,pchar,pl); npcs_draw(&L.npcs, &chars, people_err ? 0 : &people,view,focal);
            CHAR_WORK_BEGIN(other_work);
            if (!cordon_err) police_draw_cordon(&L.police, &cordon);
            for (int i = 0; i < L.ngangs; i++) { Gang *g = &L.gangs[i]; if (!g->active) continue;
                if(g->mode==9)enemycombat_draw_taban(&L.combat,g,view,planes);
                if(g->mode==6)enemycombat_draw_denki(&L.combat,g,planes,!freeze && !L.pause.active && L.ms.phase!=MISSION_PHASE_EXIT);
                if(g->mode==5) {
                    if(g->flags&0x120)continue;
                    enemycombat_draw_sniper(&L.combat,g,view,planes);
                    if(g->flags&0x10000)continue;
                }
                if(gang_mover_hidden(g) || (g->mode==6 && (g->flags&0x10120)))continue;
                if(g->mode==4) {
                    if(g->flags&0x120)continue;
                    if(g->flags&0x1000000) {
                        if(!heliwreck_err){float root[16];gang_member_matrix(g,g->m,root);heliwreck_draw(&heliwreck,&g->wreck,root);}
                        continue; /* never draw the flight tree over the wreck */
                    }
                }
                for (int j = 0; j < g->nmem; j++) {
                    GangMember *m = &g->m[j];int ch=gang_model(g->kind);
                    if((gang_source_member(g) || g->kind==23 || g->kind==25 || g->mode==7) && (m->flags&0x120))continue;
                    if(g->mode==7 && (m->flags&0x200000))continue;
                    if(gang_source_member(g) && m->police.bomb && m->anim==20) {
                        enemycombat_draw_bomb(&L.combat,g,m,planes);continue;
                    }
                    int motion = gang_motion(g, m, ch);
                    float frame = gang_render_frame(g,m,motion);
                    if (m->motion != motion) { m->motion = motion; if (!gang_source_member(g) && g->mode != 1 && g->mode != 2 && g->mode != 7 && g->mode != 8 && g->mode != 4 && g->mode != 9 && g->mode != 6 && g->mode != 5) m->frame = frame = 0.f; }
                    float root[16];gang_member_matrix(g,m,root);
                    /* Source draw gate on the camera depth of the actor's
                       bound centre: 06dd7c fades or 0c9d50 cuts. The frustum
                       test below stands in for the original viewport cone. */
                    int view_mode;
                    float gate_alpha=gang_view_alpha(g,root,view,&view_mode);
                    if(!(gate_alpha>0.f))continue;
                    int source_model=ch;ch=chars_source_lod_model(&chars,ch,view_mode);
                    /* Encounter children do not pass through npcs_draw.
                       Their custom police poses and full bike/rider meshes
                       must opt into the same scoped visual detail tier. */
                    if(((gang_source_member(g) && chars.chars[ch].nbones<=20) ||
                        (g->mode==8 && chars.chars[ch].nbones>=3 && chars.chars[ch].nbones<=64)) &&
                       !chars_actor_visible(&chars,ch,root))continue;
                    int previous_detail=chars_npc_detail_begin(gang_detail_model(source_model) ? root:NULL,view,focal);
                    if(gate_alpha<1.f)chars_fade_begin(gate_alpha);
                    if(g->kind==6 && enemycombat_draw_strong(&L.combat,g,m,root,planes)) {
                        /* Original alternate attack tree replaces the body. */
                    } else if(gang_source_member(g) && chars.chars[ch].nbones<=20) {
                        const JgcChar *model=chars.chars+ch;CharPose pose[20],other;
                        if(m->police.fire)enemycombat_fire_pose(&chars,model,m,pose);
                        else for(unsigned b=0;b<model->nbones;b++) {
                            chars_sample_pose(&chars,model,model->motion0+motion,frame,b,pose+b);
                            if(m->police.blend>0 && m->police.old_motion>=0 && (unsigned)m->police.old_motion<model->nmotions) {
                                chars_sample_pose(&chars,model,model->motion0+m->police.old_motion,m->police.old_frame,b,&other);
                                chars_blend_pose(pose+b,&other,m->police.blend);
                            } else if((g->kind==0 || g->kind==4 || g->type==3 || g->type==7 || m->police.bomb || m->police.fire || m->police.kairiki) && (m->anim==1 || (g->kind==4 && m->speed!=0 && gang_riot_overlay(m)>=0)) && m->police.gait>0) {
                                chars_sample_pose(&chars,model,model->motion0+(gang_police_gait_motion(m)),frame,b,&other);
                                chars_blend_pose(pose+b,&other,m->police.gait);
                            }
                        }
                        if(g->kind==2 && m->anim==8 && m->police.head)for(unsigned b=0;b<model->nbones;b++)
                            chars_overlay_pose(&chars,model,model->motion0+1,gang_dog_turn_frame(m),b,1.f,pose+b);
                        chars_draw_one_pose(&chars,ch,root,pose,g->kind==4?gang_riot_overlay(m):-1,g->kind==4?gang_riot_overlay_frame(m):m->secondary_frame,g->kind==4?1.f:0.f);
                        if(g->kind==23)enemycombat_draw_hands(&L.combat,m,root,pose,view,planes);
                    } else if(g->mode==2 && source_model==39) {
                        onishima_draw(&chars,ch,g,root);
                    } else if(g->mode==8 && chars.chars[ch].nbones>=3 && chars.chars[ch].nbones<=64) {
                        const JgcChar *model=chars.chars+ch;CharPose pose[64];
                        for(unsigned b=0;b<model->nbones;b++)chars_sample_pose(&chars,model,model->motion0+motion,frame,b,pose+b);
                        pose[2].ang[1]+=m->aim;
                        chars_draw_one_pose(&chars,ch,root,pose,-1,0,0);
                    } else if(g->mode==5) {
                        chars_draw_one_xf_blend(&chars,ch,root,motion,frame,
                            g->unit_state==3?12:-1,frame,g->unit_state==3?fmaxf(0.f,1.f-g->speed/1.388888955116272f):0.f,
                            g->unit_state<2?m->secondary_anim:-1,0,g->unit_state<2?m->sniper.blend:0.f);
                    } else if(g->mode==7 && m->secondary_anim>=17 && m->secondary_anim<=19) {
                        chars_draw_one_xf_blend(&chars,ch,root,motion,frame,-1,0,0,
                            m->secondary_anim-8,m->secondary_frame,1.f);
                    } else if(g->mode==9 && g->unit_state==1) {
                        chars_draw_one_xf_blend(&chars,ch,root,motion,frame,gang_taban_gait_motion(),frame,
                                                m->chief.gait,-1,0,0);
                    } else if(g->mode==3 && g->kind==11) {
                        /* 07fd8c: solid car first, additive beacon last. */
                        chars_draw_vehicle(&chars,ch,root,motion,frame);
                    } else chars_draw_one_xf(&chars,ch,root,motion,frame);
                    if(gate_alpha<1.f)chars_fade_end();
                    chars_npc_detail_end(previous_detail);
                }
            }
            CHAR_WORK_END(CHAR_WORK_OTHER,other_work);
        }
        chars_world_frustum(NULL);
        gfx_profile_section(GFX_EFFECTS);
        if (!cans_err && !L.gferr) cans_draw(&L.gf, &cans, &L.health_models, eye, planes,&objects);
        gu_set(GU_PROJECTION,proj);gu_set(GU_VIEW,view);
        gfx_profile_section(GFX_ACTORS);
        if(scene_frozen)sceneplay_draw(&L.scene,cherr ? 0:&chars);
        gfx_profile_section(GFX_PROPS);
        kogane_house_draw(L.house,eye,fwd,planes,&objects,&range_view,1);
        kogane_cover_draw(&L.cover,L.zerr ? NULL:&L.zones,L.cur_zone,eye,planes,1);
        shibuya_gates_draw(L.gates,eye,fwd,planes,1);
        city_openings_draw(L.openings,eye,fwd,planes,1);
        benten_train_draw(L.trains,eye,planes,1);benten_crane_draw(L.crane,planes,&range_view,1);
        grind_props_draw(L.grind_props,eye,planes,clip,&range_view,1,!freeze && !L.pause.active && L.ms.phase!=MISSION_PHASE_EXIT);
        street_props_draw(L.street_props,eye,fwd,1.f/proj[0],clip,1);
        gfx_profile_section(GFX_EFFECTS);
        if(!scene_frozen && paintfx_err==1) {
            paintfx_err=chars_load(&paintfx,"paintspray.jgc");
            runtime_log("PAINT_MIST load=%d\n",paintfx_err);
        }
        /* Effect models have the same conservative, animated owner bounds
           as actors. Keep their simulation/history running offscreen. */
        chars_world_frustum(planes);
        if(!scene_frozen) {
            enemycombat_draw_player_fire(&L.combat,view,planes);
            draw_skate_effects(pl);
        }
        if(!scene_frozen && L.water_splash.h && pl->water.splash) {
            float matrix[16];const int32_t angles[3]={0};const float scale[3]={2,2,2};
            player_scene_matrix(pl->water.splash_position,angles,scale,matrix);
            chars_draw_effect(&L.water_splash,0,matrix,pl->water.splash_frame,1.f);
        }
        if(!scene_frozen && !paintfx_err)for(int i=0;i<L.ngangs;i++) {
            float mist[16];Gang *g=&L.gangs[i];
            if(rival_spray_matrix(g,&chars,gang_model(g->kind),mist))
                /* 0dfa7c: effect slot 0, fixed frame +130 initialized to 0. */
                chars_draw_effect(&paintfx,0,mist,0.f,1.f);
        }
        if(!scene_frozen && !title_mode && !L.gferr) {
            const unsigned short *art[3]={L.city_tags.art[0],L.city_tags.art[1],L.city_tags.art[2]};
            if(L.final.loaded){art[0]=L.final_art.small;art[1]=NULL;art[2]=L.final_art.large;}
            spray_bubble_draw(&pl->effects.bubble,art,view);
        }
        for (int i=0;i<L.nrv;i++) if (L.rv[i].active) draw_skate_effects(&L.rv[i].pl);
        chars_world_frustum(NULL);
        /* All arrow owners share this bank, including encounters and Goji.
           Defer until EVENT retirement to preserve the intro allocation. */
        if(!scene_frozen && !title_mode && !L.tag_arrows.h && !L.tag_arrows_error) {
            L.tag_arrows_error=chars_load(&L.tag_arrows,"tagarrows.jgc");
            runtime_log("GRAFFITI_ARROWS load=%d models=%u\n",L.tag_arrows_error,L.tag_arrows.h ? L.tag_arrows.h->n_chars:0);
        }
        chars_arrow_projection(proj);
        L.tag_arrows_drawn=0;
        if (!L.gferr) {
            if(L.final.loaded) {
                if(!scene_frozen)finaltags_draw(&L.final.tags,&L.gf,&L.final_art);
                L.tag_arrows_drawn=graffiti_arrows_draw_final(&L.tag_arrows,L.final.tags.objects,&L.gf,view,title_mode || scene_frozen);
            } else {
                gfx_profile_section(GFX_TAGS);
                citytags_draw_view(&L.city_tags,&L.gf,&objects);
                gfx_profile_section(GFX_EFFECTS);
                L.tag_arrows_drawn=graffiti_arrows_draw_view(&L.tag_arrows,&L.gf,view,eye,title_mode || scene_frozen,&objects);
            }
        }
        /* 069a7c's source actor flags decide whether an event-0c rival
           has a GF; 019cfc uses that actor's PLAY +74 arrow descriptor. */
        if(!title_mode && !scene_frozen && L.mrec)
            for(int i=0;i<L.nrv;i++) {
                const struct Rival *r=L.rv+i;
                if(!r->active || !graffiti_arrow_rival_enabled(L.mrec->kind,r->table))continue;
                float root[16];player_root_matrix(&r->pl,root);
                GfoObject *o=gfo_find(&L.object_graffiti,r->graffiti);
                int digits=o ? hud_draw_rival_counter(&o->progress,root,view):0;
                if(autopilot && !(frames%60))runtime_log("RIVAL_VIEW frame=%d slot=%d character=%d segment=%d started=%d record=%d position=%.6f,%.6f,%.6f motion=%d stroke=%u reveal=%u digits=%d depth=%.6f\n",
                    frames,r->slot,r->chr,r->seg,r->started,r->play.pos,r->pl.x,r->pl.y,r->pl.z,r->pl.logical_motion,o?o->progress.stroke:0,o?o->progress.reveal:0,digits,
                    view[8]*root[3]+view[9]*root[7]+view[10]*root[11]+view[11]);
                if(!r->tagged)L.tag_arrows_drawn+=graffiti_arrow_draw_at(&L.tag_arrows,graffiti_arrow_skater(r->chr),root,view,1,r->arrow_frame);
                if(!cherr && o) {
                    if((unsigned)r->table<3 && L.rival_route[r->table].image)
                        gfo_draw_actor_pose(o,L.object_graffiti_texture,r->started ? r->tag_pose:r->pl.pose,root);
                    else gfo_draw_actor(o,L.object_graffiti_texture,&chars,MODEL_OF_CHAR[r->chr],r->pl.motion,r->pl.frame,root);
                }
            }
        if(!cherr && !scene_frozen)for(int i=0;i<L.ngangs;i++) {
            Gang *g=L.gangs+i;GfoObject *o=gfo_find(&L.object_graffiti,g->graffiti_handle);
            if(!g->active || (!o && g->mode!=1))continue;
            int ch=gang_model(g->kind);
            for(int j=0;j<g->nmem;j++) {
                const GangMember *member=g->m+j;
                float root[16];gang_member_matrix(g,member,root);
                int arrow=gang_graffiti_arrow(g,member);
                if(!title_mode && arrow>=0) {
                    {
                        int drawn=graffiti_arrow_draw_object(&L.tag_arrows,o,root,view,0,g->arrow_frame,0);
                        L.tag_arrows_drawn+=drawn;
                        /* 06e074 advances its shared owner clock AFTER each
                           eligible member draw, unlike the static task. */
                        if(drawn && !L.pause.active && !render_bench_repeat){g->arrow_frame+=1.f;if(g->arrow_frame>=120.f)g->arrow_frame-=120.f;}
                    }
                }
                if(!(gang_graffiti_policy(g,member)&GANG_TAG_DRAW))continue;
                float frame=g->mode==7 && member->secondary_anim>=14 && member->secondary_anim<=16 ? member->secondary_frame:member->frame;
                gfo_draw_actor(o,L.object_graffiti_texture,&chars,ch,gang_motion(g,member,ch),frame,root);
            }
        }
        chars_arrow_projection(NULL);
        /* Warm the averages during the hold so opening the overlay shows
           current readings. These coarse clocks are independent of the
           detailed per-actor/triangle profiler in every build. */
        if(JGR_DIAGNOSTICS || viewer_select_state.stats || viewer_select_state.held){unsigned t_cpu = sceKernelGetSystemTimeLow(); cpu_avg += ((t_cpu - t0) * 1e-3f - cpu_avg) * 0.1f;}

        /* Supplied contact-response fixture, never a production event or
           a claim that this camera placement earned a traffic collision. */
        if(hudpolishtest && !hudpolish_started && !scene_frozen && !L.pause.active && !L.ms.phase && frames>=1300){
            int before=pl->health;
            int accepted=player_receive_vehicle(pl,0,(int)(pl->yaw*65536.f/(2*3.14159265f)));
            hudpolish_started=accepted;
            runtime_log("HUD_IMPACT supplied=1 accepted=%d health=%d/%d writable=%d\n",accepted,pl->health,before,campaign_writable);
        }
        /* HUD */
        gfx_profile_section(GFX_HUD);
        if(!scene_frozen && !render_bench_repeat) {
            /* Bind the recovered cone to the current port lens. General
               gameplay camera/lens parity remains a separate audit. */
            int fov=2*player_bam_atan2(320.f,focal);
            if(L.pl.spray_active && L.pl.spray_camera.active)fov=L.pl.spray_camera.fov;
            police_observe_cordon(&L.police,view,fov,player_fsca_sin);
        }
        if(!title_mode && !scene_frozen && !L.pause.active && !L.ms.phase && !hudpol_err)
            police_hud_draw_alert(&hudpol,L.police.level,L.police.lframe,&L.police.arrow,L.police.arrow_frame,proj,view);
        if(!title_mode && !scene_frozen && !render_bench_repeat && L.police.level>=4)police_alert_kill(&L.police); /* 049684 */
        if (title_mode && tlogo_ok && L.demo_tick_n<465) {   /* FUN_8c017e08: logo bank drawn with its own camera over the demo (eye/target from the bank's camera motion) */
            float tproj[16], tview[16], teye[3] = { -10.6342239f, 28.193449f, 604.963745f }, tfwd[3] = { 13.8656473f, -23.7004757f, -607.210876f };
            /* U_TITLE camera angle is 0x471c (017e08). Preserve its
               authored camera/root, then map the logo and prompt through
               the same uniform, centered HUD canvas. */
            float logo_focal=player_camera_focal(0x471c,640.f,1.f);
            m_perspective(tproj,2.f*atanf(240.f/logo_focal),640.f/480.f,.1f,32768.f);
            hud_projection(tproj,HUD_CENTRE);
            player_camera_matrix(teye,tfwd,0,tview);
            gu_set(GU_PROJECTION, tproj); gu_set(GU_VIEW, tview); sceGuClear(GU_DEPTH_BUFFER_BIT);
            /* 017774 plays 0..159 once, then loops 160..259 while the
               large title panel is alive (through its idle/fade states). */
            float logo_frame=(float)L.demo_tick_n+L.replay_clock.remainder*30.f;
            if(logo_frame>=260.f)logo_frame=160.f+fmodf(logo_frame-160.f,100.f);
            /* 0180d6 translates and scales the bank just in front of the
               logo camera. Preserve that transform instead of compensating
               for a missing transform with an enlarged focal distance. */
            float logo_xf[16];m_identity(logo_xf);
            logo_xf[0]=logo_xf[5]=logo_xf[10]=.0170000009f;
            logo_xf[3]=-10.6000004f;logo_xf[7]=28.1499996f;logo_xf[11]=600.f;
            for (int i = 0; i < 4; i++) {
                /* 017e08 holds the first three roots in their bind pose
                   once state 4 starts. Only the outline keeps animating. */
                const JgcChar *ch=&tlogo.chars[i];
                if(L.demo_tick_n>=221 && i<3 && ch->nbones<=32) {
                    CharPose pose[32];
                    for(unsigned bi=0;bi<ch->nbones;bi++)chars_sample_pose(&tlogo,ch,-1,0.f,bi,&pose[bi]);
                    chars_draw_one_pose(&tlogo,i,logo_xf,pose,-1,0.f,0.f);
                } else chars_draw_one_xf(&tlogo, i, logo_xf, 0, logo_frame);
            }
        }
        if(!title_mode && !scene_frozen && !L.pause.active && !L.gferr && !L.ms.phase){
            finish_hud_draw(&finishhud,&L.gf.finish,proj,view);
            spray_hud_draw(&L.gf);
        }
        gfx_mode2d();
        /* Like the cue tasks, the score owner also exists in standalone
           Final arena play; it is independent of the mission score flag. */
        if(!title_mode && !scene_frozen && !L.pause.active && !L.gferr && !L.ms.phase && !L.campaign_error)
            hud_draw_spray_score(&L.gf.score_hud);
        if(title_mode && title_ui.data && title_lists.data) {
            /* 018408 draws U_TITLE+0x30 with a +118 Y translation:
               (195,280) becomes (195,398), below the large logo.
               017774 creates the prompt after 60 logo-loop ticks;
               0183ec cycles 0..30 and 018408 shows phases 0..14. */
            if(L.demo_tick_n>=220 && L.demo_tick_n<465 && ((L.demo_tick_n-220)%31)<15)
                list2d_draw_hud(&title_lists,2,&title_ui,0,118,HUD_CENTRE);
            /* After the original 210-tick idle and 30-tick fade, 05bb1c
               owns the small corner logo and upper-left attract prompt. */
            if(L.demo_tick_n>=465) {
                list2d_draw_hud(&title_lists,1,&title_ui,0,0,HUD_RIGHT);
                if((L.demo_tick_n%31)<15)list2d_draw_hud(&title_lists,3,&title_ui,0,0,HUD_LEFT);
            }
        }
        char buf[128]; float fps = ms_avg > 0.01f ? 1000.f / ms_avg : 0.f;
        if(L.final.loaded && viewer_select_state.stats) {
            snprintf(buf,sizeof buf,"FINAL PHASE %d/8%s",L.final.phase,L.final.progression_error ? " - EVENT/ASSET ERROR":L.final.clear_requested ? " - ENDING REQUESTED":"");
            font_draw(4,254,1,GCOLOR(255,230,80,255),buf);
        }
        int dbg=!render_bench.active && viewer_select_state.stats;
        if (dbg) gfx_quad(4, 4, 472, 54, GCOLOR(0, 0, 0, 180));
        if (dbg) snprintf(buf, sizeof buf, "SCORE %6d  CANS %d  TAGS %d/%d  %s", L.score, L.gferr ? 0 : L.gf.cancount, L.gferr ? 0 : L.gf.tagged, L.gferr ? 0 : L.gf.n, (!L.gferr && L.gf.near >= 0) ? (L.gf.mg_active ? L.gf.mg_text : spraying ? "SPRAYING!" : (L.gf.spots[L.gf.near].size >= 2 ? "PRESS L TO START TAG" : "PRESS L TO TAG")) : (pl->grinding ? "GRINDING" : ""));
        if (dbg) font_draw(8, 8, 2, GCOLOR(255, 214, 26, 255), buf);
        /* Retain usable instructions if the original HUD bank is missing. */
        if(sprayhud_error && !title_mode && !scene_frozen && !L.gferr && L.gf.mg_active && !L.ms.phase) {
            gfx_quad(16,210,448,48,GCOLOR(0,0,0,180));
            const char *cue=L.gf.mg_flash>0 ? "MISS" : L.gf.mg_wait ? "OK" : L.gf.mg_text;
            font_real_draw_center(240,214,.9f,GCOLOR(255,235,80,255),cue);
            snprintf(buf,sizeof buf,"%d / %d   CANS %d   R / X: EXIT",L.gf.mg_stroke,L.gf.mg_nstrokes,L.gf.cancount);
            font_real_draw_center(240,235,.65f,GCOLOR(255,255,255,255),buf);
        }
        if (L.ms.active && !title_mode && !scene_frozen && !L.campaign_error) {
            if(trickhudtest && frames>=400 && !L.ms.phase && !L.pause.active){
                if(!trickhud_started){trickhud_started=1;trickhud_base=pl->logic_tick;
                    runtime_log("TRICK_HUD supplied score events; production player/HUD clocks; not earned tricks or save writes\n");}
                int age=(int)pl->logic_tick-trickhud_base;
                for(int kind=0;kind<7;kind++)if(age>=kind*25 && !(trickhud_events&(1u<<kind))){
                    trickhud_events|=1u<<kind;
                    int name=kind<2 ? TRICK_GRIND_IDS[pl->character_id][!!pl->mirrored][2]:
                        kind<4 ? TRICK_AIR_IDS[pl->character_id][!!pl->mirrored][kind==3 ? 2:0]:0;
                    trick_hud_event(&pl->trick_hud,&pl->combo,&pl->grind_dist,0,kind,name);
                    runtime_log("TRICK_HUD event=%d age=%d name=%d combo=%d score=%d\n",kind,age,name,pl->combo,L.score);
                }
                if(age>=175 && !(trickhud_events&128)){
                    trickhud_events|=128;trick_hud_finish(&pl->trick_hud,&pl->combo,&pl->grind_dist);
                }
                if(age>=225 && trickhudtest==1){
                    int active=0;for(int i=0;i<15;i++)active+=pl->trick_hud.task[i].type!=0;
                    runtime_log("TRICK_HUD done age=%d score=%d combo=%d active=%d writable=%d\n",age,L.score,pl->combo,active,campaign_writable);
                    trickhudtest=2;
                }
            }
            if(messagehudtest && !messagehud_started && frames>=400 && !L.ms.phase){
                for(unsigned at=0;at+16<=L.ms.len;at+=4){
                    unsigned event=L.ms.base+at;
                    if(*(unsigned *)(L.ms.img+at)==0x10 && *(unsigned *)(L.ms.img+at+12)==1 &&
                       mission_message_event(&L.ms,event)>0){
                        messagehud_started=1;hud_notice_acquired(&L.notice,15);L.gf.cancount=15;
                        runtime_log("HUD_MESSAGE fixture event=%08x supplied notification/inventory; no acquisition, clear or save writes\n",event);break;
                    }
                }
            }
            if(!font_real_wide_ready()){
                if(font_real_load_wide("fontwide.bin")){L.campaign_error=10;runtime_log("HUD original wide font unavailable scene=%d phase=%d resident=%d heap=%u\n",L.scene.vm.id,L.scene.vm.phase,sceneplay_resident(&L.scene),(unsigned)mallinfo().fordblks);}
                else L.hud_wide_owned=1;
            }
            {int id=story_notification_take();if(id>=0)hud_notice_acquired(&L.notice,id);}
            if(L.ms.message_error){L.campaign_error=10;printf("HUD invalid original mission message\n");}
            if(L.message_serial!=L.ms.message.serial){
                L.message_serial=L.ms.message.serial;
                /* 0b02b8(21) is the Dreamcast vibration pattern, NOT an
                   SMSB program. PSP has no matching vibration peripheral. */
                runtime_log("HUD_MESSAGE start serial=%d voice=%d bytes=%u prepare=%d\n",L.message_serial,L.ms.message.voice,(unsigned)strlen(L.ms.message.text),audio_message_request(L.ms.message.voice));
            }
            if(L.health_hud.maximum!=pl->health_max){health_hud_init(&L.health_hud,pl->health_max);L.health_hud_tick=pl->logic_tick;}
            /* Actor logic clock, never the render frame count. */
            unsigned elapsed=pl->logic_tick-L.health_hud_tick;L.health_hud_tick=pl->logic_tick;
            if(L.result.active){elapsed=(unsigned)L.result.clock-L.result_hud_tick;L.result_hud_tick=L.result.clock;}
            if(elapsed>8)elapsed=0; /* scene/task replacement, not elapsed play */
            float can_point[3]={pl->x,pl->y+10,pl->z},camera_point[3];
            for(int k=0;k<3;k++)camera_point[k]=view[k*4]*can_point[0]+view[k*4+1]*can_point[1]+view[k*4+2]*can_point[2]+view[k*4+3];
            float can_x=240.f/HUD_SCALE,can_y=240;
            if(camera_point[2]!=0){can_x-=focal*camera_point[0]/camera_point[2];can_y+=focal*camera_point[1]/camera_point[2];}
            for(unsigned kind=0;kind<2;kind++)while(L.gf.health_pickups[kind]) {
                L.gf.health_pickups[kind]--;can_hud_play_health(&L.can_hud,kind,can_x,can_y);
            }
            while(elapsed--) {
                /* player_tick already advances pending trick tasks during
                   results; this result-clock loop must not tick them twice. */
                hud_notice_present(&L.notice,L.mrec ? L.mrec->flags:0,!L.pause.active);
                if(!L.pause.active && message_hud_tick(&L.ms.message))
                    runtime_log("HUD_MESSAGE voice=%d error=%d\n",L.ms.message.voice,audio_message_voice(L.ms.message.voice));
                if(!L.result.active)health_hud_tick(&L.health_hud,pl->health,pl->tun->health_low,0);
                if(!L.gferr)can_hud_play_tick(&L.can_hud,&L.gf.cancount,pl->tun->can_capacity,can_x,can_y,player_fsca_sin);
                int time=recruit_result_hud_time(&L.result,L.ms.time_left),voice;
                if(L.result.active)L.timer_hud.ticks=time; /* resident result task owns +74 */
                timer_hud_tick(&L.timer_hud,time);
                timer_hud_present(&L.timer_hud,L.mrec ? L.mrec->flags:0,&voice);
                if(voice>=0) {
                    int error=audio_countdown_voice(voice);
                    if(error)printf("HUD countdown voice %d unavailable\n",voice);
                    if(autopilot && logfd>=0) {
                        char line[96];int n=snprintf(line,sizeof line,"COUNTDOWN_VOICE clock=%d ref=%d error=%d\n",L.timer_hud.clock,voice,error);
                        runtime_log_write(logfd,line,n);
                    }
                }
            }
            unsigned hud_flags=L.mrec ? L.mrec->flags:0;
            if(L.pause.active)hud_flags|=HUD_HIDE_CANS; /* 03355c task-22 +a0=0 */
            if(!(hud_flags&HUD_HIDE_TIME) && L.timer_hud.countdown && L.timer_hud.previous>=0) {
                gfx_mode3d();countdown_hud_draw(&countdown_models,L.timer_hud.previous,proj,view);gfx_mode2d();
            }
            if(!(hud_flags&HUD_HIDE_CANS) && L.can_hud.children && L.can_hud_model.h) {
                /* 04e8d4: a separate 415-focal camera, whose screen centre
                   follows the recovered spring particle; only Z translates
                   its original effect-bank slot-10 mesh. */
                float hp[16],hv[16];m_ident(hv);m_perspective(hp,2.f*atanf(240.f/415.f),640.f/480.f,1,1000);
                gfx_mode3d();gu_set(GU_VIEW,hv);chars_environment_view(hv);sceGuClear(GU_DEPTH_BUFFER_BIT);
                /* Common stage setup -> 0c3ac4 overwrites the
                   executable's initial (-1,-1,-1)/white light globals. */
                CharLight light={{0},1,0,{0xff0070ad,0xfffeb455},1};
                const float direction[3]={-.01f,-.6f,.04f};
                can_hud_view_light(view,direction,light.direction);
                chars_scene_lighting(&light,&light);
                for(CanFlightTask *t=L.can_hud.children;t;t=t->next) {
                    const CanFlight *f=&t->flight;float root[16];can_flight_model(f,root,player_fsca_sin);
                    hp[0]=415.f/320.f;hp[2]=-(f->position[0]-320)/320;hp[6]=(f->position[1]-240)/240;
                    hud_projection(hp,HUD_LEFT);
                    gu_set(GU_PROJECTION,hp);
                    if(t->health_kind) {
                        for(int r=0;r<3;r++)for(int c=0;c<3;c++)root[r*4+c]*=.5f;
                        chars_draw_one_rest(&L.health_models,t->health_kind-1,root);
                    }else chars_draw_material(&L.can_hud_model,0,root,NULL,-1,0,1,NULL);
                }
                chars_scene_lighting(NULL,NULL);gfx_mode2d();
            }
            if(!L.pause.active){
                hud_draw_tricks(&pl->trick_hud); /* original depth -2 */
                hud_draw_game(&L.health_hud,&L.can_hud.counter,hud_flags|HUD_HIDE_TIME,L.score,0,player_fsca_sin);
                hud_draw_timer(&L.timer_hud,hud_flags);
                /* Original notice depth -.1 is in front of radio artwork
                   (-.15..-.2). Preserve that order in the PSP 2-D pass. */
                hud_draw_message(&L.ms.message);hud_draw_notice(&L.notice);
            }
            /* LEFT TO CLEAR belongs to the original pause/result routes,
               not an unconditional live overlay across the scene. */
            if(dbg){snprintf(buf,sizeof buf,"%d LEFT TO CLEAR",L.ms.nreq-L.ms.ntagged);font_real_draw(8,66,1.f,GCOLOR(255,255,255,255),buf);}
            if (L.ms.nsub > 0 && L.ms.sub_t > 0.f) { gfx_quad(20, 272 - 19 * L.ms.nsub - 8, 440, 19 * L.ms.nsub + 6, GCOLOR(0, 0, 0, 170)); for (int i = 0; i < L.ms.nsub; i++) font_real_draw_center(SCR_W / 2, 272 - 19 * (L.ms.nsub - i) - 4, 1.f, GCOLOR(255, 255, 255, 255), L.ms.sub[i]); }
            if (dbg) for (int i = 0; i < L.ms.nlog && i < 4; i++) font_draw(304, 60 + i * 10, 1, GCOLOR(180, 200, 220, 255), L.ms.log[i]);
        }
        if(L.campaign_error) {
            gfx_quad(20,94,440,84,GCOLOR(0,0,0,220));
            font_draw_center(240,106,1,GCOLOR(255,160,100,255),"CAMPAIGN DATA / HANDOFF UNAVAILABLE");
            snprintf(buf,sizeof buf,"ERROR %d  SEE LOG  -  START: RETURN",L.campaign_error);
            font_draw_center(240,142,1,GCOLOR(255,255,255,255),buf);
        }
        if (dbg) snprintf(buf, sizeof buf, "%4.1f MS %3.0f FPS  CPU %3.1f  OBJ %d TRI %d  %s  BUSTED %d", ms_avg, fps, cpu_avg, L.st.drawn_obj, L.st.drawn_tris, gamemode ? (pl->grinding ? "GRIND" : pl->onground ? "GND" : "AIR") : "FREECAM", L.hits);
        if (dbg) font_draw(8, 26, 1, GCOLOR(220, 220, 220, 255), buf);
        if(dbg) {
            if(JGR_DIAGNOSTICS && render_bench_available)font_draw(8,80,1,GCOLOR(180,220,255,255),"TRIANGLE: BENCHMARK THIS VIEW");
            if(vfpu_scope_valid)snprintf(buf,sizeof buf,"VFPU PATHS ~%.2f MS  %.1f%% OF 30FPS BUDGET",vfpu_scope_ms,vfpu_budget30_pct);
            else snprintf(buf,sizeof buf,"VFPU PATHS: DETAILED TIMING UNAVAILABLE");
            font_draw(8,36,1,GCOLOR(180,220,255,255),buf);
            int me_status=audio_me_status();
            const char *me_text=me_status==AUDIO_ME_FULL ? "ME ON - FULL AUDIO":
                me_status==AUDIO_ME_REVERB ? "ME ON - REVERB ONLY":"ME OFF - CPU AUDIO";
            unsigned me_color=me_status==AUDIO_ME_FULL ? GCOLOR(100,255,140,255):
                me_status==AUDIO_ME_REVERB ? GCOLOR(255,210,100,255):GCOLOR(255,120,120,255);
            font_draw(8,46,1,me_color,me_text);
            if(!JGR_DIAGNOSTICS)snprintf(buf,sizeof buf,"ME POSE TIMING UNAVAILABLE");
            else if(pose_me_last)snprintf(buf,sizeof buf,"ME POSES ON  JOIN %.2f MS",pose_wait_last);
            else snprintf(buf,sizeof buf,"ME POSES IDLE THIS WINDOW");
            font_draw(8,56,1,GCOLOR(180,220,255,255),buf);
        }
        if (!gamemode || (pad.Buttons & PSP_CTRL_LTRIGGER)) { snprintf(buf, sizeof buf, "", (int)pl->x, (int)pl->y, (int)pl->z); font_draw(8, 260, 1, GCOLOR(200, 200, 200, 255), buf); }
        if(L.result.active && !sceneplay_resident(&L.scene) && !L.campaign_error){
            gfx_quad(0,0,480,272,GCOLOR(0,0,0,L.result.alpha));
            gfx_mode3d();result_hud_draw(&failure_models,&L.result,L.mrec ? L.mrec->kind:0,L.ms.res_rank,L.result.souls,proj,view);gfx_mode2d();
            if(result_hud_visible(&L.result)){
                /* 036b6c: measure/truncate in source pixels, then scale.
                   The soul row moves the score AND heading up together. */
                if(RESULT_FLAGS[L.result.class_id][6] && L.result.souls>0){
                    char number[24];result_hud_number(number,L.result.souls);snprintf(buf,sizeof buf,RESULT_TEXT[1],number);
                    font_real_draw_xy(result_hud_text_x(font_real_source_width(buf)),200.f*272.f/480.f,HUD_SCALE,HUD_SCALE,GCOLOR(230,230,0,255),buf);
                }
                if(RESULT_FLAGS[L.result.class_id][1]){
                    char number[24];result_hud_number(number,L.result.displayed);snprintf(buf,sizeof buf,RESULT_TEXT[0],number);
                    font_real_draw_xy(result_hud_text_x(font_real_source_width(buf)),result_hud_score_y(&L.result)*272.f/480.f,HUD_SCALE,HUD_SCALE,GCOLOR(230,230,0,255),buf);
                }else if(RESULT_FLAGS[L.result.class_id][8])font_real_draw_xy(hud_x(200,HUD_CENTRE),200.f*272.f/480.f,HUD_SCALE,HUD_SCALE,GCOLOR(230,230,0,255),RESULT_TEXT[3]);
                const char *joined=result_hud_joined_name(&L.result,1);
                if(joined){
                    snprintf(buf,sizeof buf,RESULT_TEXT[2],joined);
                    font_real_draw_xy(result_hud_text_x(font_real_source_width(buf)),200.f*272.f/480.f,HUD_SCALE,HUD_SCALE,GCOLOR(230,230,0,255),buf);
                }
            }
            if(L.result.fade>0.f)gfx_quad(0,0,480,272,GCOLOR(0,0,0,(int)(255.f*L.result.fade)));
        }
        if(L.failure.active){
            gfx_quad(0,0,480,272,GCOLOR(0,0,0,L.failure.alpha));
            gfx_mode3d();failure_hud_draw(&failure_models,&L.failure,L.mrec ? L.mrec->kind:0,L.mrec && L.mrec->mid!=0,proj,view);gfx_mode2d();
            if(L.failure_fade>0)gfx_quad(0,0,480,272,GCOLOR(0,0,0,(int)(255.f*L.failure_fade)));
        }
        if(L.pause.active){
            if(L.pause.phase>=2 && !L.pause.practice){
                L.pause_map.clock=L.pause.clock;
                int capacity=1+L.nrv+L.ngangs*GANG_MAX_MEMBERS+(L.gferr ? 0:L.gf.n);
                PauseMapMarker *markers=malloc(capacity*sizeof *markers);int n=0;
                if(markers){
                    markers[n++]=(PauseMapMarker){pl->x,pl->z,(int)lroundf(pl->yaw*(65536.f/6.283185307f))-32768,0,.25f,.3f};
                    for(int i=0;i<L.nrv;i++){
                        const struct Rival *r=L.rv+i;if(!r->active || r->table<0 || r->tagged)continue;
                        int icon=pause_map_rival_icon(r->chr);
                        markers[n++]=(PauseMapMarker){r->pl.x,r->pl.z,(int)lroundf(r->pl.yaw*(65536.f/6.283185307f))-32768,icon,.25f,.145f};
                    }
                    for(int i=0;i<L.ngangs;i++){
                        const Gang *g=L.gangs+i;if(!g->active)continue;
                        int group=g->kind==17 || g->mode==4;
                        for(int j=0;j<(group ? 1:g->nmem);j++){
                            int icon=pause_map_enemy_icon(g->kind,group ? g->flags:g->m[j].flags,g->m[j].anim);
                            if(icon<0)continue;
                            const float *position=group ? g->pos:g->m[j].pos;
                            markers[n++]=(PauseMapMarker){position[0],position[2],0,icon,.25f,.4f};
                        }
                    }
                    if(!L.gferr)for(int i=0;i<L.gf.n;i++){
                        const Spot *s=L.gf.spots+i;
                        if(!s->source || !pause_map_tag_visible(&L.pause_map,s->area,s->active && s->required,s->tagged || s->complete))continue;
                        markers[n++]=(PauseMapMarker){s->source->origin[0],s->source->origin[2],0,2,L.pause_map.kind==7 || L.pause_map.kind==8 ? .48f:.32f,0};
                    }
                }
                gfx_mode3d();pause_map_draw(&L.pause_map_models,&L.pause_map,markers,n,player_fsca_sin,proj,view);gfx_mode2d();free(markers);
            }
            gfx_mode3d();
            if(L.pause.practice)practice_hud_draw(&L.practice_hud,&L.pause,proj,view);
            else pause_hud_draw(&pause_models,&L.pause,L.id-1,L.mrec && L.mrec->mid!=0,proj,view,L.pause.phase>=2);
            gfx_mode2d();
            /* Original text has its own reciprocal depth (-.4). The
               PSP's 2D font pass composites it over the menu panel. */
            if(L.pause.phase>=2){
                if(L.pause.practice)list2d_draw_hud(&L.practice_hud.lists,0,&L.practice_hud.textures,0,0,HUD_CENTRE);
                else pause_map_text(&L.pause_map);
            }
            /* Pause hides cans/alerts, not the source health/score/time.
               Their 2D port draws must follow the opaque menu backdrop. */
            unsigned paused_flags=(L.mrec ? L.mrec->flags:0)|HUD_HIDE_CANS;
            hud_draw_game(&L.health_hud,&L.can_hud.counter,paused_flags|HUD_HIDE_TIME,L.score,0,player_fsca_sin);
            hud_draw_timer(&L.timer_hud,paused_flags);
            if(L.pause.phase>=3)pause_wipe_draw(&L.pause_wipe);
        }
        if(scene_frozen)sceneplay_overlay(&L.scene);
        if(pl->water.alpha>0.f)gfx_quad(0,0,480,272,GCOLOR(0,0,0,(int)(pl->water.alpha*255.f)));
        if(L.final.loaded && L.final.fall.visible)
            gfx_quad(0,0,480,272,GCOLOR(0,0,0,(int)(L.final.fall.alpha*255.f)));
        if(render_bench.active) {
            font_draw_center(240,258,1,GCOLOR(255,255,255,255),"RENDER BENCHMARK - START TO CANCEL");
            gfx_end_vsync(0);gfx_wait_idle();
            unsigned elapsed=sceKernelGetSystemTimeLow()-render_started;
            GfxProfile after;unsigned writes[3];gfx_profile_totals(&after);ge_writeback_totals(writes);
            unsigned hash=render_bench_last(&render_bench) ? render_bench_pixels():0;
            if(render_bench_capture && render_bench_last(&render_bench)){char name[64];snprintf(name,sizeof name,"shot_render_bench_%u_%u.raw",render_bench.target,render_bench.phase);dump_shot(name);}
            /* Preserve actual PSP pixels outside measured timing. Automatic
               emulator fixtures retain their established shot names above. */
            if(render_bench.detail>=RENDER_BENCH_PED_NATIVE && render_bench_last(&render_bench)) {
                char name[80];snprintf(name,sizeof name,"jgr_bench_r%u_f%u_p%u.raw",render_bench.run,render_bench.detail,render_bench.phase);
                runtime_log("RENDER_BENCH_CAPTURE_BEGIN run=%u feature=%s phase=%u path=%s hash=%08x stack_free=%d chunk_bytes=%u\n",
                    render_bench.run,render_bench_feature(render_bench.detail),render_bench.phase,name,hash,sceKernelCheckThreadStack(),JGR_CAPTURE_CHUNK_BYTES);
                runtime_log_flush();
                int saved=render_bench_dump(name);
                runtime_log("RENDER_BENCH_CAPTURE run=%u feature=%s phase=%u path=%s bytes=%d format=rgba8888 stride=512 width=480 height=272 buffers=1\n",
                    render_bench.run,render_bench_feature(render_bench.detail),render_bench.phase,name,saved);
                runtime_log_flush();
            }
            benchmark_profile_frames++;
            if(render_bench.frame>=RENDER_BENCH_WARMUP) {
                CharPlanProfile plans;StageProfile world;chars_plan_totals(&plans);stage_profile_totals(&world);
                RenderBenchResult *r=render_bench.result+render_bench.phase;
                r->actor_timing_frames+=chars_actor_timing_enabled();
                r->ped_math_frames+=chars_ped_math_enabled();
                r->group_vertices+=plans.group_vertices-plan_before.group_vertices;
                r->classified_vertices+=world.classified_vertices-clip_before.classified_vertices;
                r->tested_groups+=world.tested_groups-clip_before.tested_groups;
                r->tested_triangles+=world.tested_triangles-clip_before.tested_triangles;
                r->stage_draws+=world.draw_calls-clip_before.draw_calls;
                r->joined_draws+=world.joined_draws-clip_before.joined_draws;
                r->joined_bytes+=world.joined_index_bytes-clip_before.joined_index_bytes;
                r->hidden_objects+=world.occlusion_rejected-clip_before.occlusion_rejected;
                r->index_cache_hits+=world.index_cache_hits-clip_before.index_cache_hits;
                CharArrowProfile arrows;chars_arrow_totals(&arrows);render_bench_arrows(r,&arrow_before,&arrows);
                CharLodProfile detail;chars_lod_totals(&detail);render_bench_detail(r,&detail_before,&detail);
                if(render_bench.detail>=RENDER_BENCH_POSE_SPAN) {
                    CharWorkStats actors[CHAR_WORK_COUNT];chars_work_totals(actors);render_bench_actors(r,actor_before,actors);
                }
                if(render_bench.detail==RENDER_BENCH_PED_PACKETS) {
                    CharPedPacketProfile packets;chars_ped_packet_totals(&packets);render_bench_ped_packets(r,&ped_packet_before,&packets);
                }
                if(render_bench.detail>=RENDER_BENCH_PED_NATIVE) {
                    CharPedSharedProfile shared;chars_ped_shared_totals(&shared);render_bench_ped_shared(r,&ped_shared_before,&shared);
                }
                if(render_bench.detail>=RENDER_BENCH_PED_OCCLUSION) {
                    NpcOcclusionProfile occlusion;npcs_occlusion_totals(&occlusion);
                    render_bench_ped_occlusion_counts(r,&ped_occlusion_before,&occlusion);
                }
            }
            render_bench_sample(&render_bench,&render_before,&after,write_before,writes,elapsed,hash);
            SceCtrlData physical;sceCtrlPeekBufferPositive(&physical,1);
            int cancelled=!running || (physical.Buttons&PSP_CTRL_START);
            if(render_bench.active && !cancelled)goto render_bench_redraw;
            render_bench.active=0;chars_draw_plan_mode(1);stage_clip_mode(1);chars_arrow_mode(1);freeze=render_saved_freeze;dt=render_saved_dt;cpu_avg=render_saved_cpu;
            chars_lod_mode(npc_detail);
            stage_set_scenery_mode(1);
            stage_set_plane_occlusion(1);
            chars_pose_span_mode(1);
            chars_ped_packet_mode(1);
            chars_ped_shared_mode(1);chars_ped_transform_mode(0);chars_ped_emit_mode(1);
            chars_actor_timing_mode(0);chars_ped_math_mode(1);npcs_occlusion_mode(NPC_OCCLUSION_PROXIES);
            prev=physical.Buttons;pad=physical;pressed=0;
            int valid=!cancelled && render_bench_valid(&render_bench,pl->logic_tick);
            if(render_bench.detail>=RENDER_BENCH_PED_OCCLUSION)
                runtime_log("PED_OCCLUSION_SESSION enabled=1 native=1 emitter=1 clocks=0 scopes=1 repeatability_and_work_valid=%d cancelled=%d policy=default aggressive=0 proxies=1\n",valid,cancelled);
            if(render_bench.detail==RENDER_BENCH_PED_OVERHEAD)
                runtime_log("PED_OVERHEAD_SESSION native=1 emitter=1 clocks=0 scopes=1 image_and_work_valid=%d cancelled=%d policy=default\n",valid,cancelled);
            if(render_bench.detail==RENDER_BENCH_PED_EMIT)
                runtime_log("PED_EMIT_SESSION enabled=1 native=1 image_and_work_valid=%d cancelled=%d policy=default\n",valid,cancelled);
            if(!cancelled && render_bench.detail==RENDER_BENCH_PED_NATIVE) {
                unsigned exercised=render_bench.result[1].ped_shared.actors && render_bench.result[2].ped_shared.actors;
                runtime_log("PED_NATIVE_SESSION enabled=1 image_valid=%d exercised=%u policy=default\n",
                    valid,exercised);
            }
            for(unsigned phase=0;phase<4;phase++) {
                const RenderBenchResult *r=render_bench.result+phase;float scale=r->samples ? .001f/r->samples:0;
                if(render_bench.detail>=RENDER_BENCH_PED_OCCLUSION) {
                    const NpcOcclusionProfile *o=&r->ped_occlusion;
                    runtime_log("RENDER_BENCH_PED_OCCLUSION run=%u phase=%u enabled=%d clocks=%d scopes=%d candidates=%u tested=%u hidden=%u kept=%u prepare_skipped=%u draw_skipped=%u query_ms=%.4f occluders=%u late_tested=%u late_hidden=%u aggressive=%d rays=%u ray_blocked=%u ray_uncertain=%u ray_unavailable=%u ray_triangles=%u ray_steps=%u proxies=%d proxy_tests=%u proxy_blocked=%u\n",
                        render_bench.run,phase,r->samples && o->enabled_frames==r->samples ? 1:o->enabled_frames ? -1:0,
                        r->samples && r->actor_timing_frames==r->samples ? 1:r->actor_timing_frames ? -1:0,
                        r->samples && r->ped_math_frames==r->samples ? 1:r->ped_math_frames ? -1:0,
                        o->candidates,o->tested,o->hidden,o->kept,o->prepare_skipped,o->draw_skipped,o->query_us*scale,o->occluders,o->late_tested,o->late_hidden,
                        r->samples && o->aggressive_frames==r->samples ? 1:o->aggressive_frames ? -1:0,
                        o->rays,o->ray_blocked,o->ray_uncertain,o->ray_unavailable,o->ray_triangles,o->ray_steps,
                        r->samples && o->proxy_frames==r->samples ? 1:o->proxy_frames ? -1:0,
                        o->proxy_tests,o->proxy_blocked);
                }
                if(render_bench.detail==RENDER_BENCH_PED_OVERHEAD)
                    runtime_log("RENDER_BENCH_PED_OVERHEAD run=%u phase=%u clocks=%d scopes=%d\n",
                        render_bench.run,phase,r->samples && r->actor_timing_frames==r->samples ? 1:r->actor_timing_frames ? -1:0,
                        r->samples && r->ped_math_frames==r->samples ? 1:r->ped_math_frames ? -1:0);
                runtime_log("RENDER_BENCH run=%u phase=%u batched=%d samples=%u submit_ms=%.4f p95_ms=%.4f ge_tail_ms=%.4f render_fence_ms=%.4f publications=%.3f writeback_ms=%.4f city_ms=%.4f actors_ms=%.4f effects_ms=%.4f hud_ms=%.4f tags_ms=%.4f hash=%08x group_vertices=%u clip_vertices=%u group_tests=%u tri_tests=%u\n",
                    render_bench.run,phase,phase==1 || phase==2,r->samples,r->submit_us*scale,render_bench_p95(r)*.001f,r->ge_us*scale,r->wall_us*scale,
                    r->samples ? (float)r->publications/r->samples:0,r->writeback_us*scale,
                    r->sections[GFX_STAGE]*scale,r->sections[GFX_ACTORS]*scale,r->sections[GFX_EFFECTS]*scale,r->sections[GFX_HUD]*scale,r->sections[GFX_TAGS]*scale,r->hash,
                    r->group_vertices,r->classified_vertices,r->tested_groups,r->tested_triangles);
                runtime_log("RENDER_BENCH_ARROWS run=%u phase=%u arrow_ms=%.4f requests=%u drawn=%u culled=%u reference=%u pose_built=%u pose_reused=%u draws=%u scopes=%u\n",
                    render_bench.run,phase,r->arrows.total_us*scale,r->arrows.requests,r->arrows.drawn,r->arrows.culled,
                    r->arrows.reference,r->arrows.pose_built,r->arrows.pose_reused,r->arrows.draw_calls,r->arrows.scopes);
                if(render_bench.detail==2 || render_bench.detail==3)runtime_log("RENDER_BENCH_SCENERY run=%u phase=%u draws=%u joined_draws=%u index_bytes=%u hidden_objects=%u cache_hits=%u\n",
                    render_bench.run,phase,r->stage_draws,r->joined_draws,r->joined_bytes,r->hidden_objects,r->index_cache_hits);
                if(render_bench.detail==1)runtime_log("RENDER_BENCH_DETAIL run=%u phase=%u actors=%u texture_actors=%u hull_actors=%u omitted_triangles=%u texture_binds=%u source_texels=%u lod_texels=%u fallbacks=%u\n",
                    render_bench.run,phase,r->detail.actors,r->detail.texture_actors,r->detail.hull_actors,r->detail.hull_triangles,
                    r->detail.texture_binds,r->detail.source_texels,r->detail.lod_texels,r->detail.texture_fallbacks);
                if(render_bench.detail==RENDER_BENCH_PED_PACKETS) {
                    const CharPedPacketProfile *p=&r->ped_packets;
                    runtime_log("RENDER_BENCH_PED_PACKETS run=%u phase=%u draws=%u strip_draws=%u source_vertices=%u vertices=%u source_indices=%u indices=%u triangles=%u built=%u reused=%u fallbacks=%u\n",
                        render_bench.run,phase,p->draws,p->strip_draws,p->source_vertices,p->vertices,p->source_indices,p->indices,p->triangles,p->built,p->reused,p->fallbacks);
                }
                if(render_bench.detail>=RENDER_BENCH_PED_NATIVE) {
                    const CharPedSharedProfile *p=&r->ped_shared;
                    runtime_log("RENDER_BENCH_PED_NATIVE run=%u phase=%u actors=%u fallback_actors=%u source_vertices=%u unique_positions=%u draws=%u triangles=%u indices=%u vertex_bytes=%u built=%u reused=%u transform_ms=%.4f gather_ms=%.4f submit_ms=%.4f\n",
                        render_bench.run,phase,p->actors,p->fallback_actors,p->source_vertices,p->unique_positions,p->draws,p->triangles,p->indices,p->vertex_bytes,p->built,p->reused,
                        p->transform_us*scale,p->gather_us*scale,p->submit_us*scale);
                }
                if(render_bench.detail>=RENDER_BENCH_POSE_SPAN) {
                    static const char *const names[CHAR_WORK_COUNT]={"PLAYER","PEDESTRIANS","TRAFFIC","OTHER"};
                    for(unsigned i=0;i<CHAR_WORK_COUNT;i++) {
                        const CharWorkStats *w=r->actors+i;
                        runtime_log("RENDER_BENCH_ACTOR run=%u phase=%u class=%s calls=%u bones=%u bone_uploads=%u skin_draws=%u skin_vertices=%u culled=%u total_ms=%.4f pose_ms=%.4f bounds_ms=%.4f packet_ms=%.4f skin_ms=%.4f cpu_skin_ms=%.4f\n",
                            render_bench.run,phase,names[i],w->calls,w->bones,w->bone_uploads,w->skin_draws,w->skin_vertices,w->culled,
                            w->total_us*scale,w->pose_us*scale,w->bounds_us*scale,w->packet_us*scale,w->skin_us*scale,w->cpu_skin_us*scale);
                    }
                }
            }
            runtime_log("RENDER_BENCH_END run=%u valid=%d cancelled=%d tick_before=%u tick_after=%u note=render_only_not_game_fps\n",render_bench.run,valid,cancelled,render_bench.tick,pl->logic_tick);
            /* If the new native backend differs on physical hardware, keep
               this exact frozen pose and separately isolate scalar and VFPU
               transforms using the original GE draw topology. */
            if(!cancelled && render_bench_diagnostics &&
               ((render_bench.detail==RENDER_BENCH_PED_NATIVE && (!valid || render_bench_force_diagnostics)) ||
                render_bench.detail==RENDER_BENCH_PED_TRANSFORM_SCALAR)) {
                unsigned next=render_bench.detail==RENDER_BENCH_PED_NATIVE ?
                    RENDER_BENCH_PED_TRANSFORM_SCALAR:RENDER_BENCH_PED_TRANSFORM_VFPU;
                runtime_log_flush();render_bench_begin(&render_bench,pl->logic_tick);
                render_bench.detail=next;render_bench.target=next-RENDER_BENCH_PED_NATIVE;
                runtime_log("RENDER_BENCH_BEGIN run=%u feature=%s stage=%d zone=%d tick=%u player=%.5f,%.5f,%.5f eye=%.5f,%.5f,%.5f warmup=%u samples=%u order=ABBA me=%d cpu_mhz=%d bus_mhz=%d\n",
                    render_bench.run,render_bench_feature(next),L.id,L.cur_zone,pl->logic_tick,pl->x,pl->y,pl->z,eye[0],eye[1],eye[2],
                    RENDER_BENCH_WARMUP,RENDER_BENCH_SAMPLES,audio_me_status(),scePowerGetCpuClockFrequencyInt(),scePowerGetBusClockFrequencyInt());
                goto render_bench_redraw;
            }
            runtime_log_flush();runtime_log_deferred=render_log_deferred;
            t_prev=sceKernelGetSystemTimeLow();render_bench_completed=1;
            if(logfd>=0)logframes++; /* one normal outer frame, not 160 simulation frames */
            goto render_bench_presented;
        }
        if (logfd >= 0 && logframes < 5) { char m[160]; int n = snprintf(m, sizeof m, "frame %d state %d pos=%.1f,%.1f,%.1f scene_load=%d heap_free=%d\n", logframes, state, L.pl.x, L.pl.y, L.pl.z,last_scene_begin,mallinfo().fordblks); runtime_log_write(logfd, m, n); }
        if(logfd>=0 && L.final.loaded) {
            static int last_fall=-1;
            if(last_fall!=L.final.fall.phase) {
                char line[180];int n=snprintf(line,sizeof line,"FINAL fall_phase=%d alpha=%.9g pos=%.9g,%.9g,%.9g world_error=%d\n",
                    L.final.fall.phase,L.final.fall.alpha,pl->x,pl->y,pl->z,L.final.error);
                runtime_log_write(logfd,line,n);last_fall=L.final.fall.phase;
            }
        }
        { static char lastev[40]; static int lastph = -1; if (logfd >= 0 && L.ms.active && (strcmp(lastev, L.ms.log[0]) || lastph != L.ms.phase)) { char lb[200]; int n = snprintf(lb, sizeof lb, "f=%d EVENT %s | phase %d | tagged %d/%d time %d sub=%s\n", logframes, L.ms.log[0], L.ms.phase, L.ms.ntagged, L.ms.nreq, L.ms.time_left, L.ms.nsub ? L.ms.sub[0] : ""); runtime_log_write(logfd, lb, n); snprintf(lastev, 40, "%s", L.ms.log[0]); lastph = L.ms.phase; } }
        if (logfd >= 0 && ++logframes % 60 == 0 && runtime_verbose) {
            if(traffic_test) {
                unsigned a[5];audio_world_info(a);runtime_log("WORLD_AUDIO starts=%u steals=%u errors=%u owners=%u live=%u\n",a[0],a[1],a[2],a[3],a[4]);
                PathWorld *w=L.traffic ? &L.traffic->world:NULL;unsigned surfaces=0,bodies=0;
                if(w)for(unsigned i=0;i<64;i++){surfaces+=w->slots[i].active && w->slots[i].surface;bodies+=w->slots[i].active && w->slots[i].body_valid && w->slots[i].steps;}
                char line[180];int n=snprintf(line,sizeof line,"TRAFFIC tick=%u count=%u surfaces=%u bodies=%u triangles=%d error=%d heap=%d\n",
                    w ? w->clock:0,w ? w->count:0,surfaces,bodies,L.traffic_models.drawn_tris,w ? w->error:-99,mallinfo().fordblks);
                runtime_log_write(logfd,line,n);
            }
            for(unsigned i=0;i<64;i++)if(L.group_parent[i].active) {
                const GroupParent *p=&L.group_parent[i];int gangs=0,members=0;char line[160];
                for(int j=0;j<L.ngangs;j++)gangs+=L.gangs[j].active && L.gangs[j].parent_handle==p->handle;
                for(int j=0;j<L.npcs.count;j++)members+=L.npcs.p[j].active && L.npcs.p[j].group_owner==p->handle;
                int n=snprintf(line,sizeof line,"GROUP parent=%08x record=%d state=%d flags=%x gangs=%d members=%d unresolved=%d\n",
                    p->handle,p->record,p->state,p->flags,gangs,members,p->unresolved_children);
                runtime_log_write(logfd,line,n);
            }
            if(L.final.loaded) {
                FinalBoss *b=&L.final.boss;char line[180];
                int n=snprintf(line,sizeof line,"FINAL_ACTOR motion=%d frame=%.0f blend=%.9g orbit=%u sides=%u,%u texture=%u bank=%u\n",
                    b->motion,b->frame,b->blend,b->orbit,b->left,b->right,b->texture_rotation,L.final.actors.h->n_chars);
                runtime_log_write(logfd,line,n);
            }
            if(sceneplay_resident(&L.scene)) {
                char sb[240];int sn=snprintf(sb,sizeof sb,"SCENE id=%d phase=%d clock=%.0f vm_error=%d audio_loaded=%d voice_serial=%u voice=%d audio_error=%d transferred=%d\n",
                    L.scene.vm.id,L.scene.vm.phase,L.scene.vm.clock,L.scene.vm.error,L.scene.audio_loaded,L.scene.voice_serial,L.scene.voice_ref,L.scene.audio_error,L.scene.transferred);
                runtime_log_write(logfd,sb,sn);
                if(L.scene.recruit_audio_loaded) {
                    unsigned position;int active;audio_recruit_voice_info(&position,&active);
                    sn=snprintf(sb,sizeof sb,"RECRUIT_AUDIO clock=%.0f position=%u active=%d serial=%u mouth=%d font=%d error=%d\n",
                        L.scene.vm.clock,position,active,L.scene.recruit_voice_serial,L.scene.mouth_loaded,font_real_wide_ready(),L.scene.audio_error);
                    runtime_log_write(logfd,sb,sn);
                }
                if(L.scene.object_audio_loaded) {
                    unsigned samples,position,hash;int active;
                    audio_final_voice_info(&samples,&position,&hash,&active);
                    sn=snprintf(sb,sizeof sb,"FINAL_AUDIO clock=%.0f samples=%u position=%u active=%d hash=%08x serial=%u\n",
                        L.scene.vm.clock,samples,position,active,hash,L.scene.object_voice_serial);
                    runtime_log_write(logfd,sb,sn);
                }
                if(L.scene.object_bank_loaded) {
                    unsigned position;int active;audio_final_object_info(1,&position,&active);
                    sn=snprintf(sb,sizeof sb,"FINAL_OBJECT_AUDIO clock=%.0f slot=1 samples=%u active=%d serial=%u error=%d\n",
                        L.scene.vm.clock,position,active,L.scene.object_serial[1],L.scene.audio_error);
                    runtime_log_write(logfd,sb,sn);
                    unsigned notes,errors,voices;audio_final_sequence_info(&notes,&errors,&voices);
                    sn=snprintf(sb,sizeof sb,"FINAL_SEQUENCE_AUDIO clock=%.0f notes=%u errors=%u voices=%u\n",L.scene.vm.clock,notes,errors,voices);
                    runtime_log_write(logfd,sb,sn);
                }
            }
            { char cb[160]; int cn = snprintf(cb, sizeof cb,
                "CAM eye=%.2f,%.2f,%.2f yaw=%.5f pitch=%.5f playerYaw=%.5f nub=%u,%u\n",
                eye[0], eye[1], eye[2], L.cam_yaw, L.cam_pitch, pl->yaw,pad.Lx,pad.Ly);
              runtime_log_write(logfd, cb, cn); }
            char lb[640]; int n = snprintf(lb, sizeof lb, "f=%d lvl=%d zone=%d npc=%d zl=%d/%d ms=%.1f cpu=%.1f pos=%.1f,%.1f,%.1f spd=%.1f gnd=%d gr=%d mot=%d objs=%d tris=%d ctri=%d adx=%u spots=%d score=%d vy=%.1f mis=%d req=%d mtag=%d t=%d ph=%d ev0=%s peds=%d/%d play=%d/%d trial=%d:%d:%d st=%d set=%d nx=%d rv=%d sobj=%d ctri=%d sobst=%d/%d r0=%.0f,%.0f,%.0f/%d/%d/%d\n", logframes, L.id, L.cur_zone, npcs_active(&L.npcs), ({ int a_ = 0, b_ = 0; for (int z_ = 0; z_ < 256; z_++) { a_ += L.zone_list[z_] >= 0; b_ += L.zone_spawned[z_]; } a_; }), ({ int b_ = 0; for (int z_ = 0; z_ < 256; z_++) b_ += L.zone_spawned[z_]; b_; }), ms_avg, cpu_avg, pl->x, pl->y, pl->z, pl->speed, pl->onground, pl->grinding, pl->motion, L.st.drawn_obj, L.st.drawn_tris, cherr ? -1 : chars.drawn_tris, stream_decoded_frames(), L.gferr ? -1 : L.gf.n, L.score, pl->vy, L.ms.active, L.ms.nreq, L.ms.ntagged, L.ms.time_left, L.ms.phase, L.ms.log[0], npcs_active(&L.npcs), L.npcs.count, L.play.active ? L.play.pos : -1, L.play.s ? L.play.s->nrec : 0, L.trial.index, (int)pl->ground_ticks, L.ctl.sub[L.set_idx].ntrial, pl->state, L.set_idx, L.ms.next_rec, L.nrv, L.nsobj, L.col.ntri, L.sobst_ok, L.nsobrec, L.nrv ? L.rv[0].pl.x : 0.f, L.nrv ? L.rv[0].pl.y : 0.f, L.nrv ? L.rv[0].pl.z : 0.f, L.nrv ? L.rv[0].seg : -1, L.nrv ? L.rv[0].started : -1, L.nrv ? L.rv[0].play.pos : -1);
            runtime_log_write(logfd, lb, n);
        }
        if(runtime_verbose && logfd>=0 && !(frames%60) && (L.mrec || L.campaign_error)) {
            if(player_audio_selection>=0){
                unsigned sounds[5],errors,active;audio_player_info(sounds,&errors,&active);
                runtime_log("PLAYER_AUDIO frame=%d voices=%u effects=%u grinds=%u sprays=%u active=%u errors=%u\n",
                    frames,sounds[1],sounds[2],sounds[3],sounds[4],active,errors);
                AudioPlayerCacheInfo cache;audio_player_cache_info(&cache);
                runtime_log("PLAYER_AUDIO_CACHE frame=%d bytes=%u peak=%u loads=%u evictions=%u read_us=%u max_read_us=%u resident_bytes=%u resident_samples=%u cached=%u\n",
                    frames,cache.bytes,cache.peak_bytes,cache.loads,cache.evictions,cache.read_us,cache.max_read_us,
                    cache.resident_bytes,cache.resident_samples,cache.cached);
            }
            const CtrlSub *cs=ctrl_sub(&L.ctl,L.set_idx);
            if(L.street_props){unsigned info[5];street_props_info(L.street_props,info);
                runtime_log("STREET_PROPS frame=%d resident=%u nearby=%u hit=%u contacts=%u ticks=%u effects=1\n",
                    frames,info[0],info[1],info[2],info[3],info[4]);
                unsigned effects[4];street_props_effect_info(L.street_props,effects);
                runtime_log("STREET_PROP_EFFECTS frame=%d flying=%u retired=%u draws=%u probes=%u\n",
                    frames,effects[0],effects[1],effects[2],effects[3]);}
            if(L.grind_props){unsigned info[6];grind_props_info(L.grind_props,info);
                runtime_log("GRIND_PROPS frame=%d ticks=%u draws=%u lift0=%u/%u lift1=%u/%u\n",frames,info[0],info[1],info[2],info[3],info[4],info[5]);}
            if(L.crane){unsigned info[5];benten_crane_info(L.crane,info);
                runtime_log("BENTEN_CRANE frame=%d ticks=%u active=%u motion=%u surfaces=%u draws=%u\n",frames,info[0],info[1],info[2],info[3],info[4]);}
            if(L.traffic)runtime_log("CAR_PULL frame=%d tick=%u cars=%u held=%d handle=%u pos=%.6f,%.6f,%.6f yaw=%d speed=%.6f world_error=%d system_free=%u\n",
                frames,pl->logic_tick,L.traffic->world.count,pl->car_pull.active,pl->car_pull.contact.handle,
                pl->x,pl->y,pl->z,player_bam_atan2(-sinf(pl->yaw),-cosf(pl->yaw)),pl->speed,L.traffic->world.error,(unsigned)sceKernelTotalFreeMemSize());
            char line[256];int n=snprintf(line,sizeof line,"CAMPAIGN frame=%d name=%.15s record=%ld set=%d kind=%d next=%d/%d retry=%d/%d trial=%d streams=%d error=%d heap=%u character=%d\n",
                frames,L.mrec?L.mrec->name:"?",L.mrec?(long)(L.mrec-missions)-L.mrec->first:-1,
                L.set_idx,L.ms.kind,L.ms.next_rec,L.ms.next_set,L.ms.retry_rec,L.ms.retry_set,
                cs?cs->ntrial:-1,cs?cs->nstream:-1,L.campaign_error,sceKernelTotalFreeMemSize(),CHAR_ID_OF_MODEL[pchar&15]);
            runtime_log_write(logfd,line,n);
        }
        if(autopilot && L.final.loaded && logfd>=0 && !(frames%60)) {
            char line[96];int n=snprintf(line,sizeof line,"FINAL_RENDER frame=%d list_bytes=%d capacity=2097152\n",frames,sceGuCheckList());
            runtime_log_write(logfd,line,n);
        }
        if(L.ms.phase==MISSION_PHASE_EXIT && L.exit.active) {
            gfx_mode2d();gfx_quad(0,0,SCR_W,SCR_H,GCOLOR(0,0,0,(int)(L.exit.alpha*255.f)));
        }
        gfx_end_vsync(vsync);
        if(L.ms.phase==MISSION_PHASE_EXIT && L.exit.alpha>=1.f && !L.exit.black_presented) {
            /* Submission defers the swap. Present black before next
               frame's audio/world teardown, including ordinary builds. */
            gfx_wait_idle();
            if(exit_fixture.enabled)dump_shot("shot_exit_black.raw");
            level_exit_presented(&L.exit);
        }
        spike_save_submit(&L,frames,spike_eligible,state,eye,fwd,view,proj,clip,yaw,pitch,pad.Buttons,cullmode,wire,vsync);
render_bench_presented:;
        if(pickuproadblocktest && !scene_frozen) {
            const int ticks[8]={30,75,150,195,300,350,420,470};
            for(int i=0;i<8;i++)if(pickuproadblocktick>=ticks[i] && !(pickuproadblockshots&(1u<<i))) {
                char name[64];snprintf(name,sizeof name,"shot_pickup_roadblock_%d.raw",i);dump_shot(name);pickuproadblockshots|=1u<<i;
                unsigned taken[2]={0};for(int k=0;k<L.gf.ncans;k++){const Can *c=L.gf.cans+k;if(c->set==(unsigned)L.set_idx && c->kind>=2 && c->taken)taken[c->kind-2]++;}
                runtime_log("PICKUP_ROADBLOCK shot=%d tick=%d health=%d max=%d cans=%d small=%u large=%u writable=%d\n",i,pickuproadblocktick,pl->health,pl->health_max,L.gf.cancount,taken[0],taken[1],campaign_writable);
                for(int k=0;k<L.npcs.count;k++){const Ped *p=L.npcs.p+k;if(p->active && (p->type==26 || p->type==27) && p->pose)runtime_log("PARKED_CAR shot=%d index=%d pose=%d xyz=%.9g,%.9g,%.9g frame=%.3f visible=%d\n",i,k,p->pose,p->x,p->y,p->z,p->frame,p->visible);}
                if(pickuproadblocktest==2)runtime_log("RIOT_ROADBLOCK shot=%d gases=%u audio_errors=%u last_audio=%d\n",i,L.combat.gases.created,L.combat.audio_errors,L.combat.last_audio_error);
                runtime_log_flush();
            }
        }
        if(combattest) {
            const int ticks[4]={64,76,100,180};
            for(int k=0;k<4;k++)if(combat_test_tick>=ticks[k] && !(combat_test_captures&(1u<<k))) {
                char path[48];snprintf(path,sizeof path,"shot_combat_%d.raw",ticks[k]);dump_shot(path);combat_test_captures|=1u<<k;
            }
        }
        if(autopilot && L.traffic && frames>0 && !(frames%600)) {
            char shot[64];snprintf(shot,sizeof shot,"shot_car_pull_%d.raw",frames);dump_shot(shot);
        }
        if(routetest==18 && state==1 && !scene_frozen && pl->logic_tick>=60 && route_action==5) {
            dump_shot("shot_menu_gameplay.raw");
            runtime_log("ROUTE_DONE menu_roundtrip stage=%d error=%d collision=%d characters=%d radio=%d gallery=%d\n",L.id,L.campaign_error,L.hferr,cherr,radio_ok,mdisp_ok);
            runtime_log_flush();route_action=6;
        }
        if(endingtest>=25 && endingtest<=27 && title_mode && title_frames==60) {
            dump_shot("shot_ending_title.raw");
            runtime_log("ENDING_TITLE_RENDERED stage=%d error=%d collision=%d characters=%d logo=%d demo=%d\n",L.id,L.campaign_error,L.hferr,cherr,tlogo_ok,L.demo_on);
            runtime_log_flush();
        }
        if(movie_route_test==2 && state==1) {
            static unsigned lesson_frames;
            if(++lesson_frames==60) {
                dump_shot("shot_movie_lesson.raw");
                runtime_log("MOVIE_LESSON_RENDERED stage=%d error=%d collision=%d characters=%d scene=%d\n",L.id,L.campaign_error,L.hferr,cherr,last_scene_begin);
                runtime_log_flush();
            }
        }
        if(movie_route_test==4 && state==1 && !L.ms.phase && !sceneplay_resident(&L.scene)) {
            static unsigned mission_frames;
            if(++mission_frames==60) {
                dump_shot("shot_movie_mission.raw");
                runtime_log("MOVIE_MISSION_RENDERED stage=%d error=%d collision=%d characters=%d world_audio=%d player_audio=%d writable=%d\n",
                    L.id,L.campaign_error,L.hferr,cherr,audio_world_blocked(),player_audio_selection,campaign_writable);
                runtime_log_flush();
            }
        }

        if(passagefixture.started) {
            int shot=passagefixture.done ? 2:passagefixture.warps ? 1:0;
            if(!(passagefixture.shots&(1<<shot))) {
                passagefixture.shots|=1<<shot;char name[40];snprintf(name,sizeof name,"shot_passage_%d.raw",shot);dump_shot(name);
                runtime_log("PASSAGE_INPUT shot=%d tick=%u xyz=%.9g,%.9g,%.9g warps=%d minimum_y=%.9g health=%d/%d state=%d character=%d writable=%d\n",shot,pl->logic_tick,pl->x,pl->y,pl->z,passagefixture.warps,passagefixture.minimum_y,pl->health,passagefixture.health,pl->state,pl->character_id,campaign_writable);
                runtime_log_flush();
            }
        }
        if(carlessontest && L.play.active && L.set_idx==2 && !scene_frozen) {
            const unsigned clocks[5]={230,245,280,360,665};
            for(int i=0;i<5;i++)if(!(carlesson_shots&(1<<i)) && pl->logic_tick>=clocks[i]) {
                carlesson_shots|=1<<i;char name[48];snprintf(name,sizeof name,"shot_spray_car_%d.raw",i);dump_shot(name);
                runtime_log("CAR_LESSON_SHOT index=%d clock=%u cans=%d tagged=%d bubble=%.6f mist=%d\n",i,pl->logic_tick,L.gf.cancount,L.gf.tagged,pl->effects.bubble.scale,pl->effects.spray.active);
            }
        }
        if(trickhudtest && trickhud_started && !scene_frozen && !L.pause.active){
            for(int i=0;i<15;i++){
                const TrickTask *t=&pl->trick_hud.task[i];
                if((t->type!=1 && t->type!=3) || t->deleted || t->phase!=1)continue;
                int slot=t->type==3 ? 7:t->kind;if(trickhud_shots&(1u<<slot))continue;
                trickhud_shots|=1u<<slot;char name[48];snprintf(name,sizeof name,"shot_trick_%d.raw",slot);dump_shot(name);
                runtime_log("TRICK_HUD shot=%d x=%.9g y=%.9g score=%d wide=%d\n",slot,t->x,t->y,L.score,font_real_wide_ready());
            }
        }
        if(messagehudtest && messagehud_started && !scene_frozen){
            int slots[6]={L.ms.message.phase==4 && L.ms.message.alert>=10,
                L.ms.message.phase==6 && L.ms.message.x<196,L.ms.message.phase==7,L.ms.message.phase==1,
                L.notice.visible && L.notice.draw_clock>=0 && L.notice.draw_clock<10,L.can_hud.counter.displayed>=10};
            for(int slot=0;slot<6;slot++)if(slots[slot] && !(messagehud_shots&(1u<<slot))){
                messagehud_shots|=1u<<slot;char name[48];snprintf(name,sizeof name,"shot_message_%d.raw",slot);dump_shot(name);
                unsigned pos;int active,error;audio_message_info(&pos,&active,&error);
                runtime_log("HUD_MESSAGE shot=%d phase=%d x=%.0f alert=%d hold=%d cans=%d notice=%d audio=%u/%d/%d wide=%d heap=%d\n",
                    slot,L.ms.message.phase,L.ms.message.x,L.ms.message.alert,L.ms.message.hold,L.can_hud.counter.displayed,
                    L.notice.draw_clock,pos,active,error,font_real_wide_ready(),mallinfo().fordblks);
            }
        }
        if(autopilot && L.ms.active && !title_mode && !scene_frozen && L.timer_hud.countdown &&
           !(L.mrec->flags&HUD_HIDE_TIME) && L.timer_hud.previous>=0) {
            static int last_count=-1;int model=L.timer_hud.previous;
            if(last_count!=model && L.timer_hud.frame>=4) {
                last_count=model;char name[48],line[96];snprintf(name,sizeof name,"shot_countdown_%d.raw",model);dump_shot(name);
                int n=snprintf(line,sizeof line,"COUNTDOWN_HUD model=%d clock=%d ticks=%d triangles=%d\n",model,L.timer_hud.clock,L.timer_hud.ticks,countdown_models.drawn_tris);
                if(logfd>=0)runtime_log_write(logfd,line,n);
            }
        }
        if(pausehudtest && L.pause.active){
            if(L.pause.phase==3 && L.pause_wipe.phase){
                static unsigned wipes=0;int wi=L.pause_wipe.clock>=25 ? 2:L.pause_wipe.clock>=16 ? 1:L.pause_wipe.clock>=6 ? 0:-1;
                if(wi>=0 && !(wipes&(1u<<wi))){char name[48],line[96];wipes|=1u<<wi;
                    snprintf(name,sizeof name,"shot_pause_wipe_%d.raw",wi);dump_shot(name);
                    int n=snprintf(line,sizeof line,"PAUSE_WIPE shot=%d clock=%.9g phase=%d\n",wi,L.pause_wipe.clock,L.pause_wipe.phase);
                    if(logfd>=0)runtime_log_write(logfd,line,n);
                }
            }
            static unsigned shots=0;int shot=L.pause.clock>=60 ? 2:L.pause.clock>=40 ? 1:L.pause.clock>=30 ? 0:-1;
            int changed=pl->logic_tick!=pausehud_tick || L.ms.time_left!=pausehud_time || pl->health!=pausehud_health ||
                pl->x!=pausehud_pos[0] || pl->y!=pausehud_pos[1] || pl->z!=pausehud_pos[2];
            if(shot>=0 && !(shots&(1u<<shot))){char name[40],line[240];shots|=1u<<shot;snprintf(name,sizeof name,"shot_pause_%d.raw",shot);dump_shot(name);
                int n=snprintf(line,sizeof line,"PAUSE_HUD clock=%d phase=%d choice=%d volume=%.9g triangles=%d world_changed=%d tick=%u time=%d health=%d\n",
                    L.pause.clock,L.pause.phase,L.pause.selection,L.pause.volume,L.pause.practice ? L.practice_hud.models.drawn_tris:pause_models.drawn_tris,changed,pl->logic_tick,L.ms.time_left,pl->health);
                if(logfd>=0)runtime_log_write(logfd,line,n);
                runtime_log("PAUSE_MAP stage=%d region=%d layout=%d triangles=%d remaining=%d wide=%d heap=%d\n",
                    L.pause_map.stage,L.pause_map.region,L.pause_map.layout,L.pause_map_models.drawn_tris,L.pause_map.remaining,font_real_wide_ready(),mallinfo().fordblks);
                if(L.pause.practice)runtime_log("PRACTICE_HUD clock=%d selection=%d models=%u textures=%d lists=%u\n",L.pause.clock,L.pause.selection,L.practice_hud.models.h->n_chars,L.practice_hud.textures.n,L.practice_hud.lists.n);
            }
            if(changed)printf("PAUSE_HUD ERROR world advanced while paused\n");
        }else if(pausehudtest && pausehud_phase==4 && pl->logic_tick>pausehud_tick+2){
            char line[128];int n=snprintf(line,sizeof line,"PAUSE_HUD resumed tick=%u prior=%u jumping=%d phase=%d\n",pl->logic_tick,pausehud_tick,pl->state==3,L.ms.phase);
            if(logfd>=0)runtime_log_write(logfd,line,n);pausehud_phase=5;
        }
        if((resulthudtest || recruitresulttest) && L.result.active && result_hud_visible(&L.result)){
            static int record_shot=0;
            if(result_hud_record_visible(&L.result) && L.result.record_clock>=15 && !record_shot){
                record_shot=1;dump_shot("shot_result_record.raw");
                runtime_log("RESULT_RECORD phase=%d clock=%d place=%d\n",L.result.phase,L.result.record_clock,L.result.record_place);
            }
            static unsigned shots=0;int dc=L.result.draw_clock,shot=dc>=180 ? 3:dc>=120 ? 2:dc>=89 ? 1:dc>=30 ? 0:-1;
            if(shot>=0 && !(shots&(1u<<shot))){char name[48],line[256];shots|=1u<<shot;
                snprintf(name,sizeof name,"shot_result_%d.raw",shot);dump_shot(name);
                int n=snprintf(line,sizeof line,"RESULT_HUD shot=%d class=%d phase=%d clock=%d draw=%d health=%d timer=%d cans=%d displayed=%d total=%d triangles=%d\n",
                    shot,L.result.class_id,L.result.phase,L.result.clock,dc,L.result.health,L.result.timer,L.gf.cancount,L.result.displayed,L.result.total,failure_models.models.drawn_tris);
                if(logfd>=0)runtime_log_write(logfd,line,n);
                runtime_log("RESULT_CONTEXT joined=%d souls=%d record=%d name=%d\n",L.result.joined,L.result.souls,L.result.new_record,result_hud_joined_name(&L.result,1)!=NULL);
            }
        }
        if(failurehudtest && L.failure.active && L.failure.phase==3){
            static int shot=-1;int now=L.failure.clock/30;
            if(now!=shot && now<4){shot=now;char name[64],line[160];
                snprintf(name,sizeof name,"shot_failure_%d.raw",now);dump_shot(name);
                int n=snprintf(line,sizeof line,"FAILURE_HUD phase=%d clock=%d alpha=%d choice=%d triangles=%d age=%d\n",L.failure.phase,L.failure.clock,L.failure.alpha,L.failure.selection,failure_models.models.drawn_tris,failurehud_age);
                if(logfd>=0)runtime_log_write(logfd,line,n);
            }
        }
        if(policehudtest && policehud_age>=0) {
            int f=(int)L.police.lframe,slot=f==1 ? 0:f==15 ? 1:f==29 ? 2:f==59 ? 3:f==75 ? 4:-1;
            if(slot>=0 && !(policehud_shots&(1u<<slot))) {
                policehud_shots|=1u<<slot;
                char name[64],line[192];snprintf(name,sizeof name,"shot_police_%d_%02d.raw",L.police.level,f);dump_shot(name);
                int n=snprintf(line,sizeof line,"POLICE_HUD level=%d frame=%d phase=%d motion=%d arrow=%.0f load=%d\n",
                    L.police.level,f,L.police.lstate,L.police.lmotion,L.police.arrow_frame,hudpol_err);
                if(logfd>=0)runtime_log_write(logfd,line,n);
            }
        }
        if (movementtest) {
            /* Supplied effect fixture, distinct from turninputtest. */
            if (logfd>=0) {
                const SkateEffects *fx=&pl->effects;char line[180];int tails=0;
                for (int k=0;k<32;k++) tails+=fx->tails[k].active!=0;
                int n=snprintf(line,sizeof line,"FX %d load=%d st=%d motion=%d flags=%x grind=%d%d wall=%d%d tails=%d\n",
                    movementframe,skatefx_err,pl->state,pl->logical_motion,pl->motion_flags,
                    fx->grind[0].active,fx->grind[1].active,fx->wall[0].active,fx->wall[1].active,tails);
                runtime_log_write(logfd,line,n);
            }
            if (movementframe==30) dump_shot(movementtest==1 ? "shot_fx_rail.raw" : "shot_fx_wall.raw");
            if (movementframe>=30 && movementframe<=52 && !(movementframe&1)) {
                char name[40];snprintf(name,sizeof name,"shot_fx_%02d.raw",(movementframe-30)/2);dump_shot(name);
            }
            if (movementframe==80) break;
        }
        if(skateinputtest && skateinputclock>0 && skateinputclock<8){
            const SkateEffects *fx=&pl->effects;int sparks=0;for(int k=0;k<24;k++)sparks+=fx->dash_sparks[k].active;
            runtime_log("SKATE_INPUT tick=%d state=%d motion=%d speed=%.7g flags=%x boost=%d sparks=%d trail=%d,%d\n",
                (int)(skateinputclock*30.f),pl->state,pl->logical_motion,pl->speed,pl->motion_flags,pl->boost,sparks,fx->trail[0].count,fx->trail[1].count);
            int shot=pl->state==2 && fx->trail[0].count>=10 ? 1:sparks>=2 ? 0:-1;
            if(shot>=0 && !(skateinputshots&(1<<shot))){
                char name[40];snprintf(name,sizeof name,"shot_skate_input_%d.raw",shot);dump_shot(name);skateinputshots|=1<<shot;
            }
        }
        if(turninputtest && pl->logical_motion==15){
            int shot=pl->frame>=9 ? 1:0;
            if(!(turninputshots&(1<<shot))){
                turninputshots|=1<<shot;char name[48];snprintf(name,sizeof name,"shot_turn_input_%d.raw",shot);dump_shot(name);
                runtime_log("TURN_INPUT shot=%d tick=%d frame=%.9g\n",shot,turninputticks,pl->frame);
            }
        }
        if (autopilot && (frames == 400 || frames == 1300 || frames==1900 || frames==3300)) dump_shot(frames == 400 ? "shot.raw" : frames==1300 ? "shot2.raw":frames==1900 ? "shot3.raw":"shot4.raw");
        if(autopilot && L.can_hud.children) {
            /* Observe natural pickup children only; never supply inventory,
               position or progress. Raw dumps require software rendering. */
            static unsigned captured=0;
            const CanFlight *f=&L.can_hud.children->flight;
            int shot=f->phase==1 ? 0:f->phase==2 && f->rotation==0x2aaa*6 ? 1:
                f->phase==2 && f->rotation==0x2aaa*13 ? 2:-1;
            if(shot>=0 && !(captured&(1u<<shot))) {
                captured|=1u<<shot;char name[48];snprintf(name,sizeof name,"shot_pickup_%d.raw",shot);dump_shot(name);
                if(logfd>=0){char line[192];int n=snprintf(line,sizeof line,"PICKUP_HUD shot=%d tick=%u inventory=%d displayed=%d phase=%d xyz=%.9g,%.9g,%.9g error=%d\n",
                    shot,pl->logic_tick,L.gf.cancount,L.can_hud.counter.displayed,f->phase,f->position[0],f->position[1],f->position[2],L.can_hud.error);runtime_log_write(logfd,line,n);}
            }
        }
        if(rivaldroptest && rival_drop_tick && !scene_frozen) {
            int flying=0,landed=0,taken=0;
            for(const CanDrop *d=L.gf.can_drops;d;d=d->next){flying+=d->flight.state<3;landed+=d->flight.state==3;taken+=d->can.taken;}
            int retired=rival_drop_count-(int)L.gf.ncan_drops;
            int shot=flying && rival_drop_tick>=8 && !(rival_drop_shots&1) ? 0:
                     landed+retired==rival_drop_count && taken==0 && rival_drop_tick>=50 && !(rival_drop_shots&2) ? 1:
                     taken+retired==rival_drop_count && (rival_drop_shots&2) && !(rival_drop_shots&4) ? 2:-1;
            if(shot<0 && !(rival_drop_shots&8))for(const CanFlightTask *t=L.can_hud.children;t;t=t->next)
                if(t->health_kind && t->flight.phase==2 && t->flight.position[2]>-60.f){shot=3;break;}
            if(shot<0 && !(rival_drop_shots&16) && player_recovery_flash(pl)>.49f)shot=4;
            if(shot<0 && (rival_drop_shots&16) && !(rival_drop_shots&32) && !pl->recovery_flash_ticks)shot=5;
            if(shot>=0) {
                char name[48];snprintf(name,sizeof name,"shot_rival_drop_%d.raw",shot);dump_shot(name);rival_drop_shots|=1<<shot;
                runtime_log("RIVAL_DROP_TEST shot=%d tick=%d flying=%d landed=%d taken=%d inventory=%d initial=%d health=%d initial_health=%d retired=%d recovery=%d pulse=%.9g clock=%u\n",shot,rival_drop_tick,flying,landed,taken,L.gf.cancount,rival_drop_initial,pl->health,rival_drop_health,retired,pl->recovery_flash_ticks,player_recovery_flash(pl),pl->logic_tick);
            }
            if(rival_drop_tick%30==0)runtime_log("RIVAL_DROP_TEST tick=%d flying=%d landed=%d taken=%d inventory=%d xyz=%.3f,%.3f,%.3f\n",rival_drop_tick,flying,landed,taken,L.gf.cancount,pl->x,pl->y,pl->z);
        }
        if(camtest_on && camtest_sweep && !scene_frozen && frames>=1000 && frames<=1720) {
            if(!(frames%60)) {char path[48];snprintf(path,sizeof path,"shot_ped_view_%d.raw",frames);dump_shot(path);}
            if(camtest_edge_capture && camtest_edge_shots<4) {
                char path[48];snprintf(path,sizeof path,"shot_ped_edge_%d.raw",camtest_edge_shots++);dump_shot(path);
                runtime_log("PED_EDGE_CAPTURE frame=%d shot=%d\n",frames,camtest_edge_shots-1);
            }
        }
        if(policegrabtest && police_grab_tick && !scene_frozen) {
            int shot=-1,count=police_grip_count(&pl->police_grips);
            if(count && !(police_grab_shots&1) && police_grab_tick>police_grab_first+10)shot=0;
            else if(pl->police_knockdown && !(police_grab_shots&2))shot=1;
            else if(police_grab_release && !(police_grab_shots&4))shot=2;
            else if(police_grab_tick>=500 && !(police_grab_shots&8))shot=3;
            if(shot>=0){char name[48];snprintf(name,sizeof name,"shot_police_grab_%d.raw",shot);dump_shot(name);police_grab_shots|=1<<shot;}
        }
        if(spraytest==4 && L.gf.tagged>bus_event_tags){
            bus_event_tags=L.gf.tagged;bus_event_wait=0;
            runtime_log("BUS_EVENT_TAG total=%d target=%d\n",L.gf.tagged,bus_event_large ? cityspray.target:spraytest_spot);
        }
        if(spraytest==5) {
            if(L.gf.tagged>bus_event_tags){bus_event_tags=L.gf.tagged;runtime_log("ARRIVAL_TAG total=%d writable=%d\n",L.gf.tagged,campaign_writable);}
            int id=L.scene.vm.id,bit=id==78+arrival_area*6 ? 1:id==79+arrival_area*6 ? 2:0;
            if(scene_frozen && bit && !(arrival_scenes&bit) && L.scene.vm.clock>=30) {
                char path[48];snprintf(path,sizeof path,"shot_arrival_%d.raw",id);dump_shot(path);arrival_scenes|=bit;
                runtime_log("ARRIVAL_SCENE id=%d tags=%d\n",id,L.gf.tagged);
            }
            if(arrival_scenes==3 && !sceneplay_resident(&L.scene) && !arrival_done && ++arrival_wait>=300) {
                int chief=0,police=0,tanks=0;
                for(int i=0;i<L.ngangs;i++)if(L.gangs[i].active){const Gang *g=L.gangs+i;
                    chief+=g->kind==17;police+=g->kind==0;tanks+=g->kind==10;}
                for(int i=0;i<L.npcs.count;i++)if(L.npcs.p[i].active && L.npcs.p[i].type==30)tanks++;
                runtime_log("ARRIVAL_COMPLETE area=%d tags=%d chief=%d police=%d tanks=%d error=%d writable=%d\n",
                    arrival_area,L.gf.tagged,chief,police,tanks,L.campaign_error,campaign_writable);
                dump_shot("shot_arrival_complete.raw");arrival_done=1;
            }
        }
        if(spraytest==4 && scene_frozen && (L.scene.vm.id==8 || L.scene.vm.id==9) && L.scene.vm.clock>=30) {
            int bit=1<<(L.scene.vm.id-8);if(!(bus_event_shots&bit)){
                char path[48];snprintf(path,sizeof path,"shot_bus_event_%d.raw",L.scene.vm.id);dump_shot(path);bus_event_shots|=bit;
            }
        }
        if(spraytest==4 && L.gf.tagged==7 && !sceneplay_resident(&L.scene) && !L.campaign_error && !bus_event_captured && ++bus_event_wait>=90) {
            runtime_log("BUS_EVENT_RESUMED tagged=%d traffic=%u world_error=%d player_audio=%d world_audio=%d spray=%d writable=%d\n",
                L.gf.tagged,L.traffic ? L.traffic->world.clock:0,L.traffic ? L.traffic->world.error:-1,
                player_audio_selection,world_audio_selection,spraychar_error,campaign_writable);
            dump_shot("shot_bus_event_resumed.raw");bus_event_captured=1;
        }
        if(spraytest==2 && spraytest_ticks>spraytest_logged) {
            spraytest_logged=spraytest_ticks;
            if(logfd>=0) {
                char line[180];int n=snprintf(line,sizeof line,"SMALL_SPRAY tick=%d active=%d ref=%d frame=%.9g variant=%d bank=%d physics=%u\n",
                    spraytest_ticks,pl->small_spray_active,pl->small_spray_ref,pl->small_spray_frame,
                    pl->spray_variant,pl->spray_bank!=NULL,pl->logic_tick);runtime_log_write(logfd,line,n);
            }
            if(spraytest_ticks==1 || spraytest_ticks==20 || spraytest_ticks==40 || spraytest_ticks==65) {
                char name[48];snprintf(name,sizeof name,"shot_spray_small_%03d.raw",spraytest_ticks);dump_shot(name);
            }
        }
        if(spraytest==3 && cityspray.target>=0 && !cityspray.captured) {
            Spot *sp=L.gf.spots+cityspray.target;
            char line[256];int n=snprintf(line,sizeof line,"CITY_SPRAY tick=%d spot=%d area=%d index=%d strokes=%u/%u cans=%d active=%d variant=%d bank=%d camera=%d fov=%d done=%d\n",
                cityspray.ticks,cityspray.target,sp->area,sp->idx,sp->stroke,sp->nstrokes,L.gf.cancount,
                pl->spray_active,pl->spray_variant,pl->spray_bank!=NULL,pl->spray_camera.active,pl->spray_camera.fov,cityspray.done);
            if(logfd>=0)runtime_log_write(logfd,line,n);
            if(cityspray.ticks==20 || cityspray.ticks==60) {
                char name[48];snprintf(name,sizeof name,"shot_spray_city_%03d.raw",cityspray.ticks);dump_shot(name);
            }
            if(cityspray.done){
                unsigned notes,errors,position;int active;audio_menu_info(&notes,&errors,&position,&active);
                runtime_log("CITY_AUDIO notes=%u errors=%u\n",notes,errors);
                dump_shot("shot_spray_city_done.raw");cityspray.captured=1;
            }
        }
        if(spraytest==3 && L.gf.finish.life){
            unsigned age=60-L.gf.finish.life;
            const unsigned samples[4]={2,8,28,40};
            for(int i=0;i<4;i++)if(age>=samples[i] && !(finish_shots&(1u<<i))){
                char path[48];snprintf(path,sizeof path,"shot_spray_finish_%d.raw",i);dump_shot(path);finish_shots|=1u<<i;
                runtime_log("HUD_FINISH shot=%d life=%u frame=%u strokes=%u tagged=%d\n",i,L.gf.finish.life,L.gf.finish.frame,
                    L.gf.spots[cityspray.target].stroke,L.gf.tagged);
            }
        }
        if(openingtest && opening_started){
            unsigned age=pl->logic_tick-opening_tick;const unsigned times[]={1,20,40,60,90,120,250,360};
            for(unsigned k=0;k<8;k++)if(age>=times[k] && !(opening_shots&(1u<<k))){
                char name[48];snprintf(name,sizeof name,"shot_opening_%u.raw",times[k]);dump_shot(name);opening_shots|=1u<<k;
            }
        }
        if(traintest && train_started){
            unsigned age=pl->logic_tick-train_tick;const unsigned times[]={1,100,150,200,250,300,400,550,600,650,700,750,800};
            for(unsigned k=0;k<sizeof times/sizeof *times;k++)if(age>=times[k] && !(train_shots&(1u<<k))){
                char name[48];snprintf(name,sizeof name,"shot_train_%u.raw",times[k]);dump_shot(name);train_shots|=1u<<k;
            }
        }
        if(water_test_case){
            const unsigned times[]={10,35,50,65,90,145,210,245,325,390,425,530};
            for(unsigned k=0;k<12;k++)if(water_test_ticks>=times[k] && !(water_shots&(1u<<k))){
                char name[48];snprintf(name,sizeof name,"shot_water_%u.raw",times[k]);dump_shot(name);water_shots|=1u<<k;
            }
        }
        if(koganehousetest && house_started){
            unsigned age=pl->logic_tick-house_tick;const unsigned times[]={1,20,40,60,90,120,250,420};
            for(unsigned k=0;k<8;k++)if(age>=times[k] && !(house_shots&(1u<<k))){
                char name[48];snprintf(name,sizeof name,"shot_house_%u.raw",times[k]);dump_shot(name);house_shots|=1u<<k;
            }
        }
        if(sewerwatertest && sewerwater_started){
            unsigned age=pl->logic_tick-sewerwater_tick;const unsigned times[]={20,35,50,65,95};
            for(unsigned k=0;k<5;k++)if(age>=times[k] && !(sewerwater_shots&(1u<<k))){
                char name[48];snprintf(name,sizeof name,"shot_water_shallow_%u.raw",times[k]);dump_shot(name);sewerwater_shots|=1u<<k;
            }
        }
        if(hudpolishtest && hudpolish_started){
            const SkateBurst *b=&pl->effects.impact;
            const float samples[4]={2,5,9,12};
            for(int i=0;i<4;i++)if(b->active && b->frame>=samples[i] && !(hudpolish_shots&(1u<<i))){
                char path[48];snprintf(path,sizeof path,"shot_impact_%d.raw",i);dump_shot(path);hudpolish_shots|=1u<<i;
                runtime_log("HUD_IMPACT shot=%d frame=%.0f point=%.6g,%.6g,%.6g\n",i,b->frame,b->matrix[3],b->matrix[7],b->matrix[11]);
            }
            if(!b->active && hudpolish_started==1){hudpolish_started=2;runtime_log("HUD_IMPACT complete shots=%u frame=%.0f\n",hudpolish_shots,b->frame);}
        }
        if(finalprogresstest && pl->spray_active && (frames==600 || frames==1300 || frames==2500)) {
            char name[40];snprintf(name,sizeof name,"shot_spray_%04d.raw",frames);dump_shot(name);
        }
        if(finalprogresstest && !(frames%120))runtime_log("FINAL_INPUT tick=%d phase=%d target=%d active=%d cue=%d wait=%d cans=%d strokes=%u,%u,%u,%u health=%d error=%d\n",
            finalfixture.ticks,L.final.phase,finalfixture.target,L.gf.mg_active,L.gf.mg_cue,L.gf.mg_wait,L.gf.cancount,
            L.gf.spots[0].stroke,L.gf.spots[1].stroke,L.gf.spots[2].stroke,L.gf.spots[3].stroke,pl->health,L.final.error);
        if(finalprogresstest && L.final.clear_requested && !finalfixture.captured) {
            char line[180];int n=snprintf(line,sizeof line,"FINAL_INPUT complete phase=%d tags=%d strokes=%u,%u,%u,%u boss=%u error=%d mission_active=%d ticks=%d\n",
                L.final.phase,L.gf.tagged,(unsigned)L.gf.spots[0].stroke,(unsigned)L.gf.spots[1].stroke,(unsigned)L.gf.spots[2].stroke,(unsigned)L.gf.spots[3].stroke,(unsigned)L.gf.spots[4].complete,L.final.progression_error,L.ms.active,finalfixture.ticks);
            if(logfd>=0)runtime_log_write(logfd,line,n);
            dump_shot("shot_final_input.raw");finalfixture.captured=1;
        }
        if(helitest && heli_test_index>=0 && heli_test_index<L.ngangs) {
            const Gang *g=L.gangs+heli_test_index;
            int sample=g->state==16 && g->wreck.frame>=20.f ? 0:g->state==17 && g->wreck.frame>=25.f ? 1:
                g->state==18 && g->wreck.explosion==14 ? 2:-1;
            if(sample>=0 && !(heli_test_shots&(1u<<sample))) {
                char path[40];snprintf(path,sizeof path,"shot_heli_%d.raw",sample);dump_shot(path);heli_test_shots|=1u<<sample;
            }
        }
        if(autopilot && finalcollapsetest && logfd>=0 && (frames%100)==0) {
            unsigned removed=0;for(int i=0;i<4;i++)if(L.final.pieces[i].deleted)removed|=1u<<i;
            char line[160];int n=snprintf(line,sizeof line,"COLLAPSE frame=%d phase=%d removed=%u clip=%d world_error=%d\n",frames,L.final.phase,removed,L.final.pieces[0].collapse_frame,L.final.error);
            runtime_log_write(logfd,line,n);
        }
        if(autopilot && finalformtest && logfd>=0 && (frames%100)==0) {
            char line[160];int n=snprintf(line,sizeof line,"FINALFORM frame=%d phase=%d mask=%u boss_mask=%u rotation=%u world_error=%d\n",frames,L.final.phase,L.final.collider.active,L.final.boss_collider.active,L.final.boss.final_rotation,L.final.error);
            runtime_log_write(logfd,line,n);
        }
        if(autopilot && sceneplay_resident(&L.scene)) {
            static int shot_id=-1;
            if(shot_id!=L.scene.vm.id){scene_shots=0;shot_id=L.scene.vm.id;}
            const int final_scene=L.scene.vm.id==103 || L.scene.vm.id==90;
            const int recruitment=scene_recruit_supported(L.scene.vm.id,L.scene.vm.recruit_mode);
            const int cube_expression=L.scene.vm.character==6 && (L.scene.vm.id==54 || L.scene.vm.id==55);
            /* Cube's original idle blink is full size at EVENT 54 clock
               132 (companion mode 2, frame 25); adjacent samples bracket it. */
            const int regular[8]={final_scene || recruitment ? 50:100,final_scene || recruitment ? 150:230,recruitment ? 190:final_scene ? 270:300,400,550,130,132,134};
            const int explosion_samples[8]={120,130,150,180,220,260,310,325};
            const int *samples=L.scene.vm.id==25?explosion_samples:regular;
            for(int i=0;i<(cube_expression || L.scene.vm.id==25 ? 8:L.scene.vm.id==12 ? 5:3);i++)if(!(scene_shots&(1u<<i)) && L.scene.vm.clock>=samples[i]) {
                char path[40];snprintf(path,sizeof path,"shot_scene%d_%d.raw",L.scene.vm.id,samples[i]);dump_shot(path);scene_shots|=1u<<i;
                if(cube_expression)runtime_log("SCENE_EXPRESSION id=%d clock=%.0f character=%d mode=%d frame=%.0f\n",L.scene.vm.id,L.scene.vm.clock,L.scene.vm.character,L.scene.vm.player_mouth_mode,L.scene.vm.player_mouth_frame);
                if(L.scene.vm.id==25) {
                    int live=0;for(int k=0;k<16;k++)live+=L.scene.vm.effects[k].active;
                    runtime_log("SCENE_EXPLOSIONS clock=%.0f spawned=%u active=%d loaded=%d triangles=%d audio_error=%d\n",L.scene.vm.clock,L.scene.vm.effect_serial,live,L.scene.effects_loaded,L.scene.effects.drawn_tris,L.scene.audio_error);
                }
            }
        }
        if(autopilot && recruit_demo_active(&L) && L.recruit_demo_tick) {
            static unsigned demo_shots;static int demo_set=-1,demo_character=-1;
            if(demo_set!=L.set_idx || demo_character!=(int)L.title_route.character || L.recruit_demo_tick==1){
                demo_set=L.set_idx;demo_character=L.title_route.character;demo_shots=0;
            }
            const char *name=L.recruit_demo->mission;
            const unsigned samples[]={60,120,180,240,300};
            for(unsigned i=0;i<5;i++)if(!(demo_shots&(1u<<i)) && (unsigned)L.recruit_demo_tick>=samples[i]) {
                char path[48];snprintf(path,sizeof path,"shot_challenge_%s_%d_%u.raw",name,L.set_idx,samples[i]);dump_shot(path);demo_shots|=1u<<i;
                runtime_log("CHALLENGE_SAMPLE name=%s set=%d row=%d tick=%u character=%u pos=%.9g,%.9g,%.9g small_spray=%d spray_frame=%.9g variant=%d mist=%d\n",name,L.set_idx,L.recruit_demo_tick,L.title_sample.tick,L.title_route.character,pl->x,pl->y,pl->z,pl->small_spray_active,pl->small_spray_frame,pl->spray_variant,pl->effects.spray.active);
            }
        }
        if(autopilot && title_mode) {
            static unsigned title_shots;const unsigned samples[]={1,60,90,120,160,220,230,235,251,240,270,480,900,1500,3000,4840,5100};
            for(unsigned i=0;i<sizeof samples/sizeof samples[0];i++)if(!(title_shots&(1u<<i)) && (unsigned)L.demo_tick_n>=samples[i]) {
                unsigned voice_position;int voice_active,voice_error;audio_movie_voice_info(&voice_position,&voice_active,&voice_error);
                char name[48];snprintf(name,sizeof name,"shot_title_%u.raw",samples[i]);dump_shot(name);title_shots|=1u<<i;
                runtime_log("TITLE_SAMPLE row=%d tick=%u part=%u character=%u pos=%.9g,%.9g,%.9g camera=%.9g,%.9g,%.9g song=%d voice=%u/%d/%d\n",
                    L.demo_tick_n,L.title_sample.tick,L.title_sample.part,L.title_route.character,
                    pl->x,pl->y,pl->z,eye[0],eye[1],eye[2],bgm_current(),voice_position,voice_active,voice_error);
            }
        }
        if (autopilot && title_mode && title_frames == 84) dump_shot("shot_title.raw");   /* self-test screenshot */
        if(exit_fixture_destination==2 && title_mode && title_frames>=10) {
            dump_shot("shot_exit_destination.raw");exit_fixture_destination=0;
            runtime_log("EXIT_FIXTURE destination=title rendered=1\n");
            runtime_log_safe_checkpoint("exit_fixture_destination");
        }
        if(JGR_DIAGNOSTICS || viewer_select_state.stats || viewer_select_state.held){unsigned t1 = sceKernelGetSystemTimeLow();float ms=(t1-t0)*1e-3f;
            if(!render_bench_completed){ms_avg+=(ms-ms_avg)*.1f;if(frames>=30 && ms>ms_max)ms_max=ms;}}
        frames++;
        if(log_fixture && (unsigned)frames>=log_fixture) {
            runtime_log("LOG_FIXTURE exit_frame=%u orderly=1\n",frames);
            exit_callback(0,0,NULL);
        }
    }
    /* HOME Exit must not free the EBOOT while either ME or its kernel
       watchdog can still access it. Stop output first, then require reset. */
    hang_mark(HANG_EXIT);
    gameplay_audio_preload_finish(NULL);
    runtime_profile_dump("exit");
    while(!audio_shutdown()){runtime_log("ME_EXIT waiting for reset fence\n");runtime_log_flush();sceKernelDelayThread(20000);}
    runtime_log("ME_EXIT fenced; audio stopped\n");runtime_log_flush();
    free(audio_heap_reserve);gfx_term();
    hang_shutdown();int crash_stop=crash_shutdown();
    runtime_log("CRASH_HANDLER shutdown=%d\n",crash_stop);runtime_log_flush();
    runtime_log_safe_checkpoint("exit");
    if(logfd>=0){sceIoClose(logfd);logfd=-1;}
    sceKernelExitGame();return 0;
}
