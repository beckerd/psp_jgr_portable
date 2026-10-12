/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float x[3][3] __asm__("jgr_data_mixed_main_c__x_0_x") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {372638197,48,36,16,x},
};
int source_data_mixed_main_c__x_0_load(void){return source_data_sections_load("source_data_mixed_main_c__x_0.jsd",sections,1u,84u,0xd2786874u);}
#endif
