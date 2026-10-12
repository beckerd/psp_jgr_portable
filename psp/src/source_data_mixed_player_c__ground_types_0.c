/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char GROUND_TYPES[50] __asm__("jgr_data_mixed_player_c__ground_types_0_GROUND_TYPES") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1061232254,48,50,16,GROUND_TYPES},
};
int source_data_mixed_player_c__ground_types_0_load(void){return source_data_sections_load("source_data_mixed_player_c__ground_types_0.jsd",sections,1u,98u,0xc52992dbu);}
#endif
