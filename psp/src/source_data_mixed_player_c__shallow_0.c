/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char shallow[17] __asm__("jgr_data_mixed_player_c__shallow_0_shallow") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3073779673,48,17,16,shallow},
};
int source_data_mixed_player_c__shallow_0_load(void){return source_data_sections_load("source_data_mixed_player_c__shallow_0.jsd",sections,1u,65u,0xcbf5c0d3u);}
#endif
