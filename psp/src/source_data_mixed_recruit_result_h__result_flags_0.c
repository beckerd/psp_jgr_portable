/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char RESULT_FLAGS[6][9] __asm__("jgr_data_mixed_recruit_result_h__result_flags_0_RESULT_FLAGS") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1780342995,48,54,16,RESULT_FLAGS},
};
int source_data_mixed_recruit_result_h__result_flags_0_load(void){return source_data_sections_load("source_data_mixed_recruit_result_h__result_flags_0.jsd",sections,1u,102u,0xbf18a441u);}
#endif
