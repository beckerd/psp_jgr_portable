#ifndef JGR_COUNTDOWN_AUDIO_H
#define JGR_COUNTDOWN_AUDIO_H
#include "movie_voice.h"
#include <string.h>
/* 02d568 selects SND_DJTALK, independent of BGM but sharing task 298's
   +15c handle with 02d4c8 MUSEN. audio.c arbitrates those two consumers. */
typedef struct {MovieVoice cue[4],playing;} CountdownAudio;
static inline int countdown_audio_select(CountdownAudio *a,int reference)
{
    memset(&a->playing,0,sizeof a->playing);
    if(reference<0)return 0;
    int index=reference-179;
    if((unsigned)index>=4 || !a->cue[index].ready)return -1;
    a->playing=a->cue[index];a->playing.active=1;return 0;
}
#endif
