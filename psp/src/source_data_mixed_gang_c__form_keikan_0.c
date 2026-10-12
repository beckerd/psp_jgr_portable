/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float FORM_KEIKAN[5][5][3] __asm__("jgr_data_mixed_gang_c__form_keikan_0_FORM_KEIKAN") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {327164390,48,300,16,FORM_KEIKAN},
};
int source_data_mixed_gang_c__form_keikan_0_load(void){return source_data_sections_load("source_data_mixed_gang_c__form_keikan_0.jsd",sections,1u,348u,0x2d889f74u);}
#endif
