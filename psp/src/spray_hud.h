#ifndef JGR_SPRAY_HUD_H
#define JGR_SPRAY_HUD_H
#include <stdint.h>
/* Recovered 041310 task kernel. Rendering, task scheduling and score
   children are separate: do not replace the production wait by this
   task's alpha alone, because deletion also waits for the score child. */
typedef struct {
    int delay,fade,finished,child_alive;
    float alpha,scale,y,z,frame;
    unsigned frames;
} SprayHudRow;
typedef struct {
    int mode,age,cue,deleted,count;
    SprayHudRow rows[3];
} SprayHud;
static inline void spray_hud_tick(SprayHud *h,int player_state,int next_cue,float (*fsca)(int))
{
    if(h->deleted)return;
    if(player_state==10 || player_state==11){h->deleted=1;return;}
    if(h->mode==0) {
        int completed=0;
        for(int j=0;j<h->count;j++) {
            SprayHudRow *r=h->rows+j;
            if(r->delay)r->delay--;
            if(!r->delay) {
                if(!r->fade){if(r->alpha<1.f)r->alpha+=0x1.555556p-4f;else r->alpha=1.f;}
                else if(r->alpha<=0.f){r->alpha=0;r->finished=1;}
                else r->alpha-=0x1.555556p-4f;
            }
            /* 0bdf30's final argument 1 means CLAMP, not loop. */
            if(j<=h->cue && !r->delay) {
                r->frame+=1.f;
                if(r->frame>(float)(r->frames-1))r->frame=(float)(r->frames-1);
                if(r->frame<0)r->frame=0;
            }
            if(j<h->cue)r->fade=1;
            r->y=(j==h->cue && !r->delay && r->alpha==1.f) ? fsca(h->age)*5.f-10.f:0.f;
            if(r->finished && !r->child_alive)completed++;
            r->z=0;r->scale=fsca((int)(r->alpha*16384.f));
        }
        h->cue=next_cue;h->age=(int32_t)((uint32_t)h->age+0x38e);
        if(completed==h->count)h->deleted=1;
    } else if(h->mode==1) {
        for(int j=0;j<h->count;j++) {
            SprayHudRow *r=h->rows+j;
            r->z=fsca((h->age*16384)/6)*200.f;r->y=0;
            /* Miss motion uses 0bdf30(...,0): wrap, not clamp. */
            r->frame+=1.f;
            if(r->frame>=r->frames)r->frame-=r->frames;
            if(r->frame<0)r->frame+=r->frames;
            if(r->frame>=r->frames)r->frame=0;
            if(r->frame<0)r->frame=(float)(r->frames-1);
        }
        if(h->age<6)h->age++;else h->deleted=1;
    }
}
#endif
