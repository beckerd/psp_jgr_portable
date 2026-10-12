#ifndef CHARANIM_H
#define CHARANIM_H
#include "diagnostics.h"
#include "player_light.h"
/* JGC1: legacy single-bone batches. JGC2: owner-local vertices permit Ninja
   cache triangles to span adjacent bones (see decompile/tools/jgc_convert.py). */
typedef struct { char magic[4]; unsigned n_chars, n_bones, n_batches, n_verts, n_idx, n_tex, n_motions;
                 unsigned off_chars, off_bones, off_batches, off_verts, off_idx, off_tex, off_motions, off_keys, off_pix; } JgcHeader;
typedef struct { unsigned bone0, nbones, motion0, nmotions; float x, y, z, yaw; char name[16]; } JgcChar;
typedef struct { short parent; unsigned short ev; float pos[3]; int ang[3]; float scl[3]; unsigned batch0, nbatch; } JgcBone;
typedef struct { unsigned nframes, chan0; } JgcMotion;
typedef struct { unsigned npos, off_pos, nang, off_ang, nscl, off_scl; } JgcChan;
typedef struct { unsigned short w, h, flags, pad; unsigned off_clut, off_pix; } JgcTex;
typedef struct { unsigned vert_off, idx_off; unsigned short n_vert, n_idx, tex, flags; } JgcBatch;

typedef struct CharGeometryPage CharGeometryPage;
typedef struct {unsigned char *vertices;unsigned short *indices;} CharBatchGeometry;
typedef struct Chars {
    unsigned char *base; JgcHeader *h; JgcChar *chars; JgcBone *bones; JgcBatch *batch; JgcTex *tex;
    JgcMotion *motions; unsigned short *idx; void *verts;
    int vert_stride;  /* JGC1=24, JGC2=28, JGC3=40 (original local normals) */
    float frame;      /* animation clock (30 fps frames) */
    int motion_sel;   /* motion index offset applied to every character */
    int drawn_tris;
    unsigned char *lighting_image;const PlayerLightVertex *lighting;
    const struct Chars *motion_owner; /* borrowed resident bank, never freed here */
    float *motion_radius; /* all native clips, root-local; NULL means pose first */
    unsigned *batch_owner_offsets,batch_owner_bytes; /* optional packed immutable bone membership */
    float *render_radius; /* conservative owner-local bounds, including hulls */
    /* Optional section allocations when a contiguous JGC cannot fit. */
    unsigned char *vertex_data,*index_data,*motion_data,*texture_data;
    /* Selected-cast PLT1 remap; one original vertex start per batch. */
    unsigned *lighting_source,source_vertices,source_geometry_hash,resident_bytes;
    unsigned source_models;
    const unsigned *source_lod;unsigned source_lod_count; /* borrowed immutable JCL1 directory */
    unsigned long long resident_mask;
    CharBatchGeometry *geometry;CharGeometryPage *geometry_pages;
    unsigned char **texture_parts,*texture_metadata;
    void *indexed_prepare; /* optional bounded Final source-reference maps */
} Chars;

/* Original 06dd7c modes: detailed, low, low with fade. A legacy bank or a
   family without an authored pair keeps its exact existing model. */
static inline int chars_source_lod_model(const Chars *c,int model,int mode)
{
    if(c && c->h && mode>=2 && (unsigned)model<c->source_lod_count) {
        unsigned far=c->source_lod[model];
        if(far && far<c->h->n_chars && c->chars[far].nbones)return (int)far;
    }
    return model;
}

static inline unsigned char *chars_data(const Chars *c,unsigned offset)
{
    if(c->texture_data && offset>=c->h->off_pix)return c->texture_data+(offset-c->h->off_pix);
    if(c->motion_data && offset>=c->h->off_motions)return c->motion_data+(offset-c->h->off_motions);
    if(c->index_data && offset>=c->h->off_idx)return c->index_data+(offset-c->h->off_idx);
    if(c->vertex_data && offset>=c->h->off_verts)return c->vertex_data+(offset-c->h->off_verts);
    return c->base+offset;
}

