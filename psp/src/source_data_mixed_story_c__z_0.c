/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
int z[17] __asm__("jgr_data_mixed_story_c__z_0_z") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {715822054,48,68,16,z},
};
int source_data_mixed_story_c__z_0_load(void){return source_data_sections_load("source_data_mixed_story_c__z_0.jsd",sections,1u,116u,0x3de88ab1u);}
#endif
