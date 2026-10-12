/* Original garage task 22a: 04b670 / 04b864 / 04bdfc. */
#ifndef JGR_GARAGE_PENDING_H
#define JGR_GARAGE_PENDING_H
#include <string.h>
typedef struct {
    int phase,selector,declined,no,paired,clock,voice,sound;
    float fade,fade_step,remainder;
    unsigned edges;
    int camera,draw_camera;
    float actor[15];
} GaragePending;
typedef struct { int mode,stage,chapter,character; } GaragePendingRoute;
/* Read-only: invitation != membership. Original priority 17fcb4. */
static inline int garage_pending_selector(const unsigned char *save)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_garage_pending_h__order_0.inc"
#else

#include "generated/source_data/garage_pending_h__order_0.inc"

#endif

    for(unsigned k=0;k<sizeof order;k++)if(save[0x32c+order[k]]==2)
        return order[k]==14 ? 12:order[k];
    return -1;
}
static inline void garage_pending_init(GaragePending *p,const unsigned char *save)
{
    memset(p,0,sizeof *p);p->selector=garage_pending_selector(save);
    p->paired=save[0x32f]==2 || save[0x332]==2;
    p->voice=p->sound=-1;
}
static inline GaragePendingRoute garage_pending_route(const GaragePending *p)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_garage_pending_h__stage_0.inc"
#else

#include "generated/source_data/garage_pending_h__stage_0.inc"

#endif
 /* 17fd14 */
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_garage_pending_h__chapter_0.inc"
#else

#include "generated/source_data/garage_pending_h__chapter_0.inc"

#endif
 /* 17fd3c */
    GaragePendingRoute r={1,-1,-1,-1};
    if(p->declined || p->selector<3 || p->selector>12)return r;
    r.stage=p->paired ? 0:stage[p->selector-3];
    r.chapter=p->paired ? 6:chapter[p->selector-3];
    r.mode=p->paired || p->selector==6 ? 12:4;
    if(r.mode==12)r.character=6;
    return r;
}
static inline void garage_pending_out(GaragePending *p)
{ p->fade=0;p->fade_step=0.04f;p->phase=5; }
/* Original DC pressed bits, after digital/analog edge merging. The fade
   child is stepped AFTER its parent, as in the source scheduler. */
static inline int garage_pending_step(GaragePending *p,unsigned buttons)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_garage_pending_h__voice_0.inc"
#else

#include "generated/source_data/garage_pending_h__voice_0.inc"

#endif

    int ready=0;p->voice=p->sound=-1;
    if(p->selector<0)return 0;
    switch(p->phase) {
    case 0:p->fade=1;p->fade_step=-0.04f;p->phase=1;break;
    case 1:if(!p->fade_step){p->phase=2;p->voice=voice[p->selector];}break;
    case 2:
        if(buttons&4){p->no=0;p->sound=1;if(p->paired)garage_pending_out(p);else p->phase=3;}
        else if(!p->paired && (buttons&2)){p->declined=1;p->sound=3;garage_pending_out(p);}
        break;
    case 3:
        if(buttons&4){p->declined=p->no;p->sound=p->no ? 3:1;garage_pending_out(p);}
        else if(buttons&2){p->phase=2;p->sound=3;}
        else if(buttons&0x40){p->no=0;p->sound=0;}
        else if(buttons&0x80){p->no=1;p->sound=0;}
        break;
    case 5:if(!p->fade_step)p->phase=6;break;
    case 6:ready=1;break;
    }
    if(p->fade_step){p->fade+=p->fade_step;
        if(p->fade>1){p->fade=1;p->fade_step=0;}
        if(p->fade<0){p->fade=0;p->fade_step=0;}}
    p->clock++;return ready;
}
/* Renderer/data adapter; clocks remain host-testable without PSP GU. */
void garage_pending_pose_step(GaragePending *p);
void garage_pending_view(const GaragePending *p,float *eye,float *target,float *fov,int *roll);
const char *garage_pending_text(int selector);
#ifndef JGR_GARAGE_HOST_TEST
#include "charanim.h"
#include "tex2d.h"
void garage_pending_draw(const GaragePending *p,Chars *bank);
void garage_pending_ui(const GaragePending *p,const Tex2dSet *tex);
#endif
#endif
