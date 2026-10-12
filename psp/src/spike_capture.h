#ifndef JGR_SPIKE_CAPTURE_H
#define JGR_SPIKE_CAPTURE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "city_profile.h"

#define SPIKE_CAPTURE_HISTORY 32u
#define SPIKE_CAPTURE_WARMUP 120u
#define SPIKE_CAPTURE_THRESHOLD_US 50000u
#define SPIKE_CAPTURE_HOME_BUTTON 0x010000u

/* HOME can be sampled before submission, during the overlapping logic, or
   immediately before capture. Reject all three, plus exit/suspend requests,
   without treating ordinary game controls as a diagnostic exclusion. */
static inline int spike_capture_context_valid(int running,int suspended,
    unsigned submitted_buttons,unsigned overlap_buttons,unsigned latest_buttons)
{
    return running && !suspended &&
        !((submitted_buttons|overlap_buttons|latest_buttons)&SPIKE_CAPTURE_HOME_BUTTON);
}

/* Context belongs to the displayed frame. Overlapping logic can already
   belong to the following frame when that display's GE fence completes.
   present_us is the raw swap-to-swap interval, including log_us; reporting
   logging separately must not silently suppress a real presentation stall. */
typedef struct {
    unsigned frame,overlap_frame,generation,eligible;
    unsigned present_us,logic_us,submit_us,ge_us,vblank_us,log_us;
    unsigned list_bytes,draws,stage_draws,stage_tris;
    unsigned city_us[CITY_PHASE_COUNT],city_clock_reads;
    int stage,mission,zone;
    float player[3],eye[3];
} SpikeFrame;

typedef struct {
    unsigned enabled,armed_streak,generation,fired,count,next;
    SpikeFrame history[SPIKE_CAPTURE_HISTORY];
} SpikeCapture;

/* All measured spans are much shorter than the 32-bit microsecond clock's
   full period. Conversion after subtraction also makes host tests portable. */
static inline uint32_t spike_capture_elapsed(uint32_t now,uint32_t then)
{return (uint32_t)(now-then);}

/* Explicit initialization starts a new session and clears the one-shot
   latch. A stage, eligibility or suspend change must not clear that latch. */
static inline void spike_capture_init(SpikeCapture *capture,int enabled)
{
    if(!capture)return;
    memset(capture,0,sizeof *capture);
    capture->enabled=enabled!=0;
}

/* Invalid completions break the consecutive gameplay streak and discard
   older scene history. A fired capture remains immutable until init. */
static inline int spike_capture_observe(SpikeCapture *capture,const SpikeFrame *frame)
{
    if(!capture || !frame || !capture->enabled || capture->fired)return 0;
    if(!frame->eligible) {
        capture->armed_streak=capture->count=capture->next=0;
        capture->generation=frame->generation;
        return 0;
    }
    if(capture->armed_streak && capture->generation!=frame->generation)
        capture->armed_streak=capture->count=capture->next=0;
    capture->generation=frame->generation;
    capture->history[capture->next]=*frame;
    capture->next=(capture->next+1u)%SPIKE_CAPTURE_HISTORY;
    if(capture->count<SPIKE_CAPTURE_HISTORY)capture->count++;
    if(capture->armed_streak<SPIKE_CAPTURE_WARMUP)capture->armed_streak++;
    if(capture->armed_streak<SPIKE_CAPTURE_WARMUP ||
       frame->present_us<SPIKE_CAPTURE_THRESHOLD_US)return 0;
    capture->fired=1;
    return 1;
}

/* Enumerate retained samples oldest first. The last sample is the trigger
   after firing; pointers remain valid while the capture is frozen. */
static inline const SpikeFrame *spike_capture_history(const SpikeCapture *capture,unsigned index)
{
    if(!capture || index>=capture->count)return NULL;
    unsigned first=(capture->next+SPIKE_CAPTURE_HISTORY-capture->count)%SPIKE_CAPTURE_HISTORY;
    return &capture->history[(first+index)%SPIKE_CAPTURE_HISTORY];
}

#endif
