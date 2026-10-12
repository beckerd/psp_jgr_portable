#ifndef JGR_AUDIO_FADE_H
#define JGR_AUDIO_FADE_H
#include <stdint.h>
/* CRI 29fa70/29f908 and 2a2c54/2a2aec, helper2a3a24. The driver stores
   signed 8.8, divides by duration/16 (integer), and advances once in the
   SETTER before subsequent service ticks. This is not a float lerp. */
typedef struct {int target,current,step;} SourceAudioRamp;
static inline int source_audio_ramp_set(SourceAudioRamp *r,int value,int duration)
{
    /* The platform rejects the original divide-by-zero duration interval;
       none of the recovered callers requests it. Negative durations fail. */
    if(duration<0 || (duration>0 && duration<16))return -1;
    r->target=(int)(int8_t)value*256;
    r->step=duration ? (r->target-r->current)/(duration/16):0;
    if(!r->step)r->current=r->target;else r->current+=r->step;
    return 0;
}
static inline int source_audio_ramp_tick(SourceAudioRamp *r)
{
    int previous=r->current;
    if(r->step){
        r->current+=r->step;
        if((r->step<0 && r->current<=r->target)||(r->step>0 && r->current>=r->target)){
            r->current=r->target;r->step=0;
        }
    }
    /* 29f658 compares high bytes, then sends signed /256 (toward zero).
       Testing the rounded output alone changes negative-boundary timing. */
    return ((unsigned)previous&~255u)!=((unsigned)r->current&~255u);
}
static inline int source_audio_ramp_value(const SourceAudioRamp *r){return r->current/256;}
#endif
