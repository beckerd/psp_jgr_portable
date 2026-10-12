/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
unsigned char rival_graffiti_mask[6] __asm__("jgr_data_rival_graffiti_rival_graffiti_mask") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2494696916,48,6,16,rival_graffiti_mask},
};
int source_data_rival_graffiti_load(void){return source_data_sections_load("source_data_rival_graffiti.jsd",sections,1u,54u,0xcf60bef2u);}
#endif
