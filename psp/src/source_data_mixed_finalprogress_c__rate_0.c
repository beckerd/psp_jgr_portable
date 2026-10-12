/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float rate[9] __asm__("jgr_data_mixed_finalprogress_c__rate_0_rate") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2040584574,48,36,16,rate},
};
int source_data_mixed_finalprogress_c__rate_0_load(void){return source_data_sections_load("source_data_mixed_finalprogress_c__rate_0.jsd",sections,1u,84u,0xef49e160u);}
#endif
