/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
int CHAR_HEALTH[16] __asm__("jgr_data_mixed_main_c__char_health_0_CHAR_HEALTH") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1325378319,48,64,16,CHAR_HEALTH},
};
int source_data_mixed_main_c__char_health_0_load(void){return source_data_sections_load("source_data_mixed_main_c__char_health_0.jsd",sections,1u,112u,0x6fa99288u);}
#endif
