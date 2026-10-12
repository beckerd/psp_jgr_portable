/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned table[9] __asm__("jgr_data_mixed_collision_source_h__table_0_table") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2036619619,48,36,16,table},
};
int source_data_mixed_collision_source_h__table_0_load(void){return source_data_sections_load("source_data_mixed_collision_source_h__table_0.jsd",sections,1u,84u,0x95c5fa14u);}
#endif
