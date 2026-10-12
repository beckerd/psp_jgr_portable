#include "hud_state.h"
#include <math.h>
#include <string.h>
void timer_hud_init(TimerHud *h,int kind,int mode)
{
    *h=(TimerHud){.mode=mode,.countdown=kind==1 || kind==2 || kind==5 || kind==8,.previous=-1};
}
void timer_hud_tick(TimerHud *h,int live)
{
    if(!h->phase) {
        h->initial=h->ticks=live;
        if(h->mode)h->countdown=0;
        h->phase=1;
    } else if(h->phase==1) {
        if(!h->mode)h->ticks=live;
        if(h->ticks<0)h->ticks=0;
    }
    h->clock++;
}
int timer_hud_present(TimerHud *h,unsigned flags,int *voice)
{
    *voice=-1;
    if((flags&HUD_HIDE_TIME) || !h->countdown)return -1;
    int model=-1;
    #if JGR_PORTABLE_ASSETS
#include "source_data_mixed_hud_state_c__start_0.inc"
#else

#include "generated/source_data/hud_state_c__start_0.inc"

#endif
    for(int i=0;i<4;i++)if(h->clock>=start[i] && h->clock<start[i]+18) {
        model=i;if(h->clock==start[i])*voice=cue[i];break;
    }
    if(h->clock>107)h->owned=0;
    if(h->previous==model)h->frame+=1.f;else h->frame=0;
    h->previous=model;return model;
}
void timer_hud_position(const TimerHud *h,float *x,float *y)
{*x=h->mode==2 ? 430:600;*y=h->mode==2 ? 178:32;}
void health_hud_init(HealthHud *h,int maximum)
{*h=(HealthHud){.maximum=maximum,.target=maximum,.previous=maximum,.displayed=maximum};}
void health_hud_tick(HealthHud *h,int current,int threshold,int result_mode)
{
    switch(h->phase) {
    case 0:if(result_mode)h->target=h->displayed=current;h->phase=1;break;
    case 1:h->previous=h->target;h->target=current;if(h->displayed!=h->target)h->phase=2;break;
    case 2:
        if(h->displayed>h->target)h->displayed--;
        else if(h->displayed<h->target){h->displayed++;h->healing=1;}
        else{h->displayed=h->target;h->healing=0;h->phase=1;}
        break;
    }
    h->low=h->target<threshold;
    if(h->low || h->healing)h->angle+=0x71c;
}
uint32_t health_hud_color(const HealthHud *h,float (*sine)(int))
{
    unsigned pulse=(int)(fabsf(sine((int)(h->angle&65535)))*255.f)&255;
    if(h->low)return 0xffff0000u|(pulse<<8);
    return 0xffffaa00u|(h->healing ? pulse:0);
}
void health_hud_bar(int value,float out[8])
{
    float width=((float)value/150.f)*240.f;
    float v[8]={34,36,20,50,34+width,36,20+width,50};memcpy(out,v,sizeof v);
}
int hud_score_glyphs(int value,float x,float y,float scale,HudGlyph out[16])
{
    if(value<0)value=0;
    int n=0;
    do{x-=22.f*scale;out[n++]=(HudGlyph){6+value%10,x,y,scale};value/=10;}while(value && n<16);
    return n;
}
int hud_timer_glyphs(int ticks,float x,float y,float scale,HudGlyph out[16])
{
    if(ticks<0)ticks=0;
    float seconds=(float)ticks/30.f;
    int whole=(int)seconds,hundredths=(int)floorf(seconds*100.f+.5f)%100;
    float small=scale*.75f;int n=0;
    for(int i=0;i<2;i++) {
        x-=28.f*small;out[n++]=(HudGlyph){26+hundredths%10,x,y+(scale-small)*56.f,small};hundredths/=10;
    }
    x-=28.f*scale*.6f;out[n++]=(HudGlyph){36,x+10.f,y+44.f,scale};
    do{x-=28.f*scale;out[n++]=(HudGlyph){26+whole%10,x,y,scale};whole/=10;}while(whole && n<16);
    return n;
}
