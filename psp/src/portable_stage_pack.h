#ifndef JGR_PORTABLE_STAGE_PACK_H
#define JGR_PORTABLE_STAGE_PACK_H
#include <stdio.h>
/* Compact existing batch spans without changing rendered bytes. Optional
 * vertex ordering keys preserve the historical Kogane packing convention. */
int jgr_stage_pack(FILE *source,FILE *vertex_keys,FILE *output,int kogane);
/* Optional read-only view of vertex_keys, held by the caller through return.
 * A NULL view retains the file-reading path. The view is never owned here. */
int jgr_stage_pack_view(FILE *source,FILE *vertex_keys,FILE *output,int kogane,
                      const void *key_view,size_t key_size);
int jgr_stage_ownership_hash(FILE *geometry,FILE *ownership);
#endif
