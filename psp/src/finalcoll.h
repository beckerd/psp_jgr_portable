#ifndef FINALCOLL_H
#define FINALCOLL_H
#include <stdint.h>
#include "collide.h"
#define FINALCOL_MAX_NODES 768
typedef struct {
    uint32_t source; int32_t piece,parent;
    uint32_t vertex0,nvertices,tri0,ntriangles;
    float pos[3]; int32_t angle[3]; float scale[3]; uint32_t angles;
} FinalColNode;
typedef struct { uint32_t v[3]; uint16_t surf,mask,dbl,node; } FinalColTri;
typedef struct {
    char magic[4]; uint32_t nodes,vertices,triangles,frames;
    uint32_t off_nodes,off_vertices,off_triangles,off_keys,length;
} FinalColHeader;
typedef struct {
    const unsigned char *data; const FinalColHeader *h;
    const FinalColNode *nodes; const float *local_vertices; const FinalColTri *triangles;
    float *matrices,*vertices; ColTri *world,*previous; int nworld;unsigned active;
    const unsigned char *override_data;const uint32_t *override_nodes;unsigned override_count,override_frames;
    /* No normal-motion keys in this node or any ancestor. A valid piece
       was last sampled with an identity root and no collapse override. */
    uint32_t static_nodes[(FINALCOL_MAX_NODES+31)/32];unsigned static_pieces;
    /* Optional immutable normal-motion local TRS, before parent/root
       composition. Collapse overrides never read or overwrite these. */
    float *local_matrices;uint32_t local_valid[(FINALCOL_MAX_NODES+31)/32];
    /* Optional compact triples matching world[].v corner order. Rebuilt
       on active-mask changes; active is their immutable-bank topology key.
       Only referenced vertices are initialized for the current world. */
    unsigned *world_indices;
    /* Provenance of the older world/history buffer. Static triangle output
       may be retained only when its compact layout and normal identity-root
       coordinates still match the current publication. */
    unsigned previous_active,previous_static_pieces;
    /* Optional byte-identical-to-previous-sample proofs in current compact
       triangle/source-vertex order. sample_generation never publishes zero. */
    uint32_t *unchanged_triangles,*unchanged_vertices;unsigned sample_generation;
    unsigned unchanged_active,unchanged_pieces,triangle_runs;
} FinalCol;
/* Caller owns image. Generated geometry/matrix storage is owned by FinalCol. */
int finalcol_init(FinalCol *c,const void *image,unsigned length);
void finalcol_free(FinalCol *c);
/* Normal arena mask = 0x1f; after source phase 4 the central collider switches
   from piece 4 to piece 5 (mask 0x2f). Keep each piece's original task clock.
   Matrices/vertices for inactive pieces are not sampled; only world/previous
   contain the current published contacts. Unchanged static hierarchies reuse
   their exact matrices/vertices; world/previous are published every sample.
   Their owned buffers may swap; callers must reacquire pointers each sample. */
int finalcol_sample(FinalCol *c,const float frames[6],unsigned active);
/* Caller owns FCO1 image. The primary bank is never modified in place. */
int finalcol_bind_overrides(FinalCol *,const void *,unsigned length);
int finalcol_sample_overrides(FinalCol *,const float frames[6],unsigned active,const float collapse_frames[4],unsigned collapse_mask);
/* 0b6990 uses the current Ninja matrix as the root of the clone walker.
   NULL is identity. Apply before composing vertices/history, not afterward. */
int finalcol_sample_world(FinalCol *,const float frames[6],unsigned active,const float world[16]);
int finalcol_surface(void *context,int triangle,const float *point,float *delta,int *yaw);
#endif
