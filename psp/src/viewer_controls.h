#ifndef JGR_VIEWER_CONTROLS_H
#define JGR_VIEWER_CONTROLS_H
#include <pspctrl.h>
#include "diagnostics.h"
typedef struct {
    unsigned started;
    int held, consumed, stats;
} ViewerSelect;

/* Physical wall clock: the three-second hold must not depend on FPS or
   scripted demo time. A short tap is deferred until release so a stats
   hold never also pauses, changes camera, or opens a menu. */
static inline int viewer_select(ViewerSelect *s,unsigned buttons,unsigned now)
{
    int held=(buttons&PSP_CTRL_SELECT)!=0;
    if(held && !s->held){s->started=now;s->consumed=0;}
    if((held || s->held) && !s->consumed && now-s->started>=3000000u) {
        s->stats^=1;s->consumed=1;
    }
    int tap=!held && s->held && !s->consumed;
    s->held=held;
    return tap;
}

/* Normal/release builds share Start's existing actions in gameplay and
   menus. Profiling builds retain Select's inspection shortcuts. Physical
   Start and all other button edges keep their ordinary timing. */
static inline unsigned viewer_select_pressed(ViewerSelect *s,unsigned buttons,
                                             unsigned pressed,unsigned now)
{
    pressed&=~PSP_CTRL_SELECT;
    if(viewer_select(s,buttons,now))
        pressed|=JGR_DIAGNOSTICS ? PSP_CTRL_SELECT:PSP_CTRL_START;
    return pressed;
}

/* Inspection controls belong to the free camera. In particular, a frozen
   frustum rejects the world behind the old camera after the player turns. */
static inline void viewer_controls(int gameplay,unsigned pressed,unsigned held,
                                   int *cull,int *wire,int *freeze,int *vsync)
{
    if(gameplay){*cull=0;*wire=0;*freeze=0;*vsync=1;return;}
    if(pressed&PSP_CTRL_SQUARE)*cull=(*cull+1)%3;
    if(pressed&PSP_CTRL_CIRCLE)*freeze^=1;
    if(pressed&PSP_CTRL_TRIANGLE) {
        if((held&(PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER))==(PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER))*vsync^=1;
        else *wire^=1;
    }
}
#endif
