#ifndef JGR_RECRUIT_AUDIO_H
#define JGR_RECRUIT_AUDIO_H
#include "player_sound.h"
#include <stddef.h>
#include <string.h>
typedef struct {unsigned short tick;unsigned char kind;signed char program;} RecruitAudioCue;
typedef struct {unsigned next,tick;int started;} RecruitAudio;
/* Completed source +294/+29c/+2a0/+2b8 and +560 model publication.
   Constant state/linear clock runs avoid another per-frame route payload. */
typedef struct {
    unsigned short tick,ref;
    unsigned char active,variant;
    float frame,target[3];
} RecruitSprayCue;
typedef struct {
    const char *mission,*path,*label;
    unsigned set,character;
    const RecruitAudioCue *cues;
    unsigned cue_count;
    const RecruitSprayCue *sprays;
    unsigned spray_count;
} RecruitDemo;
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_challenge_demo_data.inc"
#else
#include "generated/source_data/challenge_demo_data.inc"
#endif
static inline const RecruitDemo *recruit_demo_find(const char *mission,unsigned set)
{
    if(!mission)return NULL;
    for(unsigned i=0;i<sizeof RECRUIT_DEMOS/sizeof *RECRUIT_DEMOS;i++)
        if(RECRUIT_DEMOS[i].set==set && !strcmp(RECRUIT_DEMOS[i].mission,mission))return RECRUIT_DEMOS+i;
    return NULL;
}
/* Called only on completed 30 Hz simulation samples, never interpolated
   draws. Catch up once after skipped samples; pauses/repeated draws cannot
   duplicate cues. Mission teardown retires audio owners and this cursor. */
static inline void recruit_audio_tick(RecruitAudio *s,const RecruitDemo *demo,unsigned tick,
    void (*emit)(void *,int,int),void *context)
{
    if(!demo || !emit)return;
    const RecruitAudioCue *cues=demo->cues;
    unsigned count=demo->cue_count;
    if(s->started && tick<=s->tick)return;
    s->started=1;s->tick=tick;
    while(s->next<count && cues[s->next].tick<=tick){
        const RecruitAudioCue *cue=cues+s->next++;
        emit(context,cue->kind,cue->program);
    }
}
#endif
