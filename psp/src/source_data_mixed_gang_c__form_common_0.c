/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float FORM_COMMON[5][5][3] __asm__("jgr_data_mixed_gang_c__form_common_0_FORM_COMMON") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3437176354,48,300,16,FORM_COMMON},
};
int source_data_mixed_gang_c__form_common_0_load(void){return source_data_sections_load("source_data_mixed_gang_c__form_common_0.jsd",sections,1u,348u,0xef347fdfu);}
#endif