static inline unsigned char *chars_batch_vertices(const Chars *c,const JgcBatch *b)
{return c->geometry ? c->geometry[b-c->batch].vertices:(unsigned char *)c->verts+b->vert_off*c->vert_stride;}
static inline const unsigned short *chars_batch_indices(const Chars *c,const JgcBatch *b)
{return c->geometry ? c->geometry[b-c->batch].indices:c->idx+b->idx_off;}
static inline unsigned char *chars_texture_address(const Chars *c,const JgcTex *t,unsigned offset)
{return c->texture_parts ? c->texture_parts[t-c->tex]+(offset-t->off_clut):chars_data(c,offset);}

int  chars_load(Chars *c, const char *path);
int  chars_load_sections(Chars *c,const char *path);
int  chars_load_roster(Chars *,const char *path,unsigned long long mask);
int  chars_share_motions(Chars *variant,const Chars *owner,int model);
void chars_free(Chars *c);
void chars_update(Chars *c, float dt);
/* Read/reset submission counters: GE batches/vertices, CPU batches/vertices. */
void chars_skin_profile(unsigned out[4]);
void chars_material_profile(unsigned out[2]);
/* Borrow 416 KiB from the existing GU arena for immutable vertex packets.
   A crowded street needs the player, rival, police and traffic packets at
   once. Keep them resident instead of rebuilding them on every camera turn.
   gfx.c deducts these bytes from the list; this does not grow its RAM budget.
   The override permits an identical-content comparison with the old cache. */
#ifndef CHAR_GE_PACKET_BYTES
#define CHAR_GE_PACKET_BYTES (416u*1024u)
#endif
/* Charge the additional page-owner/pin metadata to the same arena too. */
#define CHAR_GE_PACKET_EXTRA_BYTES ((CHAR_GE_PACKET_BYTES-128u*1024u)/256u*3u)
/* Group index packets live in the existing page pool. Charge their extra
   cache metadata to the same fixed GU arena, not to the gameplay heap. */
#define CHAR_GE_GROUP_METADATA_BYTES (512u*16u)
/* Final indexed preparation uses one actor-local CPU cache and compact
   immutable maps. Both reservations are charged against the GU arena. */
#define CHAR_FINAL_INDEXED_WORK_BYTES (13u*1024u)
#define CHAR_FINAL_INDEXED_MAP_BYTES (32u*1024u)
#ifndef JGR_FINAL_FLOOR_BOUNDS
#ifdef __PSP__
#define JGR_FINAL_FLOOR_BOUNDS 1
#else
#define JGR_FINAL_FLOOR_BOUNDS 0
#endif
#endif
#ifndef JGR_FINAL_GE_NORMAL
#ifdef __PSP__
#define JGR_FINAL_GE_NORMAL 1
#else
#define JGR_FINAL_GE_NORMAL 0
#endif
#endif
typedef struct {unsigned batches,vertices,built,reused,rigid_rejected,fallbacks,packet_bytes;} CharFinalGeNormalProfile;
void chars_final_ge_normal_mode(int enabled);
int chars_final_ge_normal_enabled(void);
void chars_final_ge_normal_profile(CharFinalGeNormalProfile *out); /* read/reset; packet_bytes counts builds */
typedef struct {unsigned actors,batches,source_vertices,prepared_vertices,
    reused_vertices,boundary_hits,fallback_batches,map_bytes;} CharFinalIndexedProfile;
int chars_final_indexed_prepare(Chars *c);
void chars_final_indexed_mode(int enabled);
/* All counters read/reset; map_bytes reports current aggregate allocation. */
void chars_final_indexed_profile(CharFinalIndexedProfile *out);
/* Cached CPU workspace replaces part of the existing display-list arena. */
#define CHAR_CPU_SKIN_VERTICES 1024u
#define CHAR_CPU_WORK_BYTES (CHAR_CPU_SKIN_VERTICES*28u+CHAR_CPU_SKIN_VERTICES/8u+512u*(44u+12u)+64u)
#define CHAR_DRAW_PLAN_BYTES (18u*1024u)
#define CHAR_PREPARATION_BYTES (30u*1024u)
/* Exact CPU-skinned prop results; borrowed from the GU arena, not heap. */
#define CHAR_MATERIAL_CACHE_BYTES (32u*1024u)
typedef struct {unsigned hits,misses,vertices,evictions;} CharMaterialCacheProfile;
/* Opt-in immutable placement scope. Increment revision whenever the root,
   model, pose or scene light changes. Close the scope with NULL. Bank
   teardown invalidates its entries; cached vertices are never GE-owned. */
