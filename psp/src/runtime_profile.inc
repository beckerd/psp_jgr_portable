/* Compact numeric windows, formatted only after gameplay stops. */
#define RUNTIME_PROFILE_WINDOWS 8u
typedef struct {
    GfxProfile gfx;
    unsigned frame,heap_free,draws,fast_tris,clipped_tris,texture_binds;
    unsigned visibility_us,hidden_objects,hidden_batches,player_us,collision_us,traffic_us;
    int state,stage,scene,zone;
} RuntimeProfileWindow;
static RuntimeProfileWindow runtime_profiles[RUNTIME_PROFILE_WINDOWS];
static unsigned runtime_profile_collect_us,runtime_profile_collect_max_us;
static unsigned runtime_profile_count,runtime_profile_next,runtime_profile_windows;
static unsigned runtime_profile_max_us,runtime_profile_min_heap=~0u;
static unsigned long long runtime_profile_frames,runtime_profile_logic,runtime_profile_submit;
static unsigned long long runtime_profile_ge,runtime_profile_vblank;
static void runtime_profile_remember(const GfxProfile *p,unsigned frame,int state,int stage,int scene,int zone)
{
    RuntimeProfileWindow *w=runtime_profiles+runtime_profile_next;
    *w=(RuntimeProfileWindow){.gfx=*p,.frame=frame,.state=state,.stage=stage,.scene=scene,.zone=zone};
    StageProfile world;PlayerProfile player;ColProfile collision;PathWorldProfile traffic;unsigned textures[4];
    stage_profile_read(&world);player_profile_read(&player);col_profile_read(&collision);
    pathworld_profile_read(&traffic);stage_texture_profile(textures);
    w->draws=world.draw_calls;w->fast_tris=world.fast_triangles;w->clipped_tris=world.clipped_triangles;
    w->texture_binds=textures[2]+textures[3];w->visibility_us=world.visibility_us;
    w->hidden_objects=world.occlusion_rejected;w->hidden_batches=world.occlusion_batches;
    w->player_us=player.total_us;w->collision_us=collision.ray_us+collision.body_us;w->traffic_us=traffic.total_us;
    struct mallinfo mi=mallinfo();w->heap_free=(unsigned)mi.fordblks;
    crash_memory_sample(frame,stage,state,w->heap_free,sceKernelTotalFreeMemSize(),sceKernelMaxFreeMemSize());
    runtime_profile_next=(runtime_profile_next+1)%RUNTIME_PROFILE_WINDOWS;
    if(runtime_profile_count<RUNTIME_PROFILE_WINDOWS)runtime_profile_count++;
    runtime_profile_windows++;runtime_profile_frames+=p->frames;
    runtime_profile_logic+=p->logic_us;runtime_profile_submit+=p->submit_us;
    runtime_profile_ge+=p->ge_us;runtime_profile_vblank+=p->vblank_us;
    if(p->max_frame_us>runtime_profile_max_us)runtime_profile_max_us=p->max_frame_us;
    if(w->heap_free<runtime_profile_min_heap)runtime_profile_min_heap=w->heap_free;
}
static void runtime_profile_dump(const char *reason)
{
    runtime_log_checkpoint(reason);
    if(!JGR_DIAGNOSTICS)return;
    if(!runtime_profile_windows)return;
    runtime_log("PERF_RAM_SESSION reason=%s windows=%u retained=%u frames=%llu logic_us=%llu submit_us=%llu ge_us=%llu vblank_us=%llu max_span_us=%u min_heap=%u collect_us=%u max_collect_us=%u context=end_of_window all_states=1 completed_windows_only=1\n",
        reason,runtime_profile_windows,runtime_profile_count,runtime_profile_frames,runtime_profile_logic,
        runtime_profile_submit,runtime_profile_ge,runtime_profile_vblank,runtime_profile_max_us,runtime_profile_min_heap,runtime_profile_collect_us,runtime_profile_collect_max_us);
    for(unsigned i=0;i<runtime_profile_count;i++) {
        const RuntimeProfileWindow *w=runtime_profiles+(runtime_profile_next+RUNTIME_PROFILE_WINDOWS-runtime_profile_count+i)%RUNTIME_PROFILE_WINDOWS;
        const GfxProfile *p=&w->gfx;
        runtime_log("PERF_RAM frame=%u state=%d stage=%d scene=%d zone=%d frames=%u logic_us=%u submit_us=%u ge_us=%u vblank_us=%u max_span_us=%u list_bytes=%u heap_free=%u\n",
            w->frame,w->state,w->stage,w->scene,w->zone,p->frames,p->logic_us,p->submit_us,p->ge_us,p->vblank_us,p->max_frame_us,p->max_list_bytes,w->heap_free);
        runtime_log("PERF_RAM_DRAW frame=%u other=%u sky=%u city=%u props=%u actors=%u effects=%u hud=%u tags=%u draws=%u fast_tris=%u clipped_tris=%u texture_binds=%u visibility_us=%u hidden_objects=%u hidden_batches=%u\n",
            w->frame,p->section_us[0],p->section_us[1],p->section_us[2],p->section_us[3],p->section_us[4],p->section_us[5],p->section_us[6],p->section_us[7],
            w->draws,w->fast_tris,w->clipped_tris,w->texture_binds,w->visibility_us,w->hidden_objects,w->hidden_batches);
        runtime_log("PERF_RAM_LOGIC frame=%u other=%u player=%u rivals=%u npcs=%u mission=%u cans=%u gangs=%u player_us=%u collision_us=%u traffic_us=%u\n",
            w->frame,p->logic_section_us[0],p->logic_section_us[1],p->logic_section_us[2],p->logic_section_us[3],p->logic_section_us[4],p->logic_section_us[5],p->logic_section_us[6],w->player_us,w->collision_us,w->traffic_us);
    }
    AudioMeProfile me;MusicProfile music;StreamProfile stream;
    audio_me_profile_read(&me);audio_music_profile(&music);stream_profile(&stream);
    runtime_log("PERF_RAM_AUDIO timeouts=%u fallbacks=%u quarantined=%d underruns=%u io_error=%d thread_error=%d open_failures=%u\n",
        me.timeouts,me.fallbacks,me.quarantined,music.underruns,stream.io_error,stream.thread_error,stream.open_failures);
}
