/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
signed char raw[18] __asm__("jgr_data_onishima_primary_raw") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1712388616,48,18,16,raw},
};
int source_data_onishima_primary_load(void){return source_data_sections_load("source_data_onishima_primary.jsd",sections,1u,66u,0x0276ed39u);}
#endif
