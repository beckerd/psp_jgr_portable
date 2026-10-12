/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
float GAS_SPHERES[2][6][4] __asm__("jgr_data_enemy_gas_body_GAS_SPHERES") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1270862693,48,192,16,GAS_SPHERES},
};
int source_data_enemy_gas_body_load(void){return source_data_sections_load("source_data_enemy_gas_body.jsd",sections,1u,240u,0x64237230u);}
#endif
