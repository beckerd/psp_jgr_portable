#include "spray.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_spraytab.inc"
#else
#include "generated/source_data/spraytab.h"
#endif

int spray_delta(int from, int to)
{
    int d = (to & 65535) - (from & 65535);
    if (d > 32767) d -= 65536;
    if (d < -32768) d += 65536;
    return d;
}
int spray_cue(const SprayInput *s)
{
    if (!s || (unsigned)s->skill >= 3 || (unsigned)s->pattern >= 7 ||
        (unsigned)s->cue >= (unsigned)PATROWS[s->skill*7+s->pattern][3]) return 0;
    return PATROWS[s->skill*7+s->pattern][s->cue];
}
int spray_evaluate(SprayInput *s, int angle, float magnitude, SprayEvents *events)
{
    int id = spray_cue(s);
    if (events) events->animate = events->sound = 0;
    if (!id) return SPRAY_MISS; /* Safety guard; original assumes valid task fields. */
    const int *c = CUES[id];
    int prev = s->previous_angle, direction = spray_delta(prev, angle);
    int bad = (c[6] == 1 && direction < 0) || (c[6] == 2 && direction > 0) || !(magnitude > 0.1f);
    if (!s->phase) {
        if (spray_delta(c[0],angle)>0 && spray_delta(c[1],angle)<0 &&
            magnitude>0.1f && !(s->previous_magnitude>0.1f) && !s->cue) s->bonus=50;
        if (!c[6]) {
            if (!(magnitude>0.8f) || s->previous_magnitude>0.8f ||
                spray_delta(c[0],prev)<=0 || spray_delta(c[1],prev)>=0) return SPRAY_ACTIVE;
            if (events) events->animate++;
            goto accepted;
        }
        if (spray_delta(c[0],prev)<=0 || spray_delta(c[1],prev)>=0 || !(magnitude>0.1f)) return SPRAY_WAIT;
        if (c[6]==1) {
            if (spray_delta(c[2],prev)>0 || spray_delta(c[2],angle)<0) return SPRAY_WAIT;
        } else {
            if (spray_delta(c[2],prev)<0 || spray_delta(c[2],angle)>0) return SPRAY_WAIT;
        }
        if (events) events->animate++;
        s->phase=1;
    } else if (s->phase!=1) return SPRAY_ACTIVE;
    if (bad) return SPRAY_MISS;
    if ((c[6]==1 && spray_delta(c[4],prev)<=0 && spray_delta(c[4],angle)>=0) ||
        (c[6]==2 && spray_delta(c[4],prev)>=0 && spray_delta(c[4],angle)<=0)) {
accepted:
        s->cue++; s->phase=0;
        if (events) events->sound++;
        if (s->cue>=PATROWS[s->skill*7+s->pattern][3]) return SPRAY_SUCCESS;
    }
    return SPRAY_ACTIVE;
}
int spray_evaluate_physical(SprayInput *s,int angle,float magnitude,SprayEvents *events)
{
    int cue=spray_cue(s),previous=s->previous_angle;
    /* A fast nub/D-pad push can jump from neutral to full throw between
       rendered frames. The source straight-arrow kernel tests the PREVIOUS
       direction at that edge and would discard it forever while held.
       Resolve a live straight edge with its own direction. Keep the 80%
       threshold, release-to-rearm rule and all curved gestures unchanged.
       Recorded Dreamcast controllers continue through spray_evaluate. */
    if(cue && !CUES[cue][6] && !s->phase && magnitude>.8f && !(s->previous_magnitude>.8f))
        s->previous_angle=angle;
    int result=spray_evaluate(s,angle,magnitude,events);
    s->previous_angle=previous;
    return result;
}
int spray_reveal_tick(uint32_t *stroke, uint32_t *progress, uint32_t *complete)
{
    *progress += 9;
    if (*progress < 288) return 0;
    ++*stroke; *progress=0; *complete=1;
    return 1;
}
void spray_large_reveal(float *clock,uint32_t *reveal,unsigned cues,float step)
{
    if(!cues || cues>3)return;
    /* Unsigned integer divide, FMUL, then FADD, not a contracted FMAC. */
    float advance=step*(float)(288u/cues);*clock=*clock+advance;
    uint32_t value=(uint32_t)*clock;if(value>*reveal)*reveal=value;
}
/* 018fb0 copies record +8 from pointer table 8c182884 into player +278.
   Values are checked against every original character record by test_spray. */
int spray_character_skill(int character)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_spray_c__skills_0.inc"
#else

#include "generated/source_data/spray_c__skills_0.inc"

#endif

    return (unsigned)character<16 ? skills[character] : 0;
}
