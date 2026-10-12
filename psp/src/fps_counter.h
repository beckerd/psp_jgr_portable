#ifndef JGR_FPS_COUNTER_H
#define JGR_FPS_COUNTER_H
#include <stdint.h>

/* Presented frames per elapsed second, independent of simulation ticks and
   the 60-frame logging window. Unsigned subtraction survives timer wrap. */
typedef struct {
    uint32_t start, frames, tenths;
    int started, ready;
} FpsCounter;

/* Called after entering VBlank. Never burst to catch up after a slow frame. */
static inline int frame_pacer_due(uint32_t last,uint32_t now,int started)
{ return !started || (uint32_t)(now-last)>=2u; }

static inline int fps_counter_present(FpsCounter *s, uint32_t now)
{
    if (!s->started) { s->started=1; s->start=now; return 0; }
    s->frames++;
    uint32_t elapsed=now-s->start;
    if (elapsed<500000u) return 0;
    s->tenths=(uint32_t)(((uint64_t)s->frames*10000000u+elapsed/2)/elapsed);
    s->start=now; s->frames=0; s->ready=1;
    return 1;
}
#endif
