/* One-shot physical-frame evidence. No file I/O until armed frame trips.
   Included after Level; all persistent storage is static, never PSP stack. */
#include "spike_capture.h"
typedef char SpikeHomeButtonMatchesSdk[(SPIKE_CAPTURE_HOME_BUTTON==PSP_CTRL_HOME) ? 1:-1];
typedef struct {
    SpikeFrame frame;GfxFrameSample timing;
    StageProfile stage;StageCityProfile city;ColProfile collision;PlayerProfile player;
    PathWorldProfile traffic;TrafficDrawProfile traffic_draw;
    CharWorkStats actors[CHAR_WORK_COUNT];NpcOcclusionProfile peds;
    float view[16],proj[16],clip[16],forward[3],yaw,pitch;
    unsigned tick,buttons,overlap_buttons,latest_buttons,log_bytes,log_calls,profile_us,ram_profile_us,snapshot_us;
    int context_running,context_suspended;
    int state,set,scene,phase,cull,wire,vsync,npcs,vehicles;
    char mission[16];
} SpikeDetail;
static SpikeCapture spike_capture;
static SpikeDetail spike_pending,spike_before;
static unsigned spike_generation,spike_seen_serial,spike_log_start,spike_bytes_start,spike_calls_start,spike_profile_start,spike_ram_start;
static unsigned spike_present_log,spike_present_bytes,spike_present_calls;
static unsigned spike_fixture;
static ColProfile spike_overlap_collision;
static PlayerProfile spike_overlap_player;
static PathWorldProfile spike_overlap_traffic;
static char spike_text_buffer[4096];
static const char spike_city_names[]="setup source_selection frustum occlusion traversal material geometry index_assembly submission instance_prepare";
static void spike_begin_counters(void)
{
    if(!JGR_DIAGNOSTICS || !spike_enabled)return;
    stage_profile_totals(&spike_before.stage);col_profile_totals(&spike_before.collision);
    player_profile_totals(&spike_before.player);pathworld_profile_totals(&spike_before.traffic);
    traffic_draw_totals(&spike_before.traffic_draw);chars_work_totals(spike_before.actors);
    npcs_occlusion_totals(&spike_before.peds);
}
/* All these profile structs consist exclusively of unsigned counters.
   memcpy avoids aliasing assumptions. Collision byte gauges are restored. */
