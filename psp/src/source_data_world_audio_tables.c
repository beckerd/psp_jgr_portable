/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
short WORLD_NPC_REACTION[76] __asm__("jgr_data_world_audio_tables_WORLD_NPC_REACTION") __attribute__((aligned(16)));
unsigned char WORLD_SEQUENCE_CHANNEL[17] __asm__("jgr_data_world_audio_tables_WORLD_SEQUENCE_CHANNEL") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {207696447,64,152,16,WORLD_NPC_REACTION},
    {1192371442,224,17,16,WORLD_SEQUENCE_CHANNEL},
};
int source_data_world_audio_tables_load(void){return source_data_sections_load("source_data_world_audio_tables.jsd",sections,2u,241u,0x29cd3abdu);}
#endif
