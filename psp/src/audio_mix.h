#ifndef JGR_AUDIO_MIX_H
#define JGR_AUDIO_MIX_H
#include "audio_spatial.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_audio_mix_tables.inc"
#else
#include "generated/source_data/audio_mix_tables.inc"
#endif
typedef struct {int selector,sosb_max,sosb_min,adx_max,adx_min,bgm,radio,dj;} SourceAudioMix;
/* 0b23c4 writes seven independent globals; a balance control, not master
   volume. Range validation is the port's API boundary, outside the kernel. */
static inline SourceAudioMix source_audio_mix(int selector)
{
    if(selector<0)selector=0;if(selector>16)selector=16;
    const int *r=AUDIO_MIX_TABLE[selector];
    return (SourceAudioMix){selector,r[0],r[1],r[2],-280,r[3],r[4],r[4]};
}
/* 0e86cc uses the stream class from 02eabc, NOT the mission's music mode
   or the current ADX filename. Intro/result/garage classes bypass balance. */
static inline void source_audio_stream_mix(const SourceAudioMix *m,int mode,int *bgm,int *speech)
{
    int balanced=(mode>=2 && mode<=13)||(mode>=16 && mode<=25)||(mode>=28 && mode<=32);
    *bgm=balanced ? m->bgm:-50;*speech=balanced ? m->radio:0;
}
static inline int source_audio_bgm_class(int entry)
{return (unsigned)entry<190 ? AUDIO_BGM_CLASS[entry]:-1;}
static inline void source_audio_mix_context(const SourceAudioMix *m,SourceAudioContext *c)
{
    c->scene=m->selector;c->sosb_min=m->sosb_min;c->sosb_max=m->sosb_max;
    c->adx_min=m->adx_min;c->adx_max=m->adx_max;
}
#endif
