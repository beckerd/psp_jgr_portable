#include "helidestruct.h"

int heli_wreck_draws(const HeliDestruction *s,const unsigned frames[8],HeliWreckDraw out[8])
{
    if((s->flags&0x120) || s->state<16 || s->state>18)return 0;
    int n=0,motion=s->anim==4 ? 0:1;
    for(int i=0;i<2;i++)out[n++]=(HeliWreckDraw){i,motion,s->frame,0.f};
    if(s->state==16) {
        for(int i=0;i<2;i++)if(s->clock[0]<(float)frames[i])
            out[n++]=(HeliWreckDraw){2+i,0,s->clock[0],s->material[0]};
    } else if(s->state==17) {
        for(int pair=0;pair<3;pair++) {
            if(pair && s->frame<(pair==1 ? 17.f:22.f))continue;
            int i=2+pair*2;float frame=s->clock[pair];
            if(frame>=(float)frames[i])continue; /* first tree gates its whole pair */
            out[n++]=(HeliWreckDraw){2+i,0,frame,s->material[pair*2]};
            if(frame<(float)frames[i+1])out[n++]=(HeliWreckDraw){3+i,0,frame,s->material[pair*2+1]};
        }
    }
    return n;
}

static void material_pair(float *v,float clock)
{
    if(v[0]>-1.f && clock>=50.f)v[0]-=0.0344827585f; /* 3d0d3dcb */
    if(v[1]>-1.f && clock>=4.f)v[1]-=0.111111112f;   /* 3de38e39 */
}
HeliDestructionEvents heli_destruction_tick(HeliDestruction *s,int landed)
{
    HeliDestructionEvents e={-1,0,0};
    if((s->flags&0x20) || s->state<16 || s->state>18)return e;
    /* 0a7564: animation has already run on the PREVIOUS tick. */
    if(s->state==16) {
        if(s->clock[0]>=56.f && landed) {
            s->state=17;s->anim=5;s->timer=0;s->explosion=0;
            s->material[0]=s->clock[0]=0.f;e.landed_bounds=1;
        }
    } else { /* 0a75d4 and 0a76b8 */
        int was17=s->state==17;
        if(was17 && s->anim!=5) {s->state=18;s->parent_flags|=2;}
        if(was17 || s->explosion<14) {
            s->timer=(int16_t)((uint16_t)s->timer+1u);
            if(s->timer>9 && s->explosion<14) {
                s->timer=0;e.explosion=s->explosion++;
            }
        }
    }
    if(s->flags&0x120)return e; /* 0a4eb4; main state still ran */
    if(s->anim!=s->previous_anim) {
        s->sub=0;s->previous_anim=s->anim;s->frame=0.f;
    }
    if(s->anim==4 || s->anim==5) {
        if(!s->sub) { /* 0a52e8 / 0a53f0 entry */
            s->frame=0.f;s->clock[0]=0.f;
            if(s->anim==5)s->clock[1]=s->clock[2]=0.f;
            s->sub++;e.sound=s->variant ? 0xf0002:0xd0002;
        } else {
            /* 0bdf30 mode 1: clamp at count-1, do NOT loop the wreck. */
            s->frame+=1.f;
            float last=(float)s->motion_frames[s->anim-4]-1.f;
            if(s->frame>last)s->frame=last;
            if(s->frame<0.f)s->frame=0.f;
            s->clock[0]+=1.f;
            if(s->anim==4) {
                if(s->material[0]>-1.f && s->clock[0]>=60.f)s->material[0]-=0.05f;
            } else {
                material_pair(s->material,s->clock[0]);
                if(s->frame>=17.f) {s->clock[1]+=1.f;material_pair(s->material+2,s->clock[1]);}
                if(s->frame>=22.f) {
                    s->clock[2]+=1.f;material_pair(s->material+4,s->clock[2]);
                    if(s->clock[2]>=78.f)s->anim=6;
                }
                if(s->frame==16.f)e.sound=s->variant ? 0xf0002:0xd0002;
            }
        }
    } else if(s->anim==6 && !s->sub) { /* 0a56a0 */
        s->frame=(float)s->motion_frames[1]-1.f;
    }
    return e;
}