void chars_material_cache_scope(const void *instance,unsigned revision);
void chars_material_cache_mode(int enabled); /* reference diagnostic */
void chars_material_cache_profile(CharMaterialCacheProfile *);
typedef struct {unsigned built_bones,reused_bones,owner_reads,vertex_reads;} CharPreparationProfile;
void chars_preparation_mode(int enabled); /* exact reference diagnostic; default on */
void chars_preparation_profile(CharPreparationProfile *out);
/* Fixed arrow mesh/pose workspace, charged to the existing GU arena. */
#define CHAR_ARROW_BYTES 4096u
/* Distant NPC textures replace part of the GU arena, never the gameplay heap. */
#define CHAR_LOD_BYTES (68u*1024u)
typedef struct {unsigned actors,texture_actors,hull_actors,hull_triangles,texture_binds,
    source_texels,lod_texels,texture_fallbacks,bytes;} CharLodProfile;
void chars_lod_mode(int enabled); /* same-camera diagnostic; default on */
void chars_lod_totals(CharLodProfile *out);
void chars_lod_profile(CharLodProfile *out);
void chars_prepare_lod(Chars *,int model); /* load-time only; bounded, optional cache */
void chars_draw_npc(Chars *,int,float x,float y,float z,float yaw,int motion,float frame,
    float opacity,const float view[16],float focal);
/* Ordinary people-bank draw: opt into exact compact GE packets. Other
   users of chars_draw_npc (including PLAY encounters) keep their adapter. */
void chars_draw_pedestrian(Chars *,int,float x,float y,float z,float yaw,int motion,float frame,
    float opacity,const float view[16],float focal);
/* Render-only gate for the complete already-posed pedestrian bounds. The
   callback runs inside a possible preserved VFPU scope: scalar code only,
   no GU/GUM calls or VFPU use. NULL and frame begin disable the gate. */
typedef int (*CharPedOcclusionQuery)(void *context,const float center[3],const float extent[3]);
void chars_ped_occlusion_query(CharPedOcclusionQuery query,void *context);
typedef struct {unsigned draws,strip_draws,source_vertices,vertices,source_indices,indices,
    triangles,built,reused,fallbacks;} CharPedPacketProfile;
void chars_ped_packet_mode(int enabled); /* exact same-view diagnostic; default on */
void chars_ped_packet_totals(CharPedPacketProfile *);
void chars_ped_packet_profile(CharPedPacketProfile *);
/* Native strip/reference sidecar and bounded owner-cache scratch. Compiled
   model plans use the existing GE packet pages, with no extra gameplay heap. */
#define CHAR_PED_SHARED_BYTES (56u*1024u)
typedef struct {unsigned actors,fallback_actors,source_vertices,unique_positions,
    draws,triangles,indices,vertex_bytes,built,reused,transform_us,gather_us,submit_us;} CharPedSharedProfile;
void chars_ped_shared_mode(int enabled); /* same-view diagnostic; default on */
void chars_ped_emit_mode(int enabled); /* native packed emitter A/B; default on */
void chars_ped_math_mode(int enabled); /* combined hierarchy/position VFPU scope; default on */
int chars_ped_math_enabled(void);
void chars_actor_timing_mode(int enabled); /* fine timers off by default; work counts remain */
int chars_actor_timing_enabled(void);
unsigned chars_final_lookup_profile(void); /* diagnostic lookup us, read/reset; zero in release */
void chars_ped_shared_totals(CharPedSharedProfile *);
void chars_ped_shared_profile(CharPedSharedProfile *);
/* Transform-only diagnostic: retain GE packet topology/weights/materials,
   replace XYZ using 1=scalar or 2=VFPU and use identity bone matrices.
   Zero retains normal hardware skinning. Hierarchy posing is unchanged. */
