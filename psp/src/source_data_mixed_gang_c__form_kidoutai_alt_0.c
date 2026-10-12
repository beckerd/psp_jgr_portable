/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float FORM_KIDOUTAI_ALT[5][5][3] __asm__("jgr_data_mixed_gang_c__form_kidoutai_alt_0_FORM_KIDOUTAI_ALT") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {271708172,48,300,16,FORM_KIDOUTAI_ALT},
};
int source_data_mixed_gang_c__form_kidoutai_alt_0_load(void){return source_data_sections_load("source_data_mixed_gang_c__form_kidoutai_alt_0.jsd",sections,1u,348u,0xcd02aca4u);}
#endif
