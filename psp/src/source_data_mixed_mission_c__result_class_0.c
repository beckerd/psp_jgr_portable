/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char RESULT_CLASS[10] __asm__("jgr_data_mixed_mission_c__result_class_0_RESULT_CLASS") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3523289862,48,10,16,RESULT_CLASS},
};
int source_data_mixed_mission_c__result_class_0_load(void){return source_data_sections_load("source_data_mixed_mission_c__result_class_0.jsd",sections,1u,58u,0xa7d14d5eu);}
#endif
