#ifndef JGR_RENDER_BENCH_H
#define JGR_RENDER_BENCH_H
#include "gfx.h"
#include "charanim.h"
#include "npc_occlusion.h"
#include "collide.h"
#include <string.h>
/* Same-view ABBA, with an equal warmup after EVERY mode switch. These
   are render submission timings, never game FPS or simulation timings. */
#define RENDER_BENCH_WARMUP 8u
#define RENDER_BENCH_SAMPLES 32u
enum { RENDER_BENCH_POSE_SPAN=4, RENDER_BENCH_PED_PACKETS=5, RENDER_BENCH_PED_SHARED=6,
       RENDER_BENCH_PED_NATIVE=7, RENDER_BENCH_PED_TRANSFORM_SCALAR=8,
       RENDER_BENCH_PED_TRANSFORM_VFPU=9, RENDER_BENCH_PED_EMIT=10, RENDER_BENCH_PED_OVERHEAD=11,
       RENDER_BENCH_PED_OCCLUSION=12, RENDER_BENCH_PED_OCCLUSION_AGGRESSIVE=13,
       RENDER_BENCH_PED_OCCLUSION_PROXIES=14 };
static inline const char *render_bench_feature(unsigned detail)
{
    static const char *const names[]={"arrows","npc_detail","scenery","occlusion","pose_span",
        "ped_packets","ped_shared","ped_native","ped_transform_scalar","ped_transform_vfpu","ped_emit","ped_overhead","ped_occlusion","ped_occlusion_aggressive","ped_occlusion_proxies"};
    return detail<=RENDER_BENCH_PED_OCCLUSION_PROXIES ? names[detail]:"unknown";
}
typedef struct {
    unsigned samples,submit_us,ge_us,wall_us,publications,writeback_us,hash;
    unsigned actor_timing_frames,ped_math_frames;
    unsigned group_vertices,classified_vertices,tested_groups,tested_triangles;
    unsigned stage_draws,joined_draws,joined_bytes,hidden_objects,index_cache_hits;
    CharArrowProfile arrows;
    CharLodProfile detail;
    CharWorkStats actors[CHAR_WORK_COUNT];
    CharPedPacketProfile ped_packets;
    CharPedSharedProfile ped_shared;
    NpcOcclusionProfile ped_occlusion;
    unsigned sections[GFX_SECTIONS],times[RENDER_BENCH_SAMPLES];
} RenderBenchResult;
typedef struct {
    unsigned active,phase,frame,tick,run,target,detail;
    RenderBenchResult result[4];
} RenderBench;
static inline void render_bench_begin(RenderBench *b,unsigned tick)
{unsigned run=b->run+1;memset(b,0,sizeof *b);b->active=1;b->tick=tick;b->run=run;}
static inline int render_bench_mode(const RenderBench *b)
{return b->phase==1 || b->phase==2;}
static inline int render_bench_is_ped_occlusion(const RenderBench *b)
{return b->detail==RENDER_BENCH_PED_OCCLUSION || b->detail==RENDER_BENCH_PED_OCCLUSION_AGGRESSIVE ||
    b->detail==RENDER_BENCH_PED_OCCLUSION_PROXIES;}
static inline int render_bench_native_mode(const RenderBench *b)
{return b->detail==RENDER_BENCH_PED_EMIT || b->detail==RENDER_BENCH_PED_OVERHEAD || render_bench_is_ped_occlusion(b) ||
    (b->detail==RENDER_BENCH_PED_NATIVE && render_bench_mode(b));}
static inline int render_bench_emit_mode(const RenderBench *b)
{return b->detail!=RENDER_BENCH_PED_EMIT || render_bench_mode(b);}
static inline int render_bench_actor_timing(const RenderBench *b)
{return !render_bench_is_ped_occlusion(b) && (b->detail!=RENDER_BENCH_PED_OVERHEAD || !render_bench_mode(b));}
static inline int render_bench_ped_math(const RenderBench *b)
{return render_bench_is_ped_occlusion(b) || (b->detail==RENDER_BENCH_PED_OVERHEAD && render_bench_mode(b));}
static inline int render_bench_ped_occlusion(const RenderBench *b)
{return render_bench_mode(b) ? (b->detail==RENDER_BENCH_PED_OCCLUSION_PROXIES ? NPC_OCCLUSION_PROXIES:
    b->detail==RENDER_BENCH_PED_OCCLUSION_AGGRESSIVE ? NPC_OCCLUSION_AGGRESSIVE:b->detail==RENDER_BENCH_PED_OCCLUSION):NPC_OCCLUSION_OFF;}
