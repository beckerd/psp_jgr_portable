#ifndef STAGE_H
#define STAGE_H
#include <stdio.h>
#include "city_profile.h"
/* JGS1: city objects are world-space culling units. Ordinary geometry is
 * baked to world space; optional JGI1 props share local-space geometry. */
typedef struct { char magic[4]; unsigned n_tex, n_obj, n_batch, n_vert, n_idx;
                 unsigned off_tex, off_obj, off_batch, off_vert, off_idx, off_pix;
                 float bmin[3], bmax[3]; } JgsHeader;
typedef struct { unsigned short w, h, flags, pad; unsigned off_clut, off_pix; } JgsTex;
typedef struct { float cx, cy, cz, r; unsigned first_batch, n_batch, flags, pad; } JgsObj;
typedef struct { unsigned vert_off, idx_off; unsigned short n_vert, n_idx, tex, flags; } JgsBatch;
typedef struct { float u, v; unsigned color; float x, y, z; } JgsVert;
typedef struct {float cx,cy,cz,r;} StageBatchBound;
/* JGI1 keeps a shared mesh in local space and each authored placement's
   world transform/bounds. Baked vertex colors remain part of the mesh. */
typedef struct {float matrix[12];StageBatchBound bound;} StageInstance;
typedef struct {JgsVert *vertices; unsigned short *indices;} StageBatchGeometry;
typedef struct StageGeometryPage StageGeometryPage;

#define JGS_TEX_ALPHA   1
#define JGS_B_ALPHA     1
#define JGS_B_2SIDED    2
#define JGS_B_NOTEX     4   /* strip without UVs: vertex colour only */
#define JGS_B_FALLBACK 16   /* converter could not bind this source texlist slot */
#define JGS_O_SKY 1        /* explicitly identified original sky root */
#define JGS_O_FAR 2        /* original alternate mesh, selected by JRS1 */
#define JGS_O_INSTANCE 4   /* local shared geometry, transformed by JGI1 */

typedef struct {
    unsigned char *base; JgsHeader *h; JgsTex *tex; JgsObj *obj; JgsBatch *batch;
    JgsVert *vert; unsigned short *idx;
    unsigned char *vertex_data,*index_data,*texture_data; /* optional split ownership */
    const unsigned char *texture_vram; unsigned texture_vram_bytes; /* borrowed VRAM */
    int batch_bounds_borrowed; /* resident command arena owns city bounds */
    unsigned char **texture_parts; /* optional immutable palette+pixels per texture */
    StageBatchGeometry *geometry; /* optional batch addresses in smaller resident pages */
    StageGeometryPage *geometry_pages;
    StageBatchBound *batch_bounds; /* optional load-time sub-object bounds */
    const unsigned short *object_instance; /* JGI1: zero or 1-based slot */
    const StageInstance *instances;unsigned instance_count;
    unsigned char *object_passes; /* low bits: prop passes; bits 2-3: conservative city passes */
    void *prop_draw_cache; /* bounded view/clip preparation, allocated at load */
    unsigned *clip_groups; /* batch -> first group; owns group_bounds allocation */
    unsigned clip_group_bytes; /* includes reference groups, cap 64 KiB */
    StageBatchBound *group_bounds; /* conservative bounds for consecutive triangles */
    /* stats from last draw */
    int drawn_obj, drawn_tris, drawn_batches, texswitch;
    int sky_explicit;      /* citysky owns exact roots, not all large objects */
    unsigned ped_proxy_bank; /* load-time verified immutable proxy bank, 0 unavailable */
} Stage;

static inline const JgsVert *stage_batch_vertices(const Stage *s,const JgsBatch *b)
{return s->geometry ? s->geometry[b-s->batch].vertices:s->vert+b->vert_off;}
static inline const unsigned short *stage_batch_indices(const Stage *s,const JgsBatch *b)
{return s->geometry ? s->geometry[b-s->batch].indices:s->idx+b->idx_off;}
static inline const StageInstance *stage_object_instance(const Stage *s,unsigned object)
{return s->object_instance && s->object_instance[object] ? s->instances+s->object_instance[object]-1:0;}

