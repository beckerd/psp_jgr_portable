#ifndef COLLIDE_H
#define COLLIDE_H
#include <stdint.h>
/* Game collision data (COL1, from tools/jgs_collision.py): triangles with the original surface types and masks.
   Probe math transcribes FUN_8c0b648c / FUN_8c0b42d4 / FUN_8c0b45ec.
   COL1 currently stores canonical triangle winding, not raw strip order.
   The legacy field named dbl preserves source bit 31, not a promise of
   generic two-sided Moller-Trumbore behavior. */
typedef struct { float v[9]; unsigned short surf, mask, dbl, src; } ColTri;   /* 44 bytes, as in the file */
enum { COL_PATCH_BIT31=1, COL_PATCH_REVERSED=2 };
/* REVERSED preserves a source bit-30-clear triangle while keeping the stored
   vertex order front-facing for legacy body/render consumers. New providers
   must retain this bit; old COL1 exporters did not. */
typedef struct ColSurface ColSurface;
typedef struct ColSource ColSource;
typedef struct ColItems ColItems;
typedef struct ColWorldGrid ColWorldGrid;
typedef struct ColWorldIndexed ColWorldIndexed;
#define COL_SURFACE_CAPACITY 64
#define COL_SURFACE_TRIANGLES 1024
typedef struct {
    ColTri *tri; int ntri, nbase, cap;      /* owned geometry; ntri/nbase count LOGICAL contacts */
    /* Immutable COL1 records may share byte-identical storage. Keep every
       logical id and grid entry; never deduplicate hit/response ownership.
       Resolve logical contacts with col_contact, not tri[id]. */
    void *alias;int alias16,alias_cap,ngeom,nbase_geom;
    float x0, z0, cell; int gw, gh;          /* uniform XZ grid (128-unit cells like FUN_8c0b409c) */
    int *cellstart;void *cellidx;            /* lossless 16/32-bit logical triangle ids */
    unsigned cellstart_cap,cellidx_bytes;   /* retain grid capacity across lesson resets */
    int index16;                           /* only when every logical id fits uint16 */
    unsigned char *base;
    ColSource *source; /* original world patches and hashed grid; owned */
    ColItems *items; /* original placed-object patch ranges; owned */
    ColWorldGrid *world_grid; /* optional bounded incremental animated-world grid */
    ColWorldIndexed *world_indexed; /* optional unique-vertex publication workspace */
    /* Optional original moving-mesh contact data. Static city banks leave
       this NULL; providers retain stable triangle order across samples. */
    int (*surface_motion)(void *,int,const float *,float *,int *);
    void *surface_context;
    ColSurface *surfaces; /* lazy, owned slot table; geometry remains provider-owned */
} Collision;
static inline int col_grid_index(const Collision *c,int index)
{return c->index16 ? ((const unsigned short *)c->cellidx)[index]:((const int *)c->cellidx)[index];}
typedef struct { float t; float p[3], n[3]; unsigned short surf, mask; int tri; } ColHit;
/* Query wall time includes preemption by audio/decoder threads. */
typedef struct {unsigned ray_queries,body_queries,ray_us,body_us,plane_hits,plane_misses,body_plane_hits,body_plane_misses,ray_reject_reuse,body_reject_reuse,source_entries,source_patches,source_triangles,patch_batches,ray_query_reuse,body_query_reuse,ray_reuse_overflow,body_reuse_overflow,candidate_hits,candidate_misses,candidate_overflows;
    unsigned grid_publishes,grid_rebuilds,grid_reused_rects,grid_changed_rects,grid_fallbacks,grid_bytes;
    unsigned grid_bounds_us,grid_copy_us,grid_update_us,grid_indexed_publishes,grid_indexed_bytes;
    unsigned grid_unchanged_triangles,grid_unchanged_vertices,grid_unchanged_cells;
    unsigned grid_cell_code_same,grid_cell_rect_skips,grid_rect_evaluated;
    unsigned grid_dirty_publishes,grid_dirty_visited,grid_dirty_skipped;
    unsigned grid_dirty_builds,grid_dirty_fallbacks,grid_dirty_bytes;
    unsigned candidate_overflow_hits; /* remembered full keys use the original streaming walk */
} ColProfile;
void col_profile_read(ColProfile *out);
/* Current interval counters and resident-byte gauges; does not reset. */
void col_profile_totals(ColProfile *out);
/* Diagnostic A/B: disabling ignores overflow markers and stops publishing
   them. Successful candidate lists retain their usual lifetime. */
void col_candidate_overflow_mode(int enabled);
void col_predicate_profile(unsigned out[2]);
typedef struct {
    unsigned address,kind,first,count;
    unsigned candidates,patch_tests,patch_skipped,ray_exact,body_exact,samples,sample_us;
} ColItemProfile;
/* Largest candidate counts in this interval; read also resets all owners. */
int col_profile_items(const Collision *c,ColItemProfile *out,int capacity);
int  col_load(Collision *c, const char *path);
void col_free(Collision *c);
/* set-placed object colliders (JMSc +0x34 records, per-type meshes from stageN.sob): append world-space triangles / drop them again */
int  col_add(Collision *c, const ColTri *t, int n);
/* Batched set placement: append without querying, then rebuild ONCE before
   any probe/body consumer runs. col_add remains the immediate-update API. */
int col_append(Collision *c,const ColTri *t,int n);
int col_rebuild(Collision *c);
/* Replace the complete world of an animated arena, rebuilding its broadphase.
   Not used for city geometry or set-object append/reset operations. */
