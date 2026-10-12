#ifndef JGR_AUDIO_ME_H
#define JGR_AUDIO_ME_H
#include "audio.h"
#include "reverb_packed.h"
#include "audio_job.h"
extern ReverbStorage audio_reverb_storage;
void audio_me_init(void);
void audio_me_service_resume(void);
int audio_me_owns_state(void);
int audio_me_quarantined(void);
/* 1: wet output valid; 0: recovered CPU state, caller must process these
   sends once; -1: unfenced failed job, state quarantined and wet is silent. */
int audio_me_process(const int *sends,int (*wet)[2],unsigned count,int reset);
AudioMixJob *audio_me_mix_buffer(void);
int audio_me_full_mixer(void);
typedef struct {
    float dry[REVERB_JOB_FRAMES][2];int sends[REVERB_JOB_FRAMES],wet[REVERB_JOB_FRAMES][2];
} AudioReverbBuffer;
AudioReverbBuffer *audio_me_reverb_buffer(void);
int audio_me_reserve_mix(unsigned sequences);
void audio_me_release_idle_mix(void);
int audio_me_mix(unsigned count,int reset);
int audio_me_force_cpu(void);
void audio_me_collect(AudioMeProfile *out);
int audio_me_shutdown(void);
#endif
