/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float keikan[2][3] __asm__("jgr_data_mixed_enemycombat_c__keikan_0_keikan") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {4180777113,48,24,16,keikan},
};
int source_data_mixed_enemycombat_c__keikan_0_load(void){return source_data_sections_load("source_data_mixed_enemycombat_c__keikan_0.jsd",sections,1u,72u,0x0eca882eu);}
#endif
