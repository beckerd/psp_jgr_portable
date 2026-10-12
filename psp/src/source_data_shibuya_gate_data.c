/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct { float cx, cy, cz, r; unsigned first_batch, n_batch, flags, pad; } JgsObj;
typedef struct {unsigned sector,object;float position[3];int32_t angles[3];JgsObj original;} BarrierPlacement;
typedef struct { float v[9]; unsigned short surf, mask, dbl, src; } ColTri;

#include "source_data_sections.h"
BarrierPlacement barrier_placements[6] __asm__("jgr_data_shibuya_gate_data_barrier_placements") __attribute__((aligned(16)));
ColTri barrier_collision[10] __asm__("jgr_data_shibuya_gate_data_barrier_collision") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {589732840,64,384,16,barrier_placements},
    {3816845586,448,440,16,barrier_collision},
};
int source_data_shibuya_gate_data_load(void){return source_data_sections_load("source_data_shibuya_gate_data.jsd",sections,2u,888u,0xa70db555u);}
#endif
