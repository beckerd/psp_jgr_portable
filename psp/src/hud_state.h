#ifndef JGR_HUD_STATE_H
#define JGR_HUD_STATE_H
#include <stdint.h>
/* 0323d0/032450, task fields +6c,70,74,78,7c,80,81,84. */
typedef struct {
    int phase,maximum,target,previous,displayed,low,healing;
    uint32_t angle;
} HealthHud;
typedef struct {int entry;float x,y,scale;} HudGlyph;
/* 04ffd8/05001c/0500cc, original timer task 21. Update and presentation
   are separate: hidden presentation must not emit countdown voices. */
typedef struct {
    int phase,mode,ticks,countdown,clock,owned,previous,initial;
    float frame;
} TimerHud;
void timer_hud_init(TimerHud *,int mission_kind,int mode);
void timer_hud_tick(TimerHud *,int live_ticks);
int timer_hud_present(TimerHud *,unsigned flags,int *voice);
void timer_hud_position(const TimerHud *,float *x,float *y);
void health_hud_init(HealthHud *,int maximum);
void health_hud_tick(HealthHud *,int current,int low_threshold,int result_mode);
uint32_t health_hud_color(const HealthHud *,float (*sine)(int));
void health_hud_bar(int value,float out[8]); /* four DC-space strip vertices */
/* 04fcc0/04ff04: right-aligned seconds.hundredths, NOT minutes:seconds. */
int hud_timer_glyphs(int ticks,float x,float y,float scale,HudGlyph out[16]);
/* 04f9ac, source constant-width, right-aligned font. */
int hud_score_glyphs(int value,float x,float y,float scale,HudGlyph out[16]);
enum { HUD_HIDE_HEALTH=2,HUD_HIDE_SCORE=4,HUD_HIDE_TIME=8,HUD_HIDE_CANS=16 };
#endif
