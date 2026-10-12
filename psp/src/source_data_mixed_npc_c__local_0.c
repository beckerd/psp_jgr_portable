/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float local[3] __asm__("jgr_data_mixed_npc_c__local_0_local") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2457934331,48,12,16,local},
};
int source_data_mixed_npc_c__local_0_load(void){return source_data_sections_load("source_data_mixed_npc_c__local_0.jsd",sections,1u,60u,0x7e39fae3u);}
#endif
