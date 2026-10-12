/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float position[3] __asm__("jgr_data_mixed_finalarena_c__position_0_position") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3521425389,48,12,16,position},
};
int source_data_mixed_finalarena_c__position_0_load(void){return source_data_sections_load("source_data_mixed_finalarena_c__position_0.jsd",sections,1u,60u,0x636c3420u);}
#endif
