#include "garage_pending.h"
#include "garage_cast.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_garage_pending.inc"
#else
#include "generated/source_data/garage_pending.inc"
#endif
void garage_pending_pose_step(GaragePending *p)
{
    p->draw_camera=p->camera;
    p->camera=p->camera<243 ? p->camera+1:0; /* 04bdfc samples BEFORE advance */
    for(int c=0;c<15;c++)if(c==p->selector || ((p->selector==3 || p->selector==6) && (c==3 || c==6))) {
        p->actor[c]+=1;
        if(p->actor[c]>=PENDING_FRAMES[c])p->actor[c]-=PENDING_FRAMES[c];
    }
}
void garage_pending_view(const GaragePending *p,float *eye,float *target,float *fov,int *roll)
{
    unsigned k=(unsigned)p->draw_camera%244;
    memcpy(eye,PENDING_CAMERA[k],12);memcpy(target,PENDING_CAMERA[k]+3,12);
    *fov=PENDING_FOV[k]*(6.283185307f/65536.f);*roll=PENDING_ROLL[k];
}
const char *garage_pending_text(int selector)
{ return (unsigned)selector<13 ? PENDING_TEXT[selector]:""; }
#ifndef JGR_GARAGE_HOST_TEST
#include "font.h"
#include "gfx.h"
void garage_pending_draw(const GaragePending *p,Chars *bank)
{
    /* Caller scopes the original shared garage light around room + cast. */
    for(int c=0;c<15;c++)if(c==p->selector || ((p->selector==3 || p->selector==6) && (c==3 || c==6))) {
        if(PENDING_MODEL[c]<0)continue;
        float x=PENDING_POSITION[0];
        if(p->selector==3 || p->selector==6)x+=c==3 ? -5.f:5.f;
        chars_draw_one(bank,PENDING_MODEL[c],x,PENDING_POSITION[1],PENDING_POSITION[2],0,0,p->actor[c]);
    }
}
static void draw_pending_list(const Tex2dSet *tex,unsigned list)
{
    unsigned first=PENDING_LISTS[list][0],n=PENDING_LISTS[list][1];
    for(unsigned i=first;i<first+n;i++) {
        const GarageQuad *q=&PENDING_QUADS[i];
        if(q->slot<(unsigned)tex->n)tex2d_draw_entry(&tex->t[q->slot],q->x,q->y,q->w,q->h,0,0,q->color,q->u0,q->v0,q->u1,q->v1);
    }
}
void garage_pending_ui(const GaragePending *p,const Tex2dSet *tex)
{
    garage_cast_ui(tex,-1);
    draw_pending_list(tex,4); /* 800418: SHOWDOWN banner */
    const char *s=garage_pending_text(p->selector);
    float x=(640.f-font_real_width(s,1)/.75f)*.5f;
    font_real_draw_xy(x*.75f,409.f*(272.f/480.f),.75f,272.f/480.f,0xfff0f0f0,s);
    if(p->phase==3){draw_pending_list(tex,0);draw_pending_list(tex,p->no ? 2:1);draw_pending_list(tex,3);}
    if(p->fade>0)gfx_quad(0,0,480,272,((unsigned)(p->fade*255.f))<<24);
}
#endif