int col_set_world(Collision *c,const ColTri *t,int n);
/* Final arena only: identical cell/contact order, updating changed triangle
   memberships instead of rebuilding packed lists. Falls back to the legacy
   grid if its bounded optional allocation cannot represent this world. */
#define COL_WORLD_GRID_BYTES (128u*1024u)
int col_set_world_incremental(Collision *c,const ColTri *t,int n);
/* Final-only indexed inputs. Parts concatenate in logical contact order;
   indices are matching XYZ corner triples into contiguous vertices. Indices
   must stay immutable while their pointer, topology and counts are unchanged.
   Empty parts are allowed, but the total must be positive. NULL indices uses
   the ordinary triangle path. Input storage is borrowed only during a call. */
typedef struct {
    const ColTri *triangles;const float *vertices;const unsigned *indices;
    unsigned triangle_count,vertex_count,topology;
} ColWorldIndexedPart;
#define COL_WORLD_INDEXED_BYTES (64u*1024u)
#ifndef JGR_COLLISION_CELL_REUSE
#define JGR_COLLISION_CELL_REUSE 1
#endif
#ifndef JGR_COLLISION_DIRTY_TRIANGLES
#ifdef __PSP__
#define JGR_COLLISION_DIRTY_TRIANGLES 1
#else
#define JGR_COLLISION_DIRTY_TRIANGLES 0
#endif
#endif
/* Optional Final indexed-world adjacency. Failure or excess topology uses
   the original full triangle scan; this storage never borrows GU memory. */
#define COL_WORLD_DIRTY_BYTES (32u*1024u)
/* Runtime measurement keeps the same adjacency allocation in both modes;
   disabled mode selects the original full scan. No-op when compiled out. */
void col_dirty_mode(int enabled);
int col_set_world_indexed(Collision *c,const ColWorldIndexedPart *parts,unsigned part_count);
/* Optional Final-only stability proof. Set bits promise exact unchanged
   bytes since the preceding producer generation, in logical triangle or
   source-vertex order. Reuse requires matching topology and consecutive
   nonzero generations of successful indexed publications; otherwise every
   value is processed normally. Old callers need no additional fields. */
typedef struct {
    const uint32_t *triangles,*vertices;unsigned generation;
} ColWorldStability;
int col_set_world_indexed_stable(Collision *,const ColWorldIndexedPart *,unsigned,const ColWorldStability *);
void col_reset_extra(Collision *c);
/* Moving SURFACE registration, not body boxes/slip volumes. Return 0 on
   failure. Handles/triangle ids stay stable through publish and cannot alias
   another owner on removal/reuse. Current/previous arrays must have matching
   topology and remain alive until the next publish/removal/col_free. */
int col_surface_add(Collision *c,const ColTri *current,const ColTri *previous,int n);
int col_surface_publish(Collision *c,int handle,const ColTri *current,const ColTri *previous,int n);
int col_surface_remove(Collision *c,int handle);
/* 0 invalid/stale; 1 static; 2 registered surface. Output pointers are borrowed. */
int col_contact(const Collision *c,int id,const ColTri **current,const ColTri **previous);
/* Recover raw source strip order without changing canonical stored geometry. */
void col_contact_vertices(const ColTri *t,float out[9]);
/* nearest hit along p0->p1 (t in [0,1]); returns 1 if hit. maskbits: 0 = any */
int  col_probe(const Collision *c, const float *p0, const float *p1, unsigned mask, ColHit *hit);
/* Approximate render-only torso visibility. Static city and appended placed
   geometry, two-sided, no physics cache/profile changes or moving surfaces.
   Uncertainty is separate so the caller can choose aggressive culling. */
enum {COL_RENDER_UNAVAILABLE=-1,COL_RENDER_CLEAR=0,COL_RENDER_BLOCKED=1,COL_RENDER_UNCERTAIN=2};
#define COL_RENDER_MAX_TRIANGLES 128u
#define COL_RENDER_MAX_STEPS 256u
typedef struct {unsigned triangle_tests,box_tests,steps,budget_exhausted;} ColRenderOcclusionStats;
int col_render_occlusion(const Collision *c,const float eye[3],const float target[3],ColRenderOcclusionStats *stats);
/* Filter DURING traversal: an excluded foreground patch must not hide an
   accepted patch farther down the segment (FUN_8c055c1c hit-list walk). */
typedef int (*ColAccept)(const ColTri *tri, const float *normal, const void *context);
int col_probe_filtered(const Collision *c, const float *p0, const float *p1,
                       unsigned mask, ColAccept accept, const void *context, ColHit *hit);
/* convenience: highest ground under (x,z) between y+up and y-down */
int  col_ground(const Collision *c, float x, float y, float z, float up, float down, ColHit *hit);
typedef struct { float p[3], n[3], depth; unsigned short surf, mask; int tri, probe; } ColBodyHit;
/* FUN_8c0b56b8: capsule feature contacts, retaining ALL qualifying patches
   (not the nearest ray hit). old is the one-sided reference centre. */
int col_body_triangle(const ColTri *tri,const float *old,const float *a,const float *b,
                      float radius,ColBodyHit *hit);
int col_body(const Collision *c,const float *old,const float *a,const float *b,
             float radius,unsigned mask,ColBodyHit *hits,int capacity);
/* 0b65c8 / 0b4dd4: stationary sphere against solid source patches. Unlike
   the swept body query, exact plane tangency and a centre on the plane
   qualify. Returns the nearest front-side contact (stable source-order
   ties); hit.t is plane distance in world units, not a segment fraction. */
int col_sphere(const Collision *c,const float center[3],float radius,unsigned mask,ColHit *hit);
#endif
