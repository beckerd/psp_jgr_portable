/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char skills[16] __asm__("jgr_data_mixed_spray_c__skills_0_skills") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {843238150,48,16,16,skills},
};
int source_data_mixed_spray_c__skills_0_load(void){return source_data_sections_load("source_data_mixed_spray_c__skills_0.jsd",sections,1u,64u,0x1adb3401u);}
#endif
