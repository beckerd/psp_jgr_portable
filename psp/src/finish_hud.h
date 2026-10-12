#ifndef FINISH_HUD_H
#define FINISH_HUD_H
struct Chars;
/* 0e8e6c / 0e8ef0 / 0e8f34: one resident task (id 60), 60 ticks.
   0e8f5c retains the last animated pose after the 30-frame intro. */
typedef struct { unsigned life,frame; } FinishHud;
static inline void finish_hud_start(FinishHud *h)
{if(!h->life){h->life=60;h->frame=1;}}
static inline void finish_hud_tick(FinishHud *h)
{if(h->life){--h->life;if(h->frame<29)++h->frame;}}
void finish_hud_draw(struct Chars *,const FinishHud *,const float projection[16],const float view[16]);
#endif
