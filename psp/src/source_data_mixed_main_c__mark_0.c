/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float MARK[24][2] __asm__("jgr_data_mixed_main_c__mark_0_MARK") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3429514529,48,192,16,MARK},
};
int source_data_mixed_main_c__mark_0_load(void){return source_data_sections_load("source_data_mixed_main_c__mark_0.jsd",sections,1u,240u,0x82640c3du);}
#endif