typedef struct {
    unsigned fast_triangles, tested_triangles, rejected_triangles;
    unsigned clipped_triangles, clipped_vertices, draw_calls;
    unsigned tested_batches,rejected_batches;
    unsigned tested_groups,accepted_groups,rejected_groups;
    unsigned classified_vertices,scalar_vertices,reduced_group_masks;
    unsigned cached_calls,sorted_batches,material_calls;
    unsigned joined_draws,joined_index_bytes,index_cache_hits;
    unsigned visibility_us,box_rejected,occlusion_tests,occlusion_rejected,occluders,visibility_frames,occlusion_searches;
    unsigned selection_us,selection_frames,sector_rejected,range_rejected,near_selected,far_selected;
    unsigned prop_candidates,prop_sector_rejected,prop_range_rejected,prop_selected; /* kind 9, before frustum/occlusion */
    unsigned prop_prepared,prop_prepare_hits,prop_empty_passes,prop_instance_draws;
    unsigned mesh_bound_tests,mesh_coarse_decisions;
    unsigned occlusion_batch_tests,occlusion_batches,occlusion_batch_triangles;
    unsigned clip_reject_tests,clip_reject_hits; /* actual-frustum reject before full polygon clipping */
    unsigned city_list_objects,city_object_visits,city_empty_passes;
} StageProfile;
typedef struct {
    CityTiming timing;
    StageProfile work;
    unsigned drawn_objects,drawn_batches,drawn_triangles,texture_switches;
    unsigned calibration_steps,calibration_us;
} StageCityProfile;
/* Capture-only, elapsed CPU scopes (including preemption), never GPU time.
   Begin/end must bracket each actual city section, excluding all later prop
   rendering even when it shares this Stage. span 0=prepare, 1=draw. Buckets
   are exclusive and sum to those spans; get is non-resetting. Frame begin
   resets the record. Disabled profiling performs no clock reads. */
void stage_city_profile_mode(int enabled);
void stage_city_profile_begin(const Stage *,unsigned span);
void stage_city_profile_end(void);
void stage_city_profile_get(StageCityProfile *);
/* Fixed resident commands borrow space from gfx's existing 2 MiB arena. */
#define STAGE_COMMAND_BYTES (516u*1024u)
void stage_forget_commands(void);
void stage_frame_begin(void); /* after the frame fence: retire full index caches */
#define STAGE_VISIBILITY_BYTES (144u*1024u)
/* Current-frame CPU masks and certified actor occlusion cones. Borrowed
   from the same fixed rendering budget; no heap or retained GE pointers. */
#define STAGE_PREPARED_BYTES (10u*1024u)
/* Includes compiled geometry metadata and current camera cones; no heap. */
#define STAGE_PED_PROXY_BYTES (8u*1024u)
#define STAGE_PED_PROXY_MAX 16u
int stage_render_load(Stage *s,const char *path);
/* Camera sector is sticky: -1 from a missed transition volume retains it. */
void stage_render_sector(Stage *s,int sector);
void stage_render_background(Stage *s,int visible);
void stage_render_mission(Stage *s,int sector,int area);
void stage_water_step(Stage *s,float dt);
/* Current 0c3498 task circle (slot 0); includes the original sector range changes. */
int stage_render_medium_circle(const Stage *s,float *ahead,float *radius);
void stage_render_view(Stage *s,const float eye[3],const float forward[3],
                       float horizontal_tangent,const float player[3]);
void stage_set_source_rendering(int sectors,int lod,int ranges); /* diagnostic */
unsigned stage_render_near_objects(const Stage *s);
/* Authored generic props keep the cached city copy until a reaction.
   Handles belong to this loaded JRS; resolve again after a stage reload. */
int stage_prop_bind(Stage *s,unsigned source_address,const float initial_transform[16]);
void stage_prop_hide(Stage *s,int handle,int hidden);
int stage_prop_visible(Stage *s,int handle,const float position[3],const float eye[3],
                       const float forward[3],float horizontal_tangent);
/* Draw only a hidden owner's current rigid placement, split by material
   pass (0 opaque, 1 alpha). delta = currentTRS * inverse(initialTRS).
   Current position gates the source view; clip is the live projection*view.
   Returns submitted batches (zero when culled/invalid), without rebuilding vertices. */
int stage_draw_prop(Stage *s,int handle,const float delta[16],const float position[3],
                     const float eye[3],const float forward[3],float horizontal_tangent,
                     const float clip[16],int pass);
/* One authored near mesh, in original source-node order. Animated props
   keep the root and child transforms independent without skinning a copy. */
int stage_draw_prop_part(Stage *s,int handle,unsigned part,const float delta[16],const float position[3],
                         const float eye[3],const float forward[3],float horizontal_tangent,
                         const float clip[16],int pass);