static inline int render_bench_last(const RenderBench *b)
{return b->frame+1==RENDER_BENCH_WARMUP+RENDER_BENCH_SAMPLES;}
static inline void render_bench_arrows(RenderBenchResult *r,const CharArrowProfile *before,const CharArrowProfile *after)
{
#define ARROW_ACC(field) r->arrows.field+=after->field-before->field
    ARROW_ACC(scopes);ARROW_ACC(requests);ARROW_ACC(drawn);ARROW_ACC(culled);ARROW_ACC(reference);
    ARROW_ACC(pose_built);ARROW_ACC(pose_reused);ARROW_ACC(draw_calls);ARROW_ACC(total_us);
#undef ARROW_ACC
}
static inline void render_bench_detail(RenderBenchResult *r,const CharLodProfile *before,const CharLodProfile *after)
{
#define DETAIL_ACC(field) r->detail.field+=after->field-before->field
    DETAIL_ACC(actors);DETAIL_ACC(texture_actors);DETAIL_ACC(hull_actors);DETAIL_ACC(hull_triangles);
    DETAIL_ACC(texture_binds);DETAIL_ACC(source_texels);DETAIL_ACC(lod_texels);DETAIL_ACC(texture_fallbacks);
#undef DETAIL_ACC
}
static inline void render_bench_actors(RenderBenchResult *r,const CharWorkStats *before,const CharWorkStats *after)
{
    for(unsigned i=0;i<CHAR_WORK_COUNT;i++) {
#define ACTOR_ACC(field) r->actors[i].field+=after[i].field-before[i].field
        ACTOR_ACC(calls);ACTOR_ACC(total_us);ACTOR_ACC(pose_us);ACTOR_ACC(bounds_us);
        ACTOR_ACC(packet_us);ACTOR_ACC(skin_us);ACTOR_ACC(cpu_skin_us);ACTOR_ACC(bones);
        ACTOR_ACC(bone_uploads);ACTOR_ACC(skin_draws);ACTOR_ACC(skin_vertices);ACTOR_ACC(culled);
#undef ACTOR_ACC
    }
}
static inline void render_bench_ped_packets(RenderBenchResult *r,const CharPedPacketProfile *before,const CharPedPacketProfile *after)
{
#define PED_PACKET_ACC(field) r->ped_packets.field+=after->field-before->field
    PED_PACKET_ACC(draws);PED_PACKET_ACC(strip_draws);PED_PACKET_ACC(source_vertices);PED_PACKET_ACC(vertices);
    PED_PACKET_ACC(source_indices);PED_PACKET_ACC(indices);PED_PACKET_ACC(triangles);
    PED_PACKET_ACC(built);PED_PACKET_ACC(reused);PED_PACKET_ACC(fallbacks);
#undef PED_PACKET_ACC
}
static inline void render_bench_ped_shared(RenderBenchResult *r,const CharPedSharedProfile *before,const CharPedSharedProfile *after)
{
#define PED_SHARED_ACC(field) r->ped_shared.field+=after->field-before->field
    PED_SHARED_ACC(actors);PED_SHARED_ACC(fallback_actors);PED_SHARED_ACC(source_vertices);PED_SHARED_ACC(unique_positions);
    PED_SHARED_ACC(draws);PED_SHARED_ACC(triangles);PED_SHARED_ACC(indices);PED_SHARED_ACC(vertex_bytes);
    PED_SHARED_ACC(built);PED_SHARED_ACC(reused);PED_SHARED_ACC(transform_us);PED_SHARED_ACC(gather_us);PED_SHARED_ACC(submit_us);
#undef PED_SHARED_ACC
}
static inline void render_bench_ped_occlusion_counts(RenderBenchResult *r,const NpcOcclusionProfile *before,const NpcOcclusionProfile *after)
{
#define PED_OCCLUSION_ACC(field) r->ped_occlusion.field+=after->field-before->field
    PED_OCCLUSION_ACC(frames);PED_OCCLUSION_ACC(enabled_frames);PED_OCCLUSION_ACC(candidates);
    PED_OCCLUSION_ACC(tested);PED_OCCLUSION_ACC(hidden);PED_OCCLUSION_ACC(kept);
    PED_OCCLUSION_ACC(prepare_skipped);PED_OCCLUSION_ACC(draw_skipped);PED_OCCLUSION_ACC(query_us);
    PED_OCCLUSION_ACC(occluders);
    PED_OCCLUSION_ACC(late_tested);PED_OCCLUSION_ACC(late_hidden);
    PED_OCCLUSION_ACC(aggressive_frames);PED_OCCLUSION_ACC(rays);PED_OCCLUSION_ACC(ray_blocked);
    PED_OCCLUSION_ACC(ray_uncertain);PED_OCCLUSION_ACC(ray_unavailable);
    PED_OCCLUSION_ACC(ray_triangles);PED_OCCLUSION_ACC(ray_steps);
    PED_OCCLUSION_ACC(proxy_frames);PED_OCCLUSION_ACC(proxy_tests);PED_OCCLUSION_ACC(proxy_blocked);
#undef PED_OCCLUSION_ACC
}
static inline void render_bench_sample(RenderBench *b,const GfxProfile *before,const GfxProfile *after,
    const unsigned wb[3],const unsigned wa[3],unsigned wall,unsigned hash)
{
    RenderBenchResult *r=b->result+b->phase;
    if(b->frame>=RENDER_BENCH_WARMUP) {
        unsigned elapsed=after->submit_us-before->submit_us;
        r->times[r->samples++]=elapsed;r->submit_us+=elapsed;
        r->ge_us+=after->ge_us-before->ge_us;r->wall_us+=wall;
        r->publications+=wa[0]-wb[0];r->writeback_us+=wa[1]-wb[1];
        for(unsigned i=0;i<GFX_SECTIONS;i++)r->sections[i]+=after->section_us[i]-before->section_us[i];
    }
    if(render_bench_last(b)){r->hash=hash;b->frame=0;if(++b->phase==4)b->active=0;}
    else b->frame++;
}
static inline unsigned render_bench_p95(const RenderBenchResult *r)
{
    unsigned sorted[RENDER_BENCH_SAMPLES];memcpy(sorted,r->times,sizeof sorted);
    for(unsigned i=1;i<r->samples;i++){unsigned v=sorted[i],j=i;while(j && sorted[j-1]>v){sorted[j]=sorted[j-1];j--;}sorted[j]=v;}
    return r->samples ? sorted[(r->samples*95+99)/100-1]:0;
}
static inline int render_bench_valid(const RenderBench *b,unsigned tick)
{
    if(b->active || b->phase!=4 || tick!=b->tick || !b->result[0].hash)return 0;
    /* Visual LOD and aggressive culling may differ between A and B. Require each mode
       to reproduce itself, and still reject any simulation tick change. */
    for(unsigned i=0;i<4;i++)if(b->result[i].samples!=RENDER_BENCH_SAMPLES ||
        !b->result[i].hash || b->result[i].hash!=b->result[(b->detail==1 || b->detail==RENDER_BENCH_PED_OCCLUSION_AGGRESSIVE ||
            b->detail==RENDER_BENCH_PED_OCCLUSION_PROXIES) && (i==1 || i==2) ? 1:0].hash)return 0;
    if(b->detail==RENDER_BENCH_PED_EMIT || b->detail==RENDER_BENCH_PED_OVERHEAD)for(unsigned i=0;i<4;i++) {
        const RenderBenchResult *r=b->result+i,*a=b->result;
        if(b->detail==RENDER_BENCH_PED_OVERHEAD &&
           (r->actor_timing_frames!=((i==0 || i==3) ? r->samples:0) ||
            r->ped_math_frames!=((i==1 || i==2) ? r->samples:0)))return 0;
        if(!r->ped_shared.actors || !r->ped_shared.draws || !r->ped_shared.unique_positions ||
           !r->ped_shared.vertex_bytes)return 0;
#define SAME_NATIVE(field) if(r->ped_shared.field!=a->ped_shared.field)return 0
        SAME_NATIVE(actors);SAME_NATIVE(fallback_actors);SAME_NATIVE(source_vertices);SAME_NATIVE(unique_positions);
        SAME_NATIVE(draws);SAME_NATIVE(triangles);SAME_NATIVE(indices);SAME_NATIVE(vertex_bytes);
#undef SAME_NATIVE
#define SAME_SCENE(field) if(r->field!=a->field)return 0
        SAME_SCENE(group_vertices);SAME_SCENE(classified_vertices);SAME_SCENE(tested_groups);SAME_SCENE(tested_triangles);
#undef SAME_SCENE
        for(unsigned j=0;j<CHAR_WORK_COUNT;j++) {
#define SAME_ACTOR(field) if(r->actors[j].field!=a->actors[j].field)return 0
            SAME_ACTOR(calls);SAME_ACTOR(bones);SAME_ACTOR(bone_uploads);SAME_ACTOR(skin_draws);SAME_ACTOR(skin_vertices);SAME_ACTOR(culled);
#undef SAME_ACTOR
            if(b->detail==RENDER_BENCH_PED_OVERHEAD && (i==1 || i==2) &&
               (r->actors[j].total_us || r->actors[j].pose_us || r->actors[j].bounds_us ||
                r->actors[j].packet_us || r->actors[j].skin_us || r->actors[j].cpu_skin_us))return 0;
        }
        if(b->detail==RENDER_BENCH_PED_OVERHEAD && (i==1 || i==2) &&
           (r->ped_shared.transform_us || r->ped_shared.gather_us || r->ped_shared.submit_us))return 0;
    }
    if(render_bench_is_ped_occlusion(b))for(unsigned i=0;i<4;i++) {
        unsigned optimized=i==1 || i==2;
        const RenderBenchResult *r=b->result+i,*a=b->result,*same=b->result+optimized;
        const NpcOcclusionProfile *o=&r->ped_occlusion;
        if(r->actor_timing_frames || r->ped_math_frames!=r->samples || o->frames!=r->samples ||
           o->enabled_frames!=(optimized ? r->samples:0) || o->hidden>o->tested ||
           o->tested>o->candidates || o->kept!=o->candidates-o->hidden ||
           o->prepare_skipped!=o->hidden || o->draw_skipped!=o->hidden ||
           o->late_hidden>o->late_tested || o->late_tested>o->kept ||
           (!optimized && (o->tested || o->hidden || o->late_tested || o->late_hidden)) ||
           o->candidates!=a->ped_occlusion.candidates)return 0;
#define SAME_OCCLUSION(field) if(o->field!=same->ped_occlusion.field)return 0
        SAME_OCCLUSION(candidates);SAME_OCCLUSION(tested);SAME_OCCLUSION(hidden);SAME_OCCLUSION(kept);
        SAME_OCCLUSION(prepare_skipped);SAME_OCCLUSION(draw_skipped);
        SAME_OCCLUSION(late_tested);SAME_OCCLUSION(late_hidden);
        SAME_OCCLUSION(rays);SAME_OCCLUSION(ray_blocked);SAME_OCCLUSION(ray_uncertain);
        SAME_OCCLUSION(ray_unavailable);SAME_OCCLUSION(ray_triangles);SAME_OCCLUSION(ray_steps);
        SAME_OCCLUSION(proxy_tests);SAME_OCCLUSION(proxy_blocked);
#undef SAME_OCCLUSION
        if(b->detail==RENDER_BENCH_PED_OCCLUSION_AGGRESSIVE) {
            if(o->aggressive_frames!=(optimized ? r->samples:0) || o->rays!=o->tested ||
               o->hidden!=(unsigned long long)o->ray_blocked+o->ray_uncertain ||
               (unsigned long long)o->hidden+o->ray_unavailable>o->rays ||
               o->ray_triangles>(unsigned long long)o->rays*COL_RENDER_MAX_TRIANGLES ||
               o->ray_steps>(unsigned long long)o->rays*COL_RENDER_MAX_STEPS ||
               o->late_tested || o->late_hidden)return 0;
        } else if(o->aggressive_frames || o->rays || o->ray_blocked || o->ray_uncertain ||
                  o->ray_unavailable || o->ray_triangles || o->ray_steps)return 0;
        if(b->detail==RENDER_BENCH_PED_OCCLUSION_PROXIES) {
            if(o->proxy_frames!=(optimized ? r->samples:0) || o->hidden!=o->proxy_blocked ||
               o->proxy_blocked>o->proxy_tests || o->proxy_tests>(unsigned long long)o->candidates*NPC_OCCLUSION_MAX_PROXIES ||
               (!optimized && o->proxy_tests) || o->late_tested || o->late_hidden)return 0;
        } else if(o->proxy_frames || o->proxy_tests || o->proxy_blocked)return 0;
        if(!(o->hidden || o->late_hidden) && r->hash!=a->hash)return 0;
        if(r->ped_shared.fallback_actors || r->ped_shared.unique_positions>r->ped_shared.source_vertices ||
           r->ped_shared.actors>r->actors[CHAR_WORK_PEDESTRIANS].calls ||
           (!optimized && (!r->ped_shared.actors || !r->ped_shared.draws ||
                            !r->ped_shared.unique_positions || !r->ped_shared.vertex_bytes)))return 0;
#define LESS_NATIVE(field) if(r->ped_shared.field>a->ped_shared.field || r->ped_shared.field!=same->ped_shared.field || \
            (!(o->hidden || o->late_hidden) && r->ped_shared.field!=a->ped_shared.field))return 0
        LESS_NATIVE(actors);LESS_NATIVE(source_vertices);LESS_NATIVE(unique_positions);
        LESS_NATIVE(draws);LESS_NATIVE(triangles);LESS_NATIVE(indices);LESS_NATIVE(vertex_bytes);
#undef LESS_NATIVE
#define SAME_SCENE(field) if(r->field!=a->field)return 0
        SAME_SCENE(classified_vertices);SAME_SCENE(tested_groups);SAME_SCENE(tested_triangles);
#undef SAME_SCENE
        if(r->group_vertices>a->group_vertices || r->group_vertices!=same->group_vertices ||
           (!(o->hidden || o->late_hidden) && r->group_vertices!=a->group_vertices))return 0;
        for(unsigned j=0;j<CHAR_WORK_COUNT;j++) {
#define OCCLUSION_ACTOR(field) if(r->actors[j].field!=same->actors[j].field || \
            (j==CHAR_WORK_PEDESTRIANS && (o->hidden || o->late_hidden) ? r->actors[j].field>a->actors[j].field:r->actors[j].field!=a->actors[j].field))return 0
            OCCLUSION_ACTOR(calls);OCCLUSION_ACTOR(bones);OCCLUSION_ACTOR(bone_uploads);
            OCCLUSION_ACTOR(skin_draws);OCCLUSION_ACTOR(skin_vertices);
#undef OCCLUSION_ACTOR
            if(r->actors[j].culled!=same->actors[j].culled)return 0;
            if(j==CHAR_WORK_PEDESTRIANS) {
                unsigned base=a->actors[j].culled,culled=r->actors[j].culled;
                if((culled>base && culled-base>o->late_hidden) || culled<o->late_hidden ||
                   culled-o->late_hidden<(base>o->hidden ? base-o->hidden:0))return 0;
            } else if(r->actors[j].culled!=a->actors[j].culled)return 0;
            if(r->actors[j].total_us || r->actors[j].pose_us || r->actors[j].bounds_us ||
               r->actors[j].packet_us || r->actors[j].skin_us || r->actors[j].cpu_skin_us)return 0;
        }
        if(a->actors[CHAR_WORK_PEDESTRIANS].calls-r->actors[CHAR_WORK_PEDESTRIANS].calls!=o->hidden ||
           r->ped_shared.transform_us || r->ped_shared.gather_us || r->ped_shared.submit_us)return 0;
        unsigned native_skipped=a->ped_shared.actors-r->ped_shared.actors;
        if(native_skipped<o->late_hidden || native_skipped-o->late_hidden>o->hidden)return 0;
    }
    return 1;
}
#endif
