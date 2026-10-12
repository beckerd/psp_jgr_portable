#ifndef JGR_CITY_PROFILE_H
#define JGR_CITY_PROFILE_H

typedef enum CityPhase {
    CITY_SETUP,
    CITY_SOURCE,
    CITY_FRUSTUM,
    CITY_OCCLUSION,
    CITY_TRAVERSAL,
    CITY_MATERIAL,
    CITY_GEOMETRY,
    CITY_INDICES,
    CITY_SUBMISSION,
    CITY_INSTANCE,
    CITY_PHASE_COUNT,
    CITY_INACTIVE=255
} CityPhase;

typedef struct {
    unsigned enabled,prepare_spans,draw_spans,prepare_us,draw_us,clock_reads;
    unsigned phase_us[CITY_PHASE_COUNT],phase_calls[CITY_PHASE_COUNT];
    unsigned errors;
} CityTiming;

typedef struct {
    CityTiming timing;
    unsigned span,phase,start,last;
} CityProfiler;

/* Timestamp injection keeps this core independent of the PSP clock and free
   of I/O/allocation. Callers read the clock only for an enabled, valid state
   transition; an unchanged phase requires neither a read nor a switch.

   Span 0 is preparation, span 1 is drawing. Phases are exclusive: callers
   may save switch's return value and restore it after nested work. Closing
   a span pauses accounting until another open, excluding intervening work.
   All differences use unsigned subtraction, including timer rollover.

   Invalid spans/phases, nested opens, and inactive switches/closes increment
   errors without modifying valid accounting or consuming a clock read.
   Disabled calls are inert. Reset discards any unfinished span; it starts a
   fresh sample and does not need a timestamp. */
static inline void city_profile_reset(CityProfiler *p,int enabled)
{
    *p=(CityProfiler){.timing={.enabled=!!enabled},.span=CITY_INACTIVE,.phase=CITY_INACTIVE};
}

static inline void city_profile_open(CityProfiler *p,unsigned span,unsigned now)
{
    if(!p->timing.enabled)return;
    if(span>1 || p->span!=CITY_INACTIVE){p->timing.errors++;return;}
    p->span=span;p->phase=span ? CITY_TRAVERSAL:CITY_SETUP;
    p->start=p->last=now;
    if(span)p->timing.draw_spans++;else p->timing.prepare_spans++;
    p->timing.phase_calls[p->phase]++;p->timing.clock_reads++;
}

static inline unsigned city_profile_switch(CityProfiler *p,unsigned phase,unsigned now)
{
    unsigned old=p->phase;
    if(!p->timing.enabled)return old;
    if(p->span==CITY_INACTIVE || phase>=CITY_PHASE_COUNT){p->timing.errors++;return old;}
    if(phase==old)return old;
    p->timing.phase_us[old]+=now-p->last;
    p->phase=phase;p->last=now;
    p->timing.phase_calls[phase]++;p->timing.clock_reads++;
    return old;
}

static inline void city_profile_close(CityProfiler *p,unsigned now)
{
    if(!p->timing.enabled)return;
    if(p->span==CITY_INACTIVE){p->timing.errors++;return;}
    p->timing.phase_us[p->phase]+=now-p->last;
    if(p->span)p->timing.draw_us+=now-p->start;
    else p->timing.prepare_us+=now-p->start;
    p->last=now;p->span=p->phase=CITY_INACTIVE;p->timing.clock_reads++;
}

#endif
