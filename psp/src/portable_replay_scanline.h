#ifndef JGR_PORTABLE_REPLAY_SCANLINE_H
#define JGR_PORTABLE_REPLAY_SCANLINE_H
#include <stdint.h>

/* Advance only scanlines preceding the next observable interrupt or external
 * deadline. The caller still services that boundary normally, including
 * simultaneous video/audio events. This is used only while executing a
 * verified idle wait, never while game code can read scanline registers. */
static inline uint64_t portable_replay_scanline_idle(uint64_t *next_line,
    uint64_t *completed,unsigned period,unsigned first_irq,unsigned second_irq,
    uint64_t external_deadline)
{
    const unsigned ticks=3175;
    if(!period || period>1024)return *next_line;
    unsigned first=(unsigned)((*completed+1)%period),distance=period;
    if(first_irq<period)distance=(first_irq+period-first)%period;
    if(second_irq<period){unsigned other=(second_irq+period-first)%period;if(other<distance)distance=other;}
    uint64_t delta=(uint64_t)distance*ticks;
    if(*next_line>UINT64_MAX-delta)return *next_line;
    uint64_t event=*next_line+delta;
    if(external_deadline<event)event=external_deadline;
    if(event>*next_line){
        delta=event-*next_line;
        unsigned silent=(unsigned)(delta/ticks+(delta%ticks!=0));
        *completed+=silent;*next_line+=(uint64_t)silent*ticks;
    }
    return event;
}
#endif