static void spike_delta(void *out,const void *before,unsigned bytes)
{
    unsigned char *p=out;const unsigned char *b=before;
    for(unsigned i=0;i<bytes;i+=sizeof(unsigned)){unsigned x,y;memcpy(&x,p+i,sizeof x);memcpy(&y,b+i,sizeof y);x-=y;memcpy(p+i,&x,sizeof x);}
}
static void spike_save_submit(Level *l,unsigned frame,int eligible,int state,
    const float *eye,const float *forward,const float *view,const float *proj,const float *clip,
    float yaw,float pitch,unsigned buttons,int cull,int wire,int vsync)
{
    if(!JGR_DIAGNOSTICS || !spike_enabled)return;
    unsigned snapshot_started=sceKernelGetSystemTimeLow();
    SpikeDetail *d=&spike_pending;memset(d,0,sizeof *d);
    if(!gfx_capture_submitted(&d->timing))return;
    eligible=eligible && spike_capture_context_valid(running,power_sleeping || power_resumed,buttons,buttons,buttons);
    d->frame=(SpikeFrame){.frame=frame,.generation=spike_generation,.eligible=eligible,
        .stage=l->id,.mission=l->mrec ? (int)(l->mrec-missions):-1,.zone=l->cur_zone};
    d->frame.player[0]=l->pl.x;d->frame.player[1]=l->pl.y;d->frame.player[2]=l->pl.z;
    memcpy(d->frame.eye,eye,12);memcpy(d->forward,forward,12);memcpy(d->view,view,64);memcpy(d->proj,proj,64);memcpy(d->clip,clip,64);
    d->yaw=yaw;d->pitch=pitch;d->tick=l->pl.logic_tick;d->buttons=buttons;
    d->state=state;d->set=l->set_idx;d->scene=l->scene.vm.id;d->phase=l->ms.phase;
    d->cull=cull;d->wire=wire;d->vsync=vsync;d->npcs=npcs_active(&l->npcs);d->vehicles=l->traffic ? l->traffic->world.count:0;
    snprintf(d->mission,sizeof d->mission,"%.15s",l->mrec ? l->mrec->name:"?");
#define SNAP(member,fn) fn(&d->member);spike_delta(&d->member,&spike_before.member,sizeof d->member)
    SNAP(stage,stage_profile_totals);SNAP(player,player_profile_totals);SNAP(traffic,pathworld_profile_totals);SNAP(traffic_draw,traffic_draw_totals);
    col_profile_totals(&d->collision);unsigned a=d->collision.grid_bytes,b=d->collision.grid_indexed_bytes,c=d->collision.grid_dirty_bytes;
    spike_delta(&d->collision,&spike_before.collision,sizeof d->collision);
    d->collision.grid_bytes=a;d->collision.grid_indexed_bytes=b;d->collision.grid_dirty_bytes=c;
    chars_work_totals(d->actors);spike_delta(d->actors,spike_before.actors,sizeof d->actors);
    SNAP(peds,npcs_occlusion_totals);
#undef SNAP
    d->frame.stage_draws=d->stage.draw_calls;d->frame.stage_tris=l->st.drawn_tris;
    d->frame.logic_us=d->timing.logic_us;d->frame.submit_us=d->timing.submit_us;d->frame.list_bytes=d->timing.list_bytes;
    d->frame.log_us=spike_log_us-spike_log_start;d->log_bytes=spike_log_bytes-spike_bytes_start;d->log_calls=spike_log_calls-spike_calls_start;
    d->profile_us=spike_profile_us-spike_profile_start;d->ram_profile_us=runtime_profile_collect_us-spike_ram_start;
    stage_city_profile_get(&d->city);
    memcpy(d->frame.city_us,d->city.timing.phase_us,sizeof d->frame.city_us);
    d->frame.city_clock_reads=d->city.timing.clock_reads;
    stage_capture_submit(&l->st);
    d->snapshot_us=sceKernelGetSystemTimeLow()-snapshot_started;
}
static void spike_words(FILE *out,const char *tag,const char *names,const void *data,unsigned bytes)
{
    const unsigned char *p=data;fprintf(out,"%s",tag);
    for(unsigned i=0;i<bytes;i+=sizeof(unsigned)){
        const char *end=strchr(names,' ');unsigned n=end ? (unsigned)(end-names):(unsigned)strlen(names),value;
        memcpy(&value,p+i,sizeof value);fprintf(out," %.*s=%u",(int)n,names,value);names=end ? end+1:"?";
    }fputc('\n',out);
}
#define SPIKE_WORDS(out,tag,names,obj) spike_words(out,tag,names,&(obj),sizeof(obj))
static void spike_write_detail(FILE *out,const SpikeDetail *d)
{
    fprintf(out,"CONTEXT frame=%u overlapping_frame=%u mission=%s record=%d stage=%d zone=%d set=%d scene=%d phase=%d tick=%u buttons=%x cull=%d wire=%d vsync=%d npcs=%d vehicles=%d\n",d->frame.frame,d->frame.overlap_frame,d->mission,d->frame.mission,d->frame.stage,d->frame.zone,d->set,d->scene,d->phase,d->tick,d->buttons,d->cull,d->wire,d->vsync,d->npcs,d->vehicles);
    fprintf(out,"INPUT submitted_buttons=%x overlap_buttons=%x latest_buttons=%x running=%d suspended=%d home_excluded=1\n",d->buttons,d->overlap_buttons,d->latest_buttons,d->context_running,d->context_suspended);
    fprintf(out,"CAMERA eye=%.9g,%.9g,%.9g forward=%.9g,%.9g,%.9g yaw=%.9g pitch=%.9g player=%.9g,%.9g,%.9g\n",d->frame.eye[0],d->frame.eye[1],d->frame.eye[2],d->forward[0],d->forward[1],d->forward[2],atan2f(d->forward[0],d->forward[2]),asinf(fmaxf(-1.f,fminf(1.f,d->forward[1]))),d->frame.player[0],d->frame.player[1],d->frame.player[2]);
    const float *matrices[]={d->view,d->proj,d->clip};const char *names[]={"VIEW","PROJECTION","CLIP"};
    for(int i=0;i<3;i++){fprintf(out,"%s",names[i]);for(int j=0;j<16;j++)fprintf(out," %.9g",matrices[i][j]);fputc('\n',out);}
    fprintf(out,"TIMING present_us=%u span_us=%u displayed_logic_us=%u displayed_submit_us=%u ge_wait_us=%u vblank_us=%u overlap_logic_us=%u list_bytes=%u list_capacity=%u\n",d->timing.present_us,d->timing.span_us,d->timing.logic_us,d->timing.submit_us,d->timing.ge_us,d->timing.vblank_us,d->timing.overlap_logic_us,d->timing.list_bytes,d->timing.list_capacity);
    fprintf(out,"DIAGNOSTIC submit_snapshot_us=%u disabled_actor_timers_remain_disabled=1\n",d->snapshot_us);
    SPIKE_WORDS(out,"DRAW_US","other sky city props actors effects hud tags",d->timing.section_us);
    SPIKE_WORDS(out,"LOGIC_US","other player rivals npcs mission cans gangs",d->timing.logic_section_us);
    SPIKE_WORDS(out,"OVERLAP_LOGIC_US","other player rivals npcs mission cans gangs",d->timing.overlap_section_us);
    fprintf(out,"LOGGER displayed_work_us=%u displayed_bytes=%u displayed_calls=%u profile_format_and_write_us=%u interval_io_us=%u interval_bytes=%u interval_calls=%u overlap_profile_us=%u overlap_log_us=%u write_errors=%u\n",d->frame.log_us,d->log_bytes,d->log_calls,d->profile_us,spike_log_us-spike_present_log,spike_log_bytes-spike_present_bytes,spike_log_calls-spike_present_calls,spike_profile_us-spike_profile_start,spike_log_us-spike_log_start,spike_log_errors);
    fprintf(out,"RAM_PROFILER displayed_collect_us=%u overlap_collect_us=%u\n",d->ram_profile_us,runtime_profile_collect_us-spike_ram_start);
    static const char stage_names[]="fast_triangles tested_triangles rejected_triangles clipped_triangles clipped_vertices draw_calls tested_batches rejected_batches tested_groups accepted_groups rejected_groups classified_vertices scalar_vertices reduced_group_masks cached_calls sorted_batches material_calls joined_draws joined_index_bytes index_cache_hits visibility_us box_rejected occlusion_tests occlusion_rejected occluders visibility_frames occlusion_searches selection_us selection_frames sector_rejected range_rejected near_selected far_selected prop_candidates prop_sector_rejected prop_range_rejected prop_selected prop_prepared prop_prepare_hits prop_empty_passes prop_instance_draws mesh_bound_tests mesh_coarse_decisions occlusion_batch_tests occlusion_batches occlusion_batch_triangles clip_reject_tests clip_reject_hits city_list_objects city_object_visits city_empty_passes";
    SPIKE_WORDS(out,"STAGE",stage_names,d->stage);
    const CityTiming *city=&d->city.timing;
    unsigned phase_sum=0;for(unsigned i=0;i<CITY_PHASE_COUNT;i++)phase_sum+=city->phase_us[i];
    fprintf(out,"CITY_TIMING enabled=%u prepare_spans=%u draw_spans=%u prepare_us=%u draw_us=%u phase_sum_us=%u clock_reads=%u errors=%u calibration_steps=%u calibration_us=%u\n",
        city->enabled,city->prepare_spans,city->draw_spans,city->prepare_us,city->draw_us,phase_sum,city->clock_reads,city->errors,d->city.calibration_steps,d->city.calibration_us);
    SPIKE_WORDS(out,"CITY_PHASE_US",spike_city_names,city->phase_us);
    SPIKE_WORDS(out,"CITY_PHASE_ENTRIES",spike_city_names,city->phase_calls);
    SPIKE_WORDS(out,"CITY_WORK",stage_names,d->city.work);
    fprintf(out,"CITY_DRAWN objects=%u batches=%u triangles=%u texture_switches=%u\n",d->city.drawn_objects,d->city.drawn_batches,d->city.drawn_triangles,d->city.texture_switches);
    fprintf(out,"CITY_NOTE exclusive=1 elapsed_wall_time=1 includes_preemption=1 gpu_execution_time=0 later_props_excluded=1 overhead_subtracted=0 calibration_is_estimate=1 cached_packet_setup_and_call_charged_to_submission=1 geometry_includes_classification_clipping_and_output_copy=1 traversal_includes_pass_bookkeeping=1\n");
    SPIKE_WORDS(out,"COLLISION","ray_queries body_queries ray_us body_us plane_hits plane_misses body_plane_hits body_plane_misses ray_reject_reuse body_reject_reuse source_entries source_patches source_triangles patch_batches ray_query_reuse body_query_reuse ray_reuse_overflow body_reuse_overflow candidate_hits candidate_misses candidate_overflows grid_publishes grid_rebuilds grid_reused_rects grid_changed_rects grid_fallbacks grid_bytes grid_bounds_us grid_copy_us grid_update_us grid_indexed_publishes grid_indexed_bytes grid_unchanged_triangles grid_unchanged_vertices grid_unchanged_cells grid_cell_code_same grid_cell_rect_skips grid_rect_evaluated grid_dirty_publishes grid_dirty_visited grid_dirty_skipped grid_dirty_builds grid_dirty_fallbacks grid_dirty_bytes candidate_overflow_hits",d->collision);
    SPIKE_WORDS(out,"PLAYER","ticks total_us animation_us body_us ground_us audio_us camera_us effects_us body_prepared_bones body_reused_bones",d->player);
    SPIKE_WORDS(out,"TRAFFIC_LOGIC","ticks objects root_us body_us surface_us grip_us total_us body_prepared",d->traffic);
    SPIKE_WORDS(out,"TRAFFIC_DRAW","frames candidates source_rejected frustum_rejected close_kept drawn near_drawn far_drawn posed_bones triangles us sample_prepares bounds_prepares state_batches",d->traffic_draw);
    for(int i=0;i<CHAR_WORK_COUNT;i++){fprintf(out,"ACTOR_CLASS id=%d timing_enabled=%d\n",i,chars_actor_timing_enabled());SPIKE_WORDS(out,"ACTOR_WORK","calls total_us pose_us bounds_us packet_us skin_us bones bone_uploads skin_draws skin_vertices cpu_skin_us culled",d->actors[i]);}
}
static int spike_check(Level *l,unsigned frame,int eligible,unsigned overlap_buttons)
{
    if(!JGR_DIAGNOSTICS || !spike_enabled)return 0;
    GfxFrameSample sample;if(!gfx_capture_latest(&sample) || sample.serial==spike_seen_serial)return 0;
    spike_seen_serial=sample.serial;
    SpikeDetail *d=&spike_pending;
    /* HOME may become visible while next-frame logic or the natural GE
       fence runs. Recheck after the fence, as well as both frame inputs.
       A system dialog/exit must break arming, not become a gameplay hitch. */
    SceCtrlData latest={0};int input_ready=sceCtrlPeekBufferPositive(&latest,1)>0;
    d->overlap_buttons=overlap_buttons;d->latest_buttons=latest.Buttons;
    d->context_running=running;d->context_suspended=power_sleeping || power_resumed;
    int valid=eligible && input_ready && d->frame.eligible &&
        spike_capture_context_valid(d->context_running,d->context_suspended,d->buttons,overlap_buttons,latest.Buttons) &&
        d->frame.generation==spike_generation && d->timing.serial==sample.serial;
    d->frame.eligible=valid;d->frame.overlap_frame=frame;d->frame.present_us=sample.present_us;
    d->frame.ge_us=sample.ge_us;d->frame.vblank_us=sample.vblank_us;d->timing=sample;
    int fire=spike_capture_observe(&spike_capture,&d->frame);
    if(!fire){spike_present_log=spike_log_us;spike_present_bytes=spike_log_bytes;spike_present_calls=spike_log_calls;return 0;}
    col_profile_totals(&spike_overlap_collision);
    spike_delta(&spike_overlap_collision,&spike_before.collision,sizeof spike_overlap_collision);
    player_profile_totals(&spike_overlap_player);spike_delta(&spike_overlap_player,&spike_before.player,sizeof spike_overlap_player);
    pathworld_profile_totals(&spike_overlap_traffic);spike_delta(&spike_overlap_traffic,&spike_before.traffic,sizeof spike_overlap_traffic);
    /* Trigger is latched and all measured fields fixed before any capture I/O. */
    audio_pause_music(1);hang_mark(HANG_DIAGNOSTIC);
    char report[48],shot[48];SceIoStat stat;unsigned slot;
    for(slot=1;slot<1000;slot++){snprintf(report,sizeof report,"jgr_spike_%03u.txt",slot);snprintf(shot,sizeof shot,"jgr_spike_%03u.raw",slot);if(sceIoGetstat(report,&stat)<0 && sceIoGetstat(shot,&stat)<0)break;}
    int shot_result=slot<1000 ? render_bench_dump(shot):-1;
    FILE *out=slot<1000 ? fopen(report,"wb"):NULL;int ok=out!=NULL;
    if(out){setvbuf(out,spike_text_buffer,_IOFBF,sizeof spike_text_buffer);
        fprintf(out,"JGR_SPIKE version=1 build=%s threshold_us=%u warmup=%u history=%u synthetic=%u\n",crash_build_id(),SPIKE_CAPTURE_THRESHOLD_US,SPIKE_CAPTURE_WARMUP,spike_capture.count,spike_fixture);
        fprintf(out,"NOTES raw_swap_interval_includes_logger_and_audio_preemption=1 ge_wait_is_not_gpu_busy=1 per_actor_times_unavailable_unless_enabled=1 current_logic_already_advanced=1 city_masks_are_submission_eligibility=1\n");
        fprintf(out,"SCREENSHOT path=%s result=%d width=480 height=272 stride=512 format=RGBA8888\n",shot,shot_result);
        struct mallinfo mi=mallinfo();fprintf(out,"MEMORY heap_free=%u arena=%u used=%u kernel_free=%u kernel_largest=%u cpu_mhz=%d bus_mhz=%d\n",(unsigned)mi.fordblks,(unsigned)mi.arena,(unsigned)mi.uordblks,sceKernelTotalFreeMemSize(),sceKernelMaxFreeMemSize(),scePowerGetCpuClockFrequencyInt(),scePowerGetBusClockFrequencyInt());
        spike_write_detail(out,d);
        fprintf(out,"OVERLAP_COLLISION ray_queries=%u body_queries=%u ray_us=%u body_us=%u source_triangles=%u patch_batches=%u source_entries=%u candidate_hits=%u candidate_misses=%u candidate_overflows=%u candidate_overflow_hits=%u\n",spike_overlap_collision.ray_queries,spike_overlap_collision.body_queries,spike_overlap_collision.ray_us,spike_overlap_collision.body_us,spike_overlap_collision.source_triangles,spike_overlap_collision.patch_batches,spike_overlap_collision.source_entries,spike_overlap_collision.candidate_hits,spike_overlap_collision.candidate_misses,spike_overlap_collision.candidate_overflows,spike_overlap_collision.candidate_overflow_hits);
        SPIKE_WORDS(out,"OVERLAP_PLAYER","ticks total_us animation_us body_us ground_us audio_us camera_us effects_us body_prepared_bones body_reused_bones",spike_overlap_player);
        SPIKE_WORDS(out,"OVERLAP_TRAFFIC","ticks objects root_us body_us surface_us grip_us total_us body_prepared",spike_overlap_traffic);
        SPIKE_WORDS(out,"PEDESTRIAN_OCCLUSION","frames enabled_frames candidates tested hidden kept prepare_skipped draw_skipped query_us occluders late_tested late_hidden aggressive_frames rays ray_blocked ray_uncertain ray_unavailable ray_triangles ray_steps proxy_frames proxy_tests proxy_blocked",d->peds);
        for(unsigned i=0;i<spike_capture.count;i++){const SpikeFrame *f=spike_capture_history(&spike_capture,i);
            fprintf(out,"HISTORY frame=%u overlap_frame=%u present_us=%u logic_us=%u submit_us=%u ge_us=%u vblank_us=%u log_us=%u list=%u stage_draws=%u stage_tris=%u stage=%d mission=%d zone=%d player=%.9g,%.9g,%.9g eye=%.9g,%.9g,%.9g\n",f->frame,f->overlap_frame,f->present_us,f->logic_us,f->submit_us,f->ge_us,f->vblank_us,f->log_us,f->list_bytes,f->stage_draws,f->stage_tris,f->stage,f->mission,f->zone,f->player[0],f->player[1],f->player[2],f->eye[0],f->eye[1],f->eye[2]);
            fprintf(out,"HISTORY_CITY frame=%u clock_reads=%u",f->frame,f->city_clock_reads);
            SPIKE_WORDS(out,"",spike_city_names,f->city_us);}
        stage_dump_frame(&l->st,out);
        fprintf(out,"LIVE_AFTER_OVERLAP frame=%u tick=%u player=%.9g,%.9g,%.9g not_displayed_actor_state=1\n",frame,l->pl.logic_tick,l->pl.x,l->pl.y,l->pl.z);
        for(int i=0;i<l->npcs.count;i++){const Ped *p=l->npcs.p+i;if(p->active)fprintf(out,"LIVE_PED id=%d type=%d model=%d list=%d xyz=%.9g,%.9g,%.9g yaw=%.9g motion=%d animation_frame=%.9g visible=%d near=%d opacity=%.9g\n",i,p->type,p->pmodel,p->list,p->x,p->y,p->z,p->yaw,p->anim,p->frame,p->visible,p->near,p->opacity);}
        AudioMeProfile me;MusicProfile music;StreamProfile stream;audio_me_profile_read(&me);audio_music_profile(&music);stream_profile(&stream);
        fprintf(out,"AUDIO_POST_TRIGGER enabled=%d jobs=%u timeouts=%u fallbacks=%u quarantined=%u underruns=%u io_error=%d thread_error=%d open_failures=%u\n",me.enabled,me.jobs,me.timeouts,me.fallbacks,me.quarantined,music.underruns,stream.io_error,stream.thread_error,stream.open_failures);
        fprintf(out,"STREAM_POST_TRIGGER active=%d suspended=%d buffers=%u read_us=%u decode_us=%u max_read_us=%u max_decode_us=%u queued_frames=%u reader_priority=%u\n",stream.active,stream.suspended,stream.published_buffers,stream.read_us,stream.decode_us,stream.max_read_us,stream.max_decode_us,stream.queued_frames,stream.reader_priority);
        fprintf(out,"END complete=1\n");if(fflush(out) || ferror(out))ok=0;if(fclose(out))ok=0;
    }
    if(shot_result!=512*272*4)ok=0;
    int sync_result=sceIoSync("ms0:",0);if(sync_result<0)ok=0;
    runtime_profile_dump("spike");
    runtime_log("SPIKE_CAPTURE saved=%d frame=%u overlap_frame=%u present_us=%u report=%s screenshot=%s sync_result=%d\n",ok,d->frame.frame,frame,sample.present_us,report,shot,sync_result);runtime_log_flush();
    sceIoSync("ms0:",0);
    /* Evidence is saved before this status screen consumes renderer masks. */
    gfx_begin();gfx_clear(0xff000000);gfx_mode2d();
    font_draw(14,70,2,0xffffffff,ok ? "20FPS FRAME CAPTURED":"CAPTURE WRITE FAILED");
    font_draw(14,110,1,0xffffffff,report);font_draw(14,140,1,0xffffffff,"GAMEPLAY FROZEN. HOME TO QUIT.");
    font_draw(14,160,1,0xffffffff,"THEN CONNECT USB FOR ANALYSIS.");gfx_end_vsync(0);gfx_wait_idle();
    unsigned held=sceKernelGetSystemTimeLow();
    while(running){hang_mark(HANG_DIAGNOSTIC);sceKernelDelayThreadCB(20000);
        if(spike_fixture && sceKernelGetSystemTimeLow()-held>=9000000u)exit_callback(0,0,NULL);}
    return 1;
}
#undef SPIKE_WORDS
