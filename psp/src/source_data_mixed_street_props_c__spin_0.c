/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float spin[3] __asm__("jgr_data_mixed_street_props_c__spin_0_spin") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {773212419,48,12,16,spin},
};
int source_data_mixed_street_props_c__spin_0_load(void){return source_data_sections_load("source_data_mixed_street_props_c__spin_0.jsd",sections,1u,60u,0xbd8dd7feu);}
#endif
