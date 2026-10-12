/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned texmap[8] __asm__("jgr_data_final_emitter_texmap_texmap") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {269067091,48,32,16,texmap},
};
int source_data_final_emitter_texmap_load(void){return source_data_sections_load("source_data_final_emitter_texmap.jsd",sections,1u,80u,0x7d02c9c8u);}
#endif
