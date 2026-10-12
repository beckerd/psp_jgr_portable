/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char voice[13] __asm__("jgr_data_mixed_garage_pending_h__voice_0_voice") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2860523576,48,13,16,voice},
};
int source_data_mixed_garage_pending_h__voice_0_load(void){return source_data_sections_load("source_data_mixed_garage_pending_h__voice_0.jsd",sections,1u,61u,0x93783c0bu);}
#endif