void chars_ped_transform_mode(int mode);
/* Rigid-root encounter scope; preserve custom poses and blending adapters. */
int chars_npc_detail_begin(const float root[16],const float view[16],float focal);
void chars_npc_detail_end(int previous);
typedef struct {unsigned scopes,requests,drawn,culled,reference,pose_built,pose_reused,draw_calls,total_us;} CharArrowProfile;
void chars_arrow_mode(int enabled); /* same-view diagnostic; default on */
void chars_arrow_projection(const float *projection); /* camera-space bounds, NULL disables */
void chars_arrow_totals(CharArrowProfile *);
void chars_arrow_profile(CharArrowProfile *); /* read/reset window, totals remain monotonic */
void chars_arrows_begin(void);
void chars_draw_arrow(Chars *,int,const float *root,float frame);
void chars_arrows_end(void);
typedef struct {unsigned hits,built,entries,visited,build_us,group_vertices,group_skipped;} CharPlanProfile;
void chars_draw_plan_mode(int enabled); /* diagnostic; default on */
void chars_pose_span_mode(int enabled); /* exact same-view diagnostic; default on */
void chars_plan_totals(CharPlanProfile *out); /* snapshot; no reset */
void chars_plan_profile(CharPlanProfile *out); /* read/reset */
/* Begin only after the previous GU list has completed. */
void chars_frame_begin(void);
void chars_frame_end(void);
/* Scope only to world-space actors; NULL disables posed frustum rejection. */
void chars_world_frustum(const float planes[6][4]);
/* Optional fine rejection for large cutscene sets; reset after the movie. */
void chars_batch_frustum(const float planes[6][4]);
/* Final's architecture uses models0..4 in this bank. Clip only triangles
   unsafe for the GE; other models and callers retain their normal path. */
#define CHAR_FINAL_CLIP_BYTES 1536u
#ifndef JGR_FINAL_CEL_PACKET
#ifdef __PSP__
#define JGR_FINAL_CEL_PACKET 1
#else
#define JGR_FINAL_CEL_PACKET 0
#endif
#endif
#define CHAR_FINAL_CEL_PACKET_BYTES 512u
typedef struct {unsigned hits,built,misses,rejected,triangles,cuts_reused,bytes,resident_bytes;} CharFinalCelPacketProfile;
void chars_final_cel_packet_mode(int enabled);
void chars_final_cel_packet_profile(CharFinalCelPacketProfile *out);
void chars_final_floor_clip(const Chars *bank,const float clip[16]);
typedef struct {unsigned batches,safe_batches,clipped_batches,outside_batches,
    tested_triangles,clipped_triangles,output_triangles;} CharFinalClipProfile;
void chars_final_clip_profile(CharFinalClipProfile *out);
int chars_actor_visible(const Chars *,int,const float root[16]);
/* Complete all-motion world AABB; zero means unknown and must remain visible. */
int chars_actor_bounds(const Chars *,int,const float root[16],float center[3],float extent[3]);
int chars_sphere_visible(const float center[3],float radius);
void chars_render_profile(unsigned out[4]); /* prepared/reused packets, vertices, culled actors */
void chars_draw_phase_profile(unsigned out[3]); /* posed draw total, GE skin, CPU-skinned submission (us) */
void chars_cel_profile(unsigned out[2]); /* boundary-cut triangles, cel batches */
void chars_cel_mixed_profile(unsigned out[3]); /* joined batches, saved draws, conservative extra GU bytes */
void chars_cel_shared_profile(unsigned out[3]); /* shared cuts, vertices avoided, edge interpolations avoided */
/* Final-only exact pre-clip CPU results, borrowed from the existing GE pool.
   vertices counts hits; bytes counts payload written by fills (not residency).
   An observation is admitted only after the same owner matrices repeat. */
typedef struct {unsigned hits,fills,vertices,bytes,evictions,rejections,observations,search_steps;
    unsigned admission_attempts,admission_failures,redundant_probes_avoided;} CharFinalVertexProfile;
