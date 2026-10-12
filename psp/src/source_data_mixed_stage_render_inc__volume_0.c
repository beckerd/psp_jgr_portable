/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float volume[36] __asm__("jgr_data_mixed_stage_render_inc__volume_0_volume") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {968048983,48,144,16,volume},
};
int source_data_mixed_stage_render_inc__volume_0_load(void){return source_data_sections_load("source_data_mixed_stage_render_inc__volume_0.jsd",sections,1u,192u,0x158367f1u);}
#endif
