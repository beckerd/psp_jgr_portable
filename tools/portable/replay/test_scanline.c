/* Authored video/audio clock schedules; no original game assets or code. */
#include "portable_replay_scanline.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint64_t line,completed,audio;unsigned period,irq[2];} Clock;
static unsigned state=0x8253371;
static unsigned random_word(void){state=1664525u*state+1013904223u;return state;}
static uint64_t reference(Clock *c)
{
    for(unsigned silent=0;;silent++){
        unsigned next=(unsigned)((c->completed+1)%c->period);
        if(c->audio<=c->line)return c->audio;
        if(next==c->irq[0] || next==c->irq[1] || silent==c->period)return c->line;
        c->completed++;c->line+=3175;
    }
}
static unsigned service(Clock *c,uint64_t event,unsigned audio_interval)
{
    unsigned flags=0;
    /* Same boundary order: video interrupt events, then audio timer. */
    if(event>=c->line){
        unsigned next=(unsigned)(++c->completed%c->period);
        if(next==c->irq[0])flags|=1;
        if(next==c->irq[1])flags|=2;
        c->line+=3175;
    }
    if(event>=c->audio){flags|=4;c->audio=event+audio_interval;}
    return flags;
}
int main(void)
{
    unsigned tests=0;
    for(unsigned trial=0;trial<4096;trial++){
        Clock a={0},b;uint64_t epoch=((uint64_t)random_word()<<16);
        a.period=1+random_word()%1024;a.completed=random_word();
        a.line=epoch+3175;a.irq[0]=random_word()%1200;a.irq[1]=random_word()%1200;
        unsigned interval=1+random_word()%1000000;
        a.audio=trial%3?epoch+random_word()%1000000:UINT64_MAX;
        if(trial%4==0)a.audio=a.line; /* simultaneous video/audio boundaries */
        if(trial%8==0)a.irq[1]=a.irq[0]; /* both video interrupts together */
        b=a;
        for(unsigned event=0;event<64;event++){
            uint64_t expected=reference(&a);
            uint64_t actual=portable_replay_scanline_idle(&b.line,&b.completed,b.period,b.irq[0],b.irq[1],b.audio);
            assert(actual==expected && !memcmp(&a,&b,sizeof a));
            assert(service(&a,expected,interval)==service(&b,actual,interval));
            assert(!memcmp(&a,&b,sizeof a));tests++;
        }
    }
    uint64_t line=9000,completed=8;
    assert(portable_replay_scanline_idle(&line,&completed,0,2,3,UINT64_MAX)==9000 && line==9000 && completed==8);tests++;
    assert(portable_replay_scanline_idle(&line,&completed,1025,2,3,UINT64_MAX)==9000 && line==9000 && completed==8);tests++;
    line=UINT64_MAX-1;
    assert(portable_replay_scanline_idle(&line,&completed,20,3,4,UINT64_MAX)==UINT64_MAX-1 && completed==8);tests++;
    printf("{\"tests\":%u,\"passed\":true}\n",tests);return 0;
}
