/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned seeds[3] __asm__("jgr_data_mixed_stage_render_inc__seeds_0_seeds") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1875141405,48,12,16,seeds},
};
int source_data_mixed_stage_render_inc__seeds_0_load(void){return source_data_sections_load("source_data_mixed_stage_render_inc__seeds_0.jsd",sections,1u,60u,0xb1eff4bfu);}
#endif
