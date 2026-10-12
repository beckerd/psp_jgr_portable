#ifndef JGR_AUDIO_JOB_H
#define JGR_AUDIO_JOB_H
#include "reverb_job.h"
#include "smsb.h"
#include "movie_voice.h"
#include "stream_decode.h"
#define AUDIO_JOB_OSB_MAX 40 /* 17 managed slots + 16 independent enemies, with margin */
#define AUDIO_JOB_SEQUENCES 12
#define AUDIO_JOB_SFX 6
#define AUDIO_JOB_MESSAGE_CHUNKS 114
#define AUDIO_JOB_BLOCK 64
/* Only immutable sample pointers cross cores. All decoder/sequence cursors,
   envelopes and output belong to this transaction until its reply commits. */
typedef struct { OsbVoice voice;SmsbEnvelope envelope;unsigned level,total; } AudioJobOsb;
typedef struct {int id;unsigned pos;float vol;int active;} AudioJobSfx;
typedef struct {
    AudioJobOsb osb[AUDIO_JOB_OSB_MAX],garage;
    Smsb *sequences; /* private arena, sized to the currently bound banks */
    MovieVoice movie,countdown,message;
    const unsigned char *message_chunks[AUDIO_JOB_MESSAGE_CHUNKS];
    AudioJobSfx sfx[AUDIO_JOB_SFX];
    const short *sfx_pcm[64],*final_pcm;unsigned sfx_len[64];
    unsigned osb_count,sequence_count,final_position,final_samples,final_gain[2];
    int final_active,paused,enabled,mono;
    float music_gain,speech_volume;
} AudioJobState;
typedef struct {
    ReverbStorage *storage;AudioJobState input,output;
    short music[REVERB_JOB_FRAMES][2],pcm[REVERB_JOB_FRAMES][2];
    StreamDecodeJob music_decode;
    int music_decode_history[2][2];
    unsigned count,bank,reset,serial,sequence_capacity;
} __attribute__((aligned(64))) AudioMixJob;
int audio_mix_job_execute(AudioMixJob *job);
int audio_mix_job_worker(int arg);
#endif
