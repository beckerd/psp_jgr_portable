#include "can_hud.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
void can_hud_init(CanHud *h,int result_mode)
{memset(h,0,sizeof *h);h->mode=result_mode;h->color=0xffff0000;}
int can_hud_tick(CanHud *h,int *inventory,int capacity)
{
    if(!h->phase){h->origin[0]=50;h->origin[1]=448;h->origin[2]=h->mode==1 ? -.1f:-.9f;h->phase=1;}
    if(*inventory>=capacity)*inventory=capacity;
    h->full=h->displayed>=capacity;
    h->target=*inventory;
    if(h->mode==1)h->previous=h->displayed=h->target;
    /* The division precedes observing this update's arrivals/decreases. */
    h->color=0xffffffffu-(uint32_t)(h->displayed*255/capacity)*0x101u;
    int children=0;
    if(h->previous<h->target)children=h->target-h->previous<2 ? 1:5;
    else if(h->target<h->previous)h->displayed=h->target;
    h->previous=h->target;return children;
}
void can_flight_init(CanFlight *f,float x,float y,unsigned degrees)
{
    memset(f,0,sizeof *f);f->position[0]=x;f->position[1]=y;f->position[2]=-100;
    f->target[0]=0;f->target[1]=480;f->target[2]=-.9f;
    f->heading=(unsigned)(((float)degrees*65536.f)/360.f);
}
int can_flight_tick(CanFlight *f,float (*sine)(int))
{
    if(f->dead)return 0;
    if(!f->phase)f->phase=1;
    else if(f->phase==1) {
        /* 2712d0=cos, 27af2c=sin. Launch force is applied once. */
        f->force[0]+=100.f*sine((int)(f->heading+16384)&65535);
        f->force[1]+=100.f*sine((int)f->heading&65535);
        f->phase=2;
    }else if(f->phase==2) {
        for(int k=0;k<3;k++)f->force[k]+=(f->position[k]-f->target[k])*-.1f;
        /* 04f840: force/30, accumulate velocity, THEN position. */
        for(int k=0;k<3;k++)f->delta[k]=f->force[k]/30.f;
        for(int k=0;k<3;k++)f->velocity[k]+=f->delta[k];
        for(int k=0;k<3;k++)f->position[k]+=f->velocity[k];
        for(int k=0;k<3;k++)f->force[k]=f->delta[k]=0;
        f->rotation+=0x2aaa;
        if(f->position[2]>=-5.f)f->phase=3;
    }else if(f->phase==3){f->dead=1;return 1;}
    return 0;
}
void can_flight_model(const CanFlight *f,float out[16],float (*sine)(int))
{
    /* 04e8d4 -> 27a104 rotates X, not Y. Its independent camera's
       projection centre, rather than model XY, carries the particle. */
    float c=sine((f->rotation+16384)&65535),s=sine(f->rotation&65535);
    const float matrix[16]={1,0,0,0,0,c,-s,0,0,s,c,f->position[2],0,0,0,1};
    memcpy(out,matrix,sizeof matrix);
}
void can_hud_view_light(const float view[16],const float direction[3],float out[3])
{
    /* 04e950 transforms the live 10a7c4 direction through the ordinary
       scene view BEFORE installing the default HUD camera. Ninja's
       default camera reverses Y/Z; converting both cameras to PSP space
       leaves exactly this scene-view rotation, not a fixed HUD light. */
    for(int r=0;r<3;r++) {
        double d=(double)view[r*4]*direction[0];d+=(double)view[r*4+1]*direction[1];
        d+=(double)view[r*4+2]*direction[2];out[r]=(float)d;
    }
}
void can_hud_play_free(CanHudPlay *p)
{
    while(p->children){CanFlightTask *next=p->children->next;free(p->children);p->children=next;}
    memset(p,0,sizeof *p);
}
void can_hud_play_health(CanHudPlay *p,unsigned kind,float x,float y)
{
    if(kind>1 || !isfinite(x) || !isfinite(y)){p->error=1;return;}
    CanFlightTask *t=calloc(1,sizeof *t);if(!t){p->error=2;return;}
    /* 0ed6f8/0ed84c share 04f7d8/818/840 with the paint HUD particle.
       Health flies to (0,0,-.9), with fixed BAM launch and a half-size mesh. */
    can_flight_init(&t->flight,x,y,0);t->flight.target[1]=0;t->flight.heading=0xffe93e94u;
    t->health_kind=kind+1;t->next=p->children;p->children=t;
}
void can_hud_play_tick(CanHudPlay *p,int *inventory,int capacity,float x,float y,float (*sine)(int))
{
    if(capacity<=0 || !sine || !isfinite(x) || !isfinite(y)){p->error=1;return;}
    int spawn=can_hud_tick(&p->counter,inventory,capacity);
    for(int j=0;j<spawn;j++) {
        CanFlightTask *t=calloc(1,sizeof *t);
        if(!t){p->error=2;break;}
        can_flight_init(&t->flight,x,y,270+20*j);t->next=p->children;p->children=t;
    }
    for(CanFlightTask **at=&p->children;*at;) {
        CanFlightTask *t=*at;
        if(can_flight_tick(&t->flight,sine)) {
            /* 04dff0 assigns target, not +=1: concurrent flying cans do
               not award inventory or resurrect a can already sprayed. */
            if(!t->health_kind)p->counter.displayed=p->counter.target;
            *at=t->next;free(t);
        }else at=&t->next;
    }
}
