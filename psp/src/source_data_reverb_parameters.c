/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
int source_reverb_coefficient[53] __asm__("jgr_data_reverb_parameters_source_reverb_coefficient") __attribute__((aligned(16)));
int source_reverb_delay[33] __asm__("jgr_data_reverb_parameters_source_reverb_delay") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {141656447,64,212,16,source_reverb_coefficient},
    {2395339413,288,132,16,source_reverb_delay},
};
int source_data_reverb_parameters_load(void){return source_data_sections_load("source_data_reverb_parameters.jsd",sections,2u,420u,0x464faab8u);}
#endif