int stage_visibility_load(Stage *s,const char *path);
void stage_clip_mode(int enabled); /* reference versus refined groups; default on */
void stage_set_visibility(int enabled); /* diagnostic A/B; default enabled */
void stage_set_plane_occlusion(int enabled); /* diagnostic conservative depth comparison */
void stage_set_fragment_batching(int enabled); /* ordered clip spans; default on */
void stage_clip_reject_mode(int enabled); /* position-only actual-frustum rejection; default on */
void stage_visible_list_mode(int enabled); /* ordered visible city objects and pass gates; default on */
void stage_set_scenery_mode(int enabled); /* scalar fine groups vs hierarchical VFPU groups */
void stage_set_optimizations(int commands,int grouping); /* 0 SDK, 1 geometry, 2 material+geometry; default 2,0 */
void stage_profile_totals(StageProfile *out); /* snapshot; no reset */
void stage_profile_read(StageProfile *out); /* accumulated world draws; read/reset */
/* Triggered-only diagnostic, after the GE fence and BEFORE stage_frame_begin
   or a new draw preparation. With stage_capture_submit, next-logic prop hits
   and source setters may invalidate live queries without losing this frame.
   Streams metadata without allocating, resetting counters or rebuilding. */
void stage_dump_frame(const Stage *s,FILE *out);
/* Diagnostic mode only: preserve a small submitted-frame stamp, borrowing
   existing masks until the next frame begins. Returns stamp bytes, or zero
   if no valid completed city draw exists. No allocation or GPU pointers. */
unsigned stage_capture_submit(const Stage *s);
unsigned stage_capture_bytes(void);

int  stage_load(Stage *s, const char *path);
int stage_load_city_vram(Stage *s,const char *path,void *memory,unsigned capacity);
int stage_load_city(Stage *s,const char *path); /* separate texture ownership */
unsigned stage_release_pinned_texture_copies(Stage *s); /* VRAM owns until stage_free */
void stage_free(Stage *s);
/* Caller supplies aligned unused VRAM after a GE fence; load-time only. */
void stage_cache_textures(Stage *s,void *memory,unsigned capacity);
void stage_set_mipmaps(int enabled); /* optional authored world mips, default on */
void stage_mip_profile(unsigned out[2]); /* loaded count / VRAM mip bytes */
void stage_texture_profile(unsigned out[4]); /* resident count/bytes, binds in VRAM/RAM */
void stage_draw_sky(Stage *s);   /* the stage bank's sky dome objects (bounding radius > 1500): depth test off, no culling */
/* World-space visibility frustum: 6 normalized inward planes. clip is the
   LIVE row-major projection*view matrix, even when debug culling is frozen.
   GE-unsafe triangles are clipped; resident assets stay immutable. */
void stage_draw(Stage *s, const float planes[6][4], const float clip[16], int cullmode /*0 none,1 cw,2 ccw*/);
/* Optional early preparation, before actor pose prefetch. stage_draw consumes
   the matching masks once; ordinary callers can still call stage_draw alone. */
void stage_prepare_draw(Stage *s,const float planes[6][4],const float clip[16],int cullmode);
/* A complete conservative world-space actor AABB. Invalid/stale contexts or
   unsupported views fail open. These queries do not change actor simulation. */
int stage_actor_occluded(const Stage *s,const float center[3],const float extent[3]);
unsigned stage_actor_occluders(const Stage *s);
/* Approximate torso-only occlusion. Prepare after stage_prepare_draw; add
   only placed objects selected by the actual draw's range/frustum/LOD tests.
   Bus bodies are deliberately solid/inset, including their window regions.
   Missing/stale data, camera-inside bodies and ambiguous boundaries keep. */
void stage_ped_proxy_prepare(const Stage *city,const float eye[3]);
void stage_ped_proxy_add_object(const Stage *city,const Stage *bank,unsigned model,
                              float x,float y,float z,float yaw);
unsigned stage_ped_proxy_count(const Stage *city);
int stage_ped_proxy_occluded(const Stage *city,const float torso[3],unsigned *tested);
void stage_draw_obj(Stage *s, int idx);   /* one object, current matrices, no culling */
/* Same current GU model matrix; clip must be projection * view * model. */
void stage_draw_obj_clipped(Stage *s,int idx,const float clip[16]);
/* Original per-texture UV animation; vertices remain immutable. */
void stage_draw_obj_uv_clipped(Stage *s,int idx,const float clip[16],int tex,float u,float v);
#endif
