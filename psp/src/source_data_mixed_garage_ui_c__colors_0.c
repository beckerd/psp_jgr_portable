/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned colors[3] __asm__("jgr_data_mixed_garage_ui_c__colors_0_colors") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2540667875,48,12,16,colors},
};
int source_data_mixed_garage_ui_c__colors_0_load(void){return source_data_sections_load("source_data_mixed_garage_ui_c__colors_0.jsd",sections,1u,60u,0x5ffdcf88u);}
#endif
