#ifndef JGR_SMSB_H
#define JGR_SMSB_H
#include "osb.h"
#include "audio_fade.h"
/* Bounded original final-bank transport. The original streams and patch
   layers execute live; no rendered recording or authored replacement SFX.
   DSP/filter/LFO and shared Dreamcast voice-stealing policy are separate. */
#define SMSB_VOICES 16
/* Original SMPB metadata stays contiguous; immutable waves may occupy
   separate heap spans. Only large waves need a page table during decoding. */
typedef struct SmsbWave {
    struct SmsbWave *next;
    unsigned offset,bytes;
    unsigned char *samples;
    unsigned char **pages;
} SmsbWave;
typedef struct {
    unsigned char *metadata;
    unsigned metadata_size,image_size;
    SmsbWave *waves;
} SmsbPatchBank;
typedef struct {
    OsbProgram sample;
    unsigned env1,env2,level,total,layer;
} SmsbPatch;
typedef struct {
    int32_t value;unsigned state,attack,decay1,decay2,release,threshold,link;
} SmsbEnvelope;
typedef struct {
    unsigned program,volume,bank;int bend;
} SmsbChannel;
typedef struct {
    unsigned pc,limit,quantum,base,clock,due,program,volume,bank;
    int active,error,ended;
    SmsbChannel channels[16];
    unsigned origin,loop_pc,loop_left,return_pc,return_count,delay_extra,gate_extra;
} SmsbStream;
typedef struct {
    OsbVoice sample;SmsbEnvelope envelope;
    unsigned owner,key,off_clock,level,total;int active;
    unsigned program,velocity,volume,layer,channel;
} SmsbVoice;
typedef struct {
    const unsigned char *sequence,*patch;unsigned sequence_size,patch_size;
    const SmsbPatchBank *patch_parts;
    SmsbStream streams[2];SmsbVoice voices[SMSB_VOICES];
    unsigned sample_clock,irq,divider,skips,notes,errors,patch_bank,allowed_programs;
    int global_volume[2],global_pan[2];
    SourceAudioRamp volume_ramp[2],pan_ramp[2];
    int dsp_input; /* current sample's bus 0, consumed by original reverb1 */
} Smsb;
int smsb_bind(Smsb *,const void *sequence,unsigned sequence_size,const void *patch,unsigned patch_size);
int smsb_bind_parts(Smsb *,const void *sequence,unsigned sequence_size,const SmsbPatchBank *);
int smsb_bind_menu(Smsb *,const void *sequence,unsigned sequence_size,const void *patch,unsigned patch_size);
int smsb_bind_world(Smsb *,const void *sequence,unsigned sequence_size,const void *patch,unsigned patch_size);
int smsb_start(Smsb *,unsigned slot,unsigned program);
void smsb_stop(Smsb *,unsigned slot);
void smsb_release(Smsb *,unsigned slot); /* 29ed3c: retire the channel, not just note-off */
void smsb_volume(Smsb *,unsigned slot,int volume);
void smsb_pan(Smsb *,unsigned slot,int pan);
int smsb_fade_volume(Smsb *,unsigned slot,int volume,int duration);
int smsb_fade_pan(Smsb *,unsigned slot,int pan,int duration);
void smsb_service(Smsb *); /* one original SH-4 driver service; separate from ARM IRQ */
void smsb_next(Smsb *,int *left,int *right);
/* Same timer/note/envelope sequence, with voice loops between IRQ boundaries.
   Each frame is left, right, DSP bus 0. Caller owns frames*3 integers. */
void smsb_next_buffer(Smsb *,int (*out)[3],unsigned frames);
/* Driver 0e8c/1c4c layer resolution, exposed for reference-register tests. */
int smsb_patch(const void *,unsigned size,unsigned program,unsigned key,unsigned velocity,unsigned volume,
               unsigned global_volume,SmsbPatch out[4]);
void smsb_envelope_start(SmsbEnvelope *,unsigned env1,unsigned env2,unsigned pitch);
unsigned smsb_envelope_tick(SmsbEnvelope *);
#endif
