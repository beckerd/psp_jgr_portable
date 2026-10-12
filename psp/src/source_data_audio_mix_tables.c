/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
int AUDIO_MIX_TABLE[17][5] __asm__("jgr_data_audio_mix_tables_AUDIO_MIX_TABLE") __attribute__((aligned(16)));
signed char AUDIO_BGM_CLASS[190] __asm__("jgr_data_audio_mix_tables_AUDIO_BGM_CLASS") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1340206609,64,340,16,AUDIO_MIX_TABLE},
    {2874545730,416,190,16,AUDIO_BGM_CLASS},
};
int source_data_audio_mix_tables_load(void){return source_data_sections_load("source_data_audio_mix_tables.jsd",sections,2u,606u,0x84870042u);}
#endif
