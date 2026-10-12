/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float ROUTE_FORM[5][5][3] __asm__("jgr_data_mixed_gang_c__route_form_0_ROUTE_FORM") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {671178029,48,300,16,ROUTE_FORM},
};
int source_data_mixed_gang_c__route_form_0_load(void){return source_data_sections_load("source_data_mixed_gang_c__route_form_0.jsd",sections,1u,348u,0xb8b58097u);}
#endif
