#ifndef JGR_ONISHIMA_DRAW_H
#define JGR_ONISHIMA_DRAW_H
#include "gang.h"
#include "charanim.h"
static inline void onishima_draw(Chars *c,int ch,const Gang *g,const float *root)
{
    const GangMember *m=g->m;int secondary=-1;float weight=0,other_frame=m->frame;
    if(m->chief.blend>0 && m->chief.old_motion>=0) {
        secondary=m->chief.old_motion;weight=m->chief.blend;other_frame=m->chief.old_frame;
    } else if(m->anim==22 && g->speed>0){secondary=7;weight=m->chief.gait;}
    chars_draw_one_xf_blend(c,ch,root,m->anim,m->frame,secondary,other_frame,weight,
        m->overlay,m->secondary_frame,1);
}
#endif
