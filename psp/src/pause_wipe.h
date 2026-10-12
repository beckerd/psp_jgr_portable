#ifndef JGR_PAUSE_WIPE_H
#define JGR_PAUSE_WIPE_H
#include <stdint.h>
#if JGR_PORTABLE_ASSETS
#include "source_data_pause_wipe_path.inc"
#else
#include "generated/source_data/pause_wipe_path.inc"
#endif
typedef struct {float x,y;uint32_t color;uint16_t u,v;} PauseWipePoint;
typedef struct {int phase;float clock;PauseWipePoint grid[400];} PauseWipe;
/* 04c424/04c5ac: default black, non-textured wipe; reverse is retained for
   the original oracle but the pause menu requests direction +1 (phase 3). */
static inline void pause_wipe_init(PauseWipe *w,int reverse)
{
    w->phase=reverse ? 2:3;w->clock=0;
    for(int y=0;y<20;y++)for(int x=0;x<20;x++){
        PauseWipePoint *p=&w->grid[y*20+x];
        p->x=x*0x1.0d7944p+5f;p->y=y*0x1.9435e6p+4f;
        p->u=(uint16_t)(x*0x1.ad7944p+3f);p->v=(uint16_t)(y*0x1.ad7944p+3f);
        p->color=reverse ? 0xff000000u:0;
    }
}
/* 04c4ac, 04c660/04c6e0, 04ca48, 04ca7c/04cab4. Source uses separate
   single-precision multiply/add instructions, ten sequential .1 increments
   per 30-Hz update, and still stamps once after declaring the wipe complete. */
static inline void pause_wipe_tick(PauseWipe *w)
{
    if(w->phase!=2 && w->phase!=3)return;
    int reverse=w->phase==2;
    if(!(31.f>w->clock)){w->clock=0;w->phase=0;}
    for(int j=0;j<10;j++){
        int i=(int)w->clock;float f=w->clock-i,inv=1.f-f;
        float x=inv*PAUSE_WIPE_PATH[i][0]+f*PAUSE_WIPE_PATH[i+1][0];
        float y=-(inv*PAUSE_WIPE_PATH[i][1]+f*PAUSE_WIPE_PATH[i+1][1]);
        for(int k=0;k<400;k++){
            float dx=w->grid[k].x-x,dy=w->grid[k].y-y;
            if(dx*dx+dy*dy<10000.f)w->grid[k].color=reverse ? 0:0xff000000u;
        }
        w->clock+=.1f;
    }
    for(int k=0;k<400;k++){
        int a=(int)(w->grid[k].color>>24)+(reverse ? -9:9);
        if(a<0)a=0;if(a>255)a=255;
        w->grid[k].color=(w->grid[k].color&0xffffffu)|((uint32_t)a<<24);
    }
}
void pause_wipe_draw(const PauseWipe *w);
#endif
