#ifndef JGR_MESSAGE_HUD_H
#define JGR_MESSAGE_HUD_H
#include <string.h>
/* 035bb0/035a9c/035ca8: task 56. Text bytes are copied from JMDc, never
   decoded/reworded. Sound consumers receive the original reference. */
typedef struct {
    int phase,flags,hold,alert,width,voice,visible,serial;
    float x,y;
    char text[256];
} MessageHud;
static inline void message_hud_init(MessageHud *h)
{memset(h,0,sizeof *h);h->visible=1;h->x=196;h->y=419;h->voice=-1;}
static inline int message_hud_show(MessageHud *h,const char *text,unsigned length,int voice,int language)
{
    if(language!=1 || !text || length>=sizeof h->text)return 0;
    memcpy(h->text,text,length);h->text[length]=0;h->width=(int)length*12;
    h->voice=voice;h->x=448;h->y=419;h->phase=3;h->serial++;return 1;
}
/* Returns 1 at the original ADX dispatch boundary, including stop (-1).
   Starting a new message deliberately does NOT reset either source clock. */
static inline int message_hud_tick(MessageHud *h)
{
    switch(h->phase){
    case 0:h->phase=1;break;
    case 3:h->phase=4;break;
    case 4:if(++h->alert>30){h->alert=0;h->phase=5;return 1;}break;
    case 5:h->phase=6;break;
    case 6:
        h->x-=6;
        if(h->flags&2){if(h->x+h->width<188)h->x=448;}
        else if(h->width>256){if(h->x+h->width<444){h->x=444-h->width;h->phase=7;}}
        else if(h->x<196){h->x=196;h->phase=7;}
        break;
    case 7:if(++h->hold>=60){h->hold=0;h->phase=1;}break;
    }return 0;
}
/* Task 22's draw advances only while cans/popup are enabled. */
typedef struct {int id,clock,draw_clock,visible;} HudNotice;
static inline void hud_notice_init(HudNotice *h)
{h->id=-1;h->clock=30;h->draw_clock=30;h->visible=0;}
static inline void hud_notice_acquired(HudNotice *h,int id)
{h->id=id;h->clock=-30;h->visible=0;}
static inline void hud_notice_present(HudNotice *h,unsigned flags,int enabled)
{
    h->visible=h->id>=0 && h->clock<30 && !(flags&16) && enabled;
    if(h->visible)h->draw_clock=h->clock++;
}
static inline int hud_notice_x(int clock)
{int d=clock<-20 ? clock+20:clock>20 ? clock-20:0;return 450+2*d*d;}
#endif
