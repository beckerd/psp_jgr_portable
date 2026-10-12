/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float PARA_AIR[5][5][3] __asm__("jgr_data_mixed_gang_c__para_air_0_PARA_AIR") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1918061484,48,300,16,PARA_AIR},
};
int source_data_mixed_gang_c__para_air_0_load(void){return source_data_sections_load("source_data_mixed_gang_c__para_air_0.jsd",sections,1u,348u,0xd908e928u);}
#endif
