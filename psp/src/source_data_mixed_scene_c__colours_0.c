/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
uint32_t colours[7] __asm__("jgr_data_mixed_scene_c__colours_0_colours") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3691619162,48,28,16,colours},
};
int source_data_mixed_scene_c__colours_0_load(void){return source_data_sections_load("source_data_mixed_scene_c__colours_0.jsd",sections,1u,76u,0xf5468c63u);}
#endif
