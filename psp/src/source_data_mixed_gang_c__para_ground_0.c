/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float PARA_GROUND[5][5][3] __asm__("jgr_data_mixed_gang_c__para_ground_0_PARA_GROUND") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1652654018,48,300,16,PARA_GROUND},
};
int source_data_mixed_gang_c__para_ground_0_load(void){return source_data_sections_load("source_data_mixed_gang_c__para_ground_0.jsd",sections,1u,348u,0x770110beu);}
#endif