void chars_final_cel_vfpu_mode(int enabled); /* Optional VFPU: 0 scalar, 1 copy+edges, 2 copy only. */
void chars_final_vertex_mode(int enabled);
void chars_final_admission_retry_mode(int enabled); /* guarded exact allocator retry elimination */
void chars_final_vertex_profile(CharFinalVertexProfile *out); /* read/reset */
/* Attribute existing renderer counters to a caller-owned gameplay scope.
   Calls count completed scopes, not actors. Phase timers overlap (GE skin
   includes packet work); total_us is the enclosing CPU elapsed time.
   Legacy read/reset profiles must be consumed outside open work scopes. */
enum { CHAR_WORK_PLAYER,CHAR_WORK_PEDESTRIANS,CHAR_WORK_TRAFFIC,CHAR_WORK_OTHER,CHAR_WORK_COUNT };
typedef struct {unsigned calls,total_us,pose_us,bounds_us,packet_us,skin_us,bones,
    bone_uploads,skin_draws,skin_vertices,cpu_skin_us,culled;} CharWorkStats;
void chars_work_begin(CharWorkStats *mark);
void chars_work_end(unsigned kind,const CharWorkStats *mark);
void chars_work_profile(CharWorkStats out[CHAR_WORK_COUNT]); /* read/reset window only */
void chars_work_totals(CharWorkStats out[CHAR_WORK_COUNT]); /* monotonic snapshot */
#if JGR_DIAGNOSTICS && (defined(__PSP__) || defined(JGR_TEST_ACTOR_WORK))
#define CHAR_WORK_BEGIN(name) CharWorkStats name;chars_work_begin(&(name))
#define CHAR_WORK_END(kind,name) chars_work_end((kind),&(name))
#else
/* Standalone gameplay harnesses can retain their renderer mocks. */
#define CHAR_WORK_BEGIN(name) ((void)0)
#define CHAR_WORK_END(kind,name) ((void)0)
#endif
/* Static-bone batch cache for a scoped room draw (garage). mask: one bit per
   bone whose pose is constant over `motion` and whose ancestors are too. */
unsigned chars_static_bones(const Chars *,int ci,int motion,unsigned char *mask,unsigned *cacheable_vertices);
int chars_static_cache_prepare(const Chars *,const unsigned char *mask,unsigned vertices); /* -1: no cache */
void chars_static_cache_scope(int active);
void chars_static_cache_profile(unsigned out[2]); /* hits, fills */
void chars_group_profile(unsigned out[3]); /* grouped draws, weighted vertices, max weights; work counts, not GE cycles */
void chars_bone_profile(unsigned out[2]); /* GE palette uploads/reuse; read/reset */
void chars_cpu_profile(unsigned out[5]); /* pose/packet/CPU-skin/bounds us, posed bones */
/* draw every character; planes = frustum for a cheap per-character sphere test */
void chars_draw(Chars *c, const float planes[6][4]);
/* draw one character at an explicit pose: world pos/yaw (radians), motion index relative to its motion0, frame */
void chars_draw_one(Chars *c, int ci, float x, float y, float z, float yaw, int motion, float frame);
void chars_draw_one_faded(Chars *c,int ci,float x,float y,float z,float yaw,int motion,float frame,float opacity);
/* 06dd7c distance fade for any actor draw between begin and end; an opacity of one or more is a no-op. */
void chars_fade_begin(float opacity);
void chars_fade_end(void);
void chars_draw_one_blend(Chars *c, int ci, float x, float y, float z, float yaw,
                         int motion, float frame, int motion2, float frame2, float weight,
                         int overlay, float overlay_frame, float overlay_weight);
void chars_draw_one_xf(Chars *c, int ci, const float *xf, int motion, float frame);   /* explicit row-major root matrix */
/* Visible NJS hierarchy (0b8b1c), including eval flags and keyed-channel
   overrides. Unlike 0be244 collision anchors, omitted TRS is omitted. */
void chars_mesh_matrices(const Chars *,const JgcChar *,int motion,float frame,
                         const float root[16],float mats[64][16]);
struct CharPose;
void chars_pose_mesh_matrices(const Chars *,const JgcChar *,const struct CharPose *,
                              const float root[16],float mats[64][16]);
