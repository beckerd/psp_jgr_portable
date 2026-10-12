/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {int backend;float range,far_fraction,near_fraction;} SourceAudioProfile;
typedef struct {
    unsigned kind,bank,reference,file,program,backend,channel,logical_bank,patch_file,patch_bank;
    SourceAudioProfile profile;
} EventAudioCue;

#include "source_data_sections.h"
EventAudioCue ENEMY_AUDIO_CUES[220] __asm__("jgr_data_enemy_audio_tables_ENEMY_AUDIO_CUES") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3263352805,48,12320,16,ENEMY_AUDIO_CUES},
};
int source_data_enemy_audio_tables_load(void){return source_data_sections_load("source_data_enemy_audio_tables.jsd",sections,1u,12368u,0x2929324cu);}
#endif
