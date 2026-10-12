/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned tone[8] __asm__("jgr_data_mixed_graffiti_game_c__tone_0_tone") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {4085751469,48,32,16,tone},
};
int source_data_mixed_graffiti_game_c__tone_0_load(void){return source_data_sections_load("source_data_mixed_graffiti_game_c__tone_0.jsd",sections,1u,80u,0xb5099bb6u);}
#endif
