/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float positions[6][3] __asm__("jgr_data_mixed_finalhazard_c__positions_0_positions") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2202127871,48,72,16,positions},
};
int source_data_mixed_finalhazard_c__positions_0_load(void){return source_data_sections_load("source_data_mixed_finalhazard_c__positions_0.jsd",sections,1u,120u,0xe6e66cd5u);}
#endif
