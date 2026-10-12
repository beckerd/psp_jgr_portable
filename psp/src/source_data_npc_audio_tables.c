/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {float speed;int16_t reference;uint16_t period;} NpcAudioRoute;
typedef struct {int16_t reference,reaction;uint32_t period;} NpcAudioType;

#include "source_data_sections.h"
NpcAudioType NPC_AUDIO_TYPES[76] __asm__("jgr_data_npc_audio_tables_NPC_AUDIO_TYPES") __attribute__((aligned(16)));
NpcAudioRoute NPC_AUDIO_ROUTES[25] __asm__("jgr_data_npc_audio_tables_NPC_AUDIO_ROUTES") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2831069913,64,608,16,NPC_AUDIO_TYPES},
    {3499898524,672,200,16,NPC_AUDIO_ROUTES},
};
int source_data_npc_audio_tables_load(void){return source_data_sections_load("source_data_npc_audio_tables.jsd",sections,2u,872u,0x1377b720u);}
#endif
