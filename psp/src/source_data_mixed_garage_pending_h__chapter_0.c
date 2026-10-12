/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char chapter[10] __asm__("jgr_data_mixed_garage_pending_h__chapter_0_chapter") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {617355768,48,10,16,chapter},
};
int source_data_mixed_garage_pending_h__chapter_0_load(void){return source_data_sections_load("source_data_mixed_garage_pending_h__chapter_0.jsd",sections,1u,58u,0x602bd7d9u);}
#endif
