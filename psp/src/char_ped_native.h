#ifndef JGR_CHAR_PED_NATIVE_H
#define JGR_CHAR_PED_NATIVE_H
/* JPN1: native cache definitions and authored strip references. The compact
   file borrows immutable attributes from its checksummed JGC geometry.
   All section offsets are file-relative; no serialized pointers. */
typedef struct {
    char magic[4];unsigned version,bytes,geometry_hash,nmodels,off_models,reserved[2];
} PedNativeHeader;
typedef struct {
    unsigned short npositions,nvertices,ngroups,nindices,source_vertices,nbones;
    unsigned off_positions,off_vertices,off_groups,off_indices,reserved;
} PedNativeModel;
typedef struct {unsigned short source_vertex,owner;} PedNativePosition;
typedef struct {unsigned short position,source_vertex;} PedNativeVertex;
typedef struct {
    unsigned short first_vertex,nvertices,first_index,nindices,tex,flags,triangles,strip;
} PedNativeGroup;
typedef char ped_native_header_size[sizeof(PedNativeHeader)==32 ? 1:-1];
typedef char ped_native_model_size[sizeof(PedNativeModel)==32 ? 1:-1];
typedef char ped_native_group_size[sizeof(PedNativeGroup)==16 ? 1:-1];
#endif
