#ifndef JGR_EVENT_AUDIO_H
#define JGR_EVENT_AUDIO_H
#include "audio_spatial.h"
#define EVENT_AUDIO_HANDLES (30+30+32+16)
typedef struct {
    unsigned kind,bank,reference,file,program,backend,channel,logical_bank,patch_file,patch_bank;
    SourceAudioProfile profile;
} EventAudioCue;
int audio_load_event(int id);
/* Last load dependency/result survives rollback for the hardware log.
   Stage 0 means this bank load has not been attempted. */
typedef struct {int id,error;unsigned file,stage,largest;} EventAudioLoadInfo;
void audio_event_reset_load_info(int id);
EventAudioLoadInfo audio_event_load_info(void);
void audio_free_event(void);
int audio_event_voice(unsigned slot,unsigned kind,unsigned bank,unsigned reference);
void audio_event_stop(unsigned slot);
const EventAudioCue *audio_event_cue(unsigned slot);
void audio_event_levels(unsigned slot,int volume,int pan);
#endif
