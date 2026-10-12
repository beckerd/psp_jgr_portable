/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float PAR_T0[24] __asm__("jgr_data_mixed_gang_c__par_t0_0_PAR_T0") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2346627108,48,96,16,PAR_T0},
};
int source_data_mixed_gang_c__par_t0_0_load(void){return source_data_sections_load("source_data_mixed_gang_c__par_t0_0.jsd",sections,1u,144u,0x110e07aeu);}
#endif
