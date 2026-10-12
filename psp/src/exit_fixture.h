#ifndef JGR_EXIT_FIXTURE_H
#define JGR_EXIT_FIXTURE_H
/* Opt-in integration fixture: supply two nearby positions across an authored
   exit. The ordinary mission/stage processors own detection and phase changes.
   No script, score, result, mission clock, or campaign state is changed here. */
#include <math.h>
#include <string.h>
#include "mission.h"
#include "stage_exit.h"

typedef struct {
    int enabled,phase,just_started,just_crossed;
    unsigned age,entry,event,direction;
    int stage_boundary;
    float from[3],to[3];
} ExitFixture;

static int exit_fixture_span(const MissionState *m,unsigned address,unsigned bytes)
{return m && m->img && address>=m->base && address-m->base<=m->len && bytes<=m->len-(address-m->base);}
static unsigned exit_fixture_word(const MissionState *m,unsigned address)
{unsigned word=0;if(exit_fixture_span(m,address,4))memcpy(&word,m->img+address-m->base,4);return word;}

static int exit_fixture_points(ExitFixture *f,const MissionQuad *source,unsigned mask)
{
    MissionQuad q=*source;mission_quad_prepare(&q);
    float length=sqrtf(q.normal[0]*q.normal[0]+q.normal[1]*q.normal[1]+q.normal[2]*q.normal[2]);
    if(!(length>0) || !isfinite(length))return 0;
    float sign=(mask&0x10000u) ? 1.f:-1.f,segment[6];
    for(int j=0;j<3;j++) {
        float offset=sign*q.normal[j]/length;
        f->from[j]=q.center[j]-offset;f->to[j]=q.center[j]+offset;
        segment[j]=f->from[j];segment[j+3]=f->to[j]-f->from[j];
    }
    f->direction=mission_quad_cross(&q,segment);
    return mission_quad_near(&q,segment) && (f->direction&mask);
}

static int exit_fixture_select(ExitFixture *f,const MissionState *m)
{
    unsigned list=exit_fixture_word(m,m->ra+0x34);
    for(unsigned i=0;i<256 && exit_fixture_span(m,list,20);i++,list+=20) {
        unsigned address=exit_fixture_word(m,list);if(!address)break;
        unsigned mask=exit_fixture_word(m,list+8),event=exit_fixture_word(m,list+16);
        /* An exit-only authored script makes the fixture's save/progression
           assertions unambiguous. Never enable a disabled boundary. */
        if(!mask || exit_fixture_word(m,list+12) || !exit_fixture_span(m,address,sizeof(MissionQuad)) ||
           !exit_fixture_span(m,event,36) || exit_fixture_word(m,event)!=0x19 || exit_fixture_word(m,event+32))continue;
        MissionQuad q;memcpy(&q,m->img+address-m->base,sizeof q);
        if(!exit_fixture_points(f,&q,mask))continue;
        f->entry=i;f->event=event;f->stage_boundary=0;return 1;
    }
    if(m->rec && m->rec->st==2)for(unsigned i=0;i<stage_exit_boundary_count();i++) {
        const StageExitBoundary *b=stage_exit_boundary(i);
        if(b->mission_id>=0 && b->mission_id!=(int)m->rec->mid)continue;
        if(!exit_fixture_points(f,&b->quad,b->direction))continue;
        f->entry=b->index;f->event=0;f->stage_boundary=1;return 1;
    }
    return 0;
}

/* Call immediately before the running 30-Hz stage/mission processors, after
   intro cleanup, outside replay/demo mode. Return 1 supplies/holds the source
   side; 2 supplies the destination; -1 means no valid exit or no detection.
   On just_started only, reset StageExit's previous sample to the supplied
   point to exclude the initial fixture teleport. Do not reset on crossing.
   Holding each side spans both native 9-tick and 4-tick sampling schedules. */
static int exit_fixture_tick(ExitFixture *f,const MissionState *m,float position[3])
{
    f->just_started=f->just_crossed=0;
    if(!f->enabled || !m || !m->active || m->phase)return 0;
    if(f->phase<0)return -1;
    if(!f->phase) {
        if(!exit_fixture_select(f,m)){f->phase=-1;return -1;}
        f->phase=1;f->age=0;f->just_started=1;
    }
    if(f->age>=36){f->phase=-1;return -1;}
    int crossed=f->age>=9;f->just_crossed=f->age==9;
    memcpy(position,crossed ? f->to:f->from,sizeof f->from);
    f->age++;return crossed ? 2:1;
}
#endif
