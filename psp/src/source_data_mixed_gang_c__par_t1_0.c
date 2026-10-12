/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float PAR_T1[24] __asm__("jgr_data_mixed_gang_c__par_t1_0_PAR_T1") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {320596876,48,96,16,PAR_T1},
};
int source_data_mixed_gang_c__par_t1_0_load(void){return source_data_sections_load("source_data_mixed_gang_c__par_t1_0.jsd",sections,1u,144u,0x71b56a7au);}
#endif
