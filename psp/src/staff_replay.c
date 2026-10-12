#include "staff_replay.h"
#include <string.h>
#include <math.h>
#include "source_probe.h"
static uint32_t word(const StaffReplay *s,uint32_t p){return scene_word(&s->source,p);}
static float real(const StaffReplay *s,uint32_t p){uint32_t w=word(s,p);float f;memcpy(&f,&w,4);return f;}
int staff_replay_bind(StaffReplay *s,const void *image,unsigned length)
{
    memset(s,0,sizeof *s);s->source.image=image;s->source.length=length;s->source.base=0x8cfe0000;
    if(!image || length<240 || word(s,0x8cfe0000)!=1 || word(s,0x8cfe0014)!=1)return 0;
    s->row=1;s->load_request=-1;return 1; /* init 027e18 skips first sentinel */
}
int staff_replay_step_loaded(StaffReplay *s,int loader_busy)
{
    if(s->error || s->done)return 0;
    s->load_request=-1;s->clock++;
    if(s->startup<3) {
        if(!loader_busy) {
            s->startup++;
            if(s->startup<3)s->load_request=s->startup==1 ? 1:19;
        }
        return 0; /* no phase-3 work on the loader transition callback */
    }
    if(s->phase==0) {
        uint32_t row=0x8cfe0000+s->row*20,p=word(s,row);
        if(!p){s->done=1;return 0;}
        if(p==1){s->row++;return 0;}
        if(!scene_bytes(&s->source,p,36) || !scene_bytes(&s->source,row,20)){s->error=1;return 0;}
        CtrlStream *c=&s->stream;c->chr=word(s,p);c->speed=real(s,p+4);
        c->x=real(s,p+8);c->y=real(s,p+12);c->z=real(s,p+16);c->w5=word(s,p+20);c->yaw=word(s,p+24);c->w7=word(s,p+28);
        c->rec=scene_bytes(&s->source,p+32,4);c->nrec=0;
        while(scene_bytes(&s->source,p+32+c->nrec*4,4)) {
            if(c->rec[c->nrec++*4]&128)break;
        }
        if(!c->nrec || !(c->rec[(c->nrec-1)*4]&128)){s->error=1;return 0;}
        s->camera=word(s,row+16);if(!scene_bytes(&s->source,s->camera,16)){s->error=1;return 0;}
        ctrl_play_start(&s->input,c);
        /* A new original task is initialized AND updated in its creation
           pass: the zero header record is consumed before first playback. */
        ctrl_play_tick(&s->input);s->tick=0;s->phase=1;return 1;
    }
    if(s->phase==1) {
        s->tick++;
        if(word(s,s->camera)<=(unsigned)s->tick) {
            s->camera+=16;if(!scene_bytes(&s->source,s->camera,16)){s->error=1;return 0;}
        }
        if(!ctrl_play_tick(&s->input))s->phase=2;
    }else {s->row++;s->phase=0;} /* original task-19 cleanup, then next creation */
    return 0;
}
int staff_replay_step(StaffReplay *s){return staff_replay_step_loaded(s,0);}
void staff_replay_camera_target(const StaffReplay *s,const float pos[3],const float body[3],SceneCamera *c)
{
    memset(c,0,sizeof *c);uint32_t p=word(s,s->camera+12),flags=word(s,s->camera+8);
    if(word(s,s->camera+4)!=1 || !scene_bytes(&s->source,p,40))return;
    for(int k=0;k<3;k++){c->pos[k]=real(s,p+4*k)+(flags&2 ? pos[k]:0);c->forward[k]=real(s,p+12+4*k);}
    if(flags&32) {
        for(int k=0;k<3;k++)c->forward[k]=body[k]-c->pos[k];
        float squared=probe_fipr(c->forward,c->forward);
        if(squared>0){float inverse=1.f/sqrtf(squared);for(int k=0;k<3;k++)c->forward[k]*=inverse;}
    }
    /* Untracked authored directions are copied without renormalization. */
    c->roll=word(s,p+24);c->fov=word(s,p+28);c->near_clip=-real(s,p+32);c->far_clip=-real(s,p+36);c->active=1;
}
void staff_replay_camera(const StaffReplay *s,const float pos[3],SceneCamera *c)
{float body[3]={pos[0],pos[1]+10,pos[2]};staff_replay_camera_target(s,pos,body,c);}