void chars_draw_effect(Chars *c,int ci,const float *xf,float frame,float alpha);
/* Depth-tested room geometry with PSP-safe frustum clipping. Scoped to this
   draw; actors and other scenes retain their existing GE skinning path. */
void chars_draw_material_alpha_clipped(Chars *,int,const float *,float,const unsigned short *,const float [6][4]);
void chars_draw_material_pass_clipped(Chars *,int,const float *xf,int motion,float frame,int pass,const float planes[6][4]);
void chars_draw_material_pose_clipped(Chars *,int,const float *xf,const struct CharPose *,int pass,const float planes[6][4]);
void chars_draw_material_bind_clipped(Chars *,int,const float *xf,int pass,const float planes[6][4]);
/* Scoped triangle sorting for translucent two-sided meshes. NULL disables. */
void chars_material_depth_sort(const float forward[3]);
/* Source background pass: no depth reads/writes, CPU frustum clipping for
   the PSP GE, while keeping the original geometry/UV/material colours. */
void chars_draw_background(Chars *,int,const float *xf,const float planes[6][4]);
/* Set the current row-major PSP view for source environment-map strips. */
void chars_environment_view(const float *view);
void chars_final_lighting(int enabled);
/* Scoped PLAY recovery material offset; caller restores zero after actor. */
void chars_recovery_flash(float strength);
void chars_spray_hud_lighting(int enabled);
typedef struct {float direction[3],diffuse,ambient;unsigned color[2];
    int raw_direction; /* some draw tasks skip source direction normalization */
} CharLight;
/* Scoped source movie/garage light descriptors. NULL restores stage light. */
void chars_scene_lighting(const CharLight *normal,const CharLight *cel);
/* Before first submission only: prepare ordinary lighting for bones whose
   complete ancestor rotation/scale is constant throughout a movie segment. */
unsigned chars_prepare_movie_light(Chars *,unsigned,const struct CharPose *,const unsigned char *,const CharLight *);
/* Scoped original UI post-projection depth. Identity is (1,0); 02f000
   uses (2,0), mode-7 0ba260 uses (1,1). XY/UV/light remain unchanged. */
void chars_hud_depth(float reciprocal_scale,float depth_shift);
int chars_bind_material_texture(Chars *,unsigned texture);
void chars_external_texture(const unsigned short *pixels,unsigned width,unsigned height);
int chars_load_lighting(Chars *,const char *path);
void chars_free_lighting(Chars *);
void chars_draw_one_xf_blend(Chars *c,int ci,const float *xf,int motion,float frame,
                           int motion2,float frame2,float weight,int overlay,float overlay_frame,float overlay_weight);
struct CharPose;
/* Source non-cel draw path, with optional frozen CNK pose and texture-list
   indirection. Remapping selects resident textures without mutating any
   palette/image while the asynchronous PSP GE is using it. */
void chars_draw_material(Chars *,int ci,const float *xf,const struct CharPose *,int motion,float frame,float alpha,const unsigned short *texture_map);
/* -1 both lists, 0 opaque, 1 translucent. Allows source modifier-volume
   composition BETWEEN the opaque receiver and translucent room lists. */
void chars_draw_material_pass(Chars *,int,const float *,const struct CharPose *,int,float,float,const unsigned short *,int);
/* 0cc7c8 typed EVENT body: mode-0 surface/outline with additive sirens. */
void chars_draw_event_actor(Chars *,int,const float *,const struct CharPose *);
/* Stationary vehicles: opaque mode-0 body followed by additive beacons. */
void chars_draw_vehicle(Chars *,int,const float *,int,float);
void chars_draw_one_pose(Chars *c,int ci,const float *xf,const struct CharPose *pose,
                         int overlay,float overlay_frame,float overlay_weight);
/* Consecutive custom poses from one bank, without overlays. Owns GU state
   until end: do not nest or interleave other renderers. Geometry, ordering,
   palettes and each actor's live pose remain independent. */
void chars_pose_batch_begin(Chars *);
void chars_pose_batch_end(void);
void chars_draw_one_rest(Chars *,int,const float *); /* static source object, no motion sampler */
void chars_draw_range(Chars *c, const float planes[6][4], int first, int last);
#endif
