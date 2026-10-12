/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float targets[4][2] __asm__("jgr_data_mixed_finalhazard_c__targets_0_targets") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1713272165,48,32,16,targets},
};
int source_data_mixed_finalhazard_c__targets_0_load(void){return source_data_sections_load("source_data_mixed_finalhazard_c__targets_0.jsd",sections,1u,80u,0x24011041u);}
#endif
