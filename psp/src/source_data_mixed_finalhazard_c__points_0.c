/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float points[2][3] __asm__("jgr_data_mixed_finalhazard_c__points_0_points") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3454802619,48,24,16,points},
};
int source_data_mixed_finalhazard_c__points_0_load(void){return source_data_sections_load("source_data_mixed_finalhazard_c__points_0.jsd",sections,1u,72u,0xac164487u);}
#endif
