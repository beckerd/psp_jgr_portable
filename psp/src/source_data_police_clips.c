/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char clips[20] __asm__("jgr_data_police_clips_clips") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1743001307,48,20,16,clips},
};
int source_data_police_clips_load(void){return source_data_sections_load("source_data_police_clips.jsd",sections,1u,68u,0x9d1f6359u);}
#endif
