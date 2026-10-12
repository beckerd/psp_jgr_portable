/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char MARK_MODEL[24] __asm__("jgr_data_main_mark_model_MARK_MODEL") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1732972259,48,24,16,MARK_MODEL},
};
int source_data_main_mark_model_load(void){return source_data_sections_load("source_data_main_mark_model.jsd",sections,1u,72u,0xdbd3dcbeu);}
#endif
