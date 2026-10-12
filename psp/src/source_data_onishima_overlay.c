/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
signed char overlays[2][3] __asm__("jgr_data_onishima_overlay_overlays") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1163904383,48,6,16,overlays},
};
int source_data_onishima_overlay_load(void){return source_data_sections_load("source_data_onishima_overlay.jsd",sections,1u,54u,0x59ae9ff4u);}
#endif
