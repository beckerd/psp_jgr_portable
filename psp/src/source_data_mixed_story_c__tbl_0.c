/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
int tbl[12] __asm__("jgr_data_mixed_story_c__tbl_0_tbl") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {825119362,48,48,16,tbl},
};
int source_data_mixed_story_c__tbl_0_load(void){return source_data_sections_load("source_data_mixed_story_c__tbl_0.jsd",sections,1u,96u,0x0c14fd75u);}
#endif
