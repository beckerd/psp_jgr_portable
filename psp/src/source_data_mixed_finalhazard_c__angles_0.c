/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
int angles[6] __asm__("jgr_data_mixed_finalhazard_c__angles_0_angles") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2444709517,48,24,16,angles},
};
int source_data_mixed_finalhazard_c__angles_0_load(void){return source_data_sections_load("source_data_mixed_finalhazard_c__angles_0.jsd",sections,1u,72u,0xf179972bu);}
#endif
