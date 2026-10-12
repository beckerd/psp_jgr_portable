/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
signed char scenes[15] __asm__("jgr_data_mixed_story_c__scenes_0_scenes") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3484220818,48,15,16,scenes},
};
int source_data_mixed_story_c__scenes_0_load(void){return source_data_sections_load("source_data_mixed_story_c__scenes_0.jsd",sections,1u,63u,0xa671f1eeu);}
#endif
