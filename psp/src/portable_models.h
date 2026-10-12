#ifndef JGR_PORTABLE_MODELS_H
#define JGR_PORTABLE_MODELS_H
#include "portable_disc.h"
#include <stdio.h>

/* Shared bounded Ninja chunk-model decoder for setup. No original geometry,
 * animation keys or texture bytes are present in this implementation. */
typedef struct JgrModelCache JgrModelCache;
typedef struct {
    double direction[3], diffuse, ambient, rgb[3], cel_direction[3];
    unsigned cel_colors[2][3];
} JgrModelLight;
typedef struct {
    uint32_t address, eval, model, child, sibling;
    double position[3], scale[3];
    int32_t angle[3];
} JgrModelNode;
typedef struct {
    double matrix[16], bind[16], inverse_bind[16];
    const uint16_t *texture_map;
    unsigned texture_slots, texture_base, texture_count;
    int owner; /* -1: JGS/JGC1, otherwise owner-local JGC2/3. */
    unsigned cache_vertices, light, toon, two_sided;
    unsigned source_material, source_environment, source_lighting;
    double max_edge;
    const JgrModelLight *baked_light;
    const double *light_matrix, *post_matrix;
    unsigned palette_mix, retain_lighting; /* zero keeps ordinary conversion. */
    double shadow_lift;
} JgrModelOptions;
typedef struct {
    unsigned texture, flags, vertices, indices, stride;
    const unsigned char *vertex_bytes; /* LE JGS/JGC vertex records. */
    const unsigned char *index_bytes;  /* LE u16, excludes alignment pad. */
    const double *positions; /* packed xyz doubles for exact object bounds. */
    const unsigned char *lighting_bytes; /* optional LE normal/material records. */
} JgrModelBatch;
typedef int (*JgrModelEmit)(const JgrModelBatch *, void *);
typedef int (*JgrModelVisit)(const JgrModelNode *, const double matrix[16],
                             unsigned ordinal, int parent, void *);

JgrModelCache *jgr_model_cache_create(void);
void jgr_model_cache_clear(JgrModelCache *);
void jgr_model_cache_free(JgrModelCache *);
void jgr_model_options_default(JgrModelOptions *);
void jgr_model_identity(double matrix[16]);
void jgr_model_multiply(double out[16], const double a[16], const double b[16]);
int jgr_model_inverse(double out[16], const double matrix[16]);
int jgr_model_node(JgrDisc *, const JgrDiscFile *, uint32_t base,
                    uint32_t address, JgrModelNode *);
void jgr_model_node_matrix(double out[16], const JgrModelNode *, int legacy);
int jgr_model_baked_light(JgrDisc *, JgrModelLight *);
/* Tree traversal detects cycles and rejects a depth >128 or >4096 nodes. */
int jgr_model_walk(JgrDisc *, const JgrDiscFile *, uint32_t base, uint32_t root,
                   const double parent[16], int legacy, int root_sibling,
                   JgrModelVisit, void *);
/* A cache is reusable across one tree, and must be cleared between trees.
 * Input reads stream through a 4 KiB window. Temporary geometry is bounded
 * to 65,536 vertices and 1,048,576 references per source material group.
 * Returns -1 on malformed/unsupported input or output callback failure. */
int jgr_model_mesh(JgrModelCache *, JgrDisc *, const JgrDiscFile *,
                   uint32_t base, uint32_t address,
                   const JgrModelOptions *, JgrModelEmit, void *);

/* Lossless channel-table sharing or mesh-only derivative of a previously
 * generated JGC bank. Streaming copy, <=1 MiB temporary metadata. */
int jgr_model_compact(FILE *source, FILE *output, int mesh_only);
/* Optional complete immutable view, borrowed only for the duration of the call. */
int jgr_model_compact_view(FILE *source,FILE *output,int mesh_only,
                           const void *input_data,size_t input_bytes);

typedef struct JgrJgcBuild JgrJgcBuild;
typedef struct {
    JgrDiscFile source, motion_source;
    uint32_t base, root, motion_base, motion_table;
    const uint32_t *motion_headers; /* Optional explicit absolute addresses. */
    unsigned motion_count;
    char name[16];
    float placement[4];
    JgrModelOptions model;
    unsigned force_alpha, force_two_sided, effect_blend, collision_tree;
    FILE *lighting; /* optional caller-owned raw PLT record stream. */
} JgrJgcCharacter;
typedef struct {unsigned width,height,flags;uint32_t clut,pixels;} JgrJgcTexture;
typedef int (*JgrJgcTextureEmit)(unsigned source_index,FILE *pixels,
                                JgrJgcTexture *record,void *user);
/* Ten empty caller-owned seekable temporary streams: chars, bones, batches,
 * vertices, indices, textures, motion headers, keys, pixels, channels. The
 * caller closes/removes these streams on both success and failure. */
JgrJgcBuild *jgr_jgc_create(JgrDisc *, FILE *sections[10], int legacy);
void jgr_jgc_free(JgrJgcBuild *);
int jgr_jgc_add(JgrJgcBuild *, const JgrJgcCharacter *);
/* Resolve ordinary PLAY roots/motion table and its first PTEX list directly
 * from the user disc. Caller supplies >=512 u16 texture-map slots and keeps
 * them alive until jgr_jgc_add returns. Does not change name/placement/count. */
int jgr_jgc_resolve_play(JgrDisc *, const JgrDiscFile *source,
                         const JgrDiscFile *textures, unsigned lod,
                         unsigned texture_base, uint16_t texture_map[512],
                         JgrJgcCharacter *);
int jgr_jgc_keep_texture(JgrJgcBuild *, unsigned source_index);
/* Next used texture at or after start, or -1. Valid during finish callbacks. */
int jgr_jgc_next_texture(const JgrJgcBuild *,unsigned start);
int jgr_jgc_finish(JgrJgcBuild *, FILE *output, JgrJgcTextureEmit, void *user);
#endif
