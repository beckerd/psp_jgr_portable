/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
int start[4] __asm__("jgr_data_mixed_hud_state_c__start_0_start") __attribute__((aligned(16)));
int cue[4] __asm__("jgr_data_mixed_hud_state_c__start_0_cue") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1650071132,64,16,16,start},
    {1687408292,80,16,16,cue},
};
int source_data_mixed_hud_state_c__start_0_load(void){return source_data_sections_load("source_data_mixed_hud_state_c__start_0.jsd",sections,2u,96u,0x265f7326u);}
#endif
