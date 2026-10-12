/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float small[6][4] __asm__("jgr_data_mixed_object_view_h__small_0_small") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3251362201,48,96,16,small},
};
int source_data_mixed_object_view_h__small_0_load(void){return source_data_sections_load("source_data_mixed_object_view_h__small_0.jsd",sections,1u,144u,0xe42e1ae0u);}
#endif
