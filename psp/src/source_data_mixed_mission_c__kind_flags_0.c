/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char KIND_FLAGS[6][9] __asm__("jgr_data_mixed_mission_c__kind_flags_0_KIND_FLAGS") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {384325186,48,54,16,KIND_FLAGS},
};
int source_data_mixed_mission_c__kind_flags_0_load(void){return source_data_sections_load("source_data_mixed_mission_c__kind_flags_0.jsd",sections,1u,102u,0xc6819497u);}
#endif
