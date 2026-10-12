/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float cue_x[3][3] __asm__("jgr_data_mixed_spray_score_h__cue_x_0_cue_x") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1810329928,48,36,16,cue_x},
};
int source_data_mixed_spray_score_h__cue_x_0_load(void){return source_data_sections_load("source_data_mixed_spray_score_h__cue_x_0.jsd",sections,1u,84u,0xb4248165u);}
#endif
