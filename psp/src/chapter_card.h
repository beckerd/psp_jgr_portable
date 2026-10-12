#ifndef JGR_CHAPTER_CARD_H
#define JGR_CHAPTER_CARD_H
#include <string.h>
/* Original garage parent 021490, task a01 (0fbc90/0fbd18), fade task
   217 (020ea0/02106c). Seen flags are session state, not save bytes. */
static inline void chapter_seen_load(unsigned char seen[3],const unsigned char *s)
{
    seen[0]=s[0x33c]==1;
    seen[1]=s[0x33e]==1 && s[0x32f]==1;
    seen[2]=s[0x33f]==1 || s[0x343]==1 || s[0x347]==1;
}
static inline int chapter_card_take(unsigned char seen[3],const unsigned char *s)
{
    int show=(!seen[0] && s[0x33c]!=1) ||
        (!seen[1] && s[0x33e]==1 && s[0x32f]!=1) ||
        (!seen[2] && s[0x356]==1 && s[0x33f]!=1 && s[0x343]!=1 && s[0x347]!=1);
    if(!show)return -1;
    int i=s[0x356]==1 ? 2:s[0x33e]==1 ? 1:0;
    seen[i]=1;return i;
}
typedef struct {int chapter,phase,hold,done;float alpha,remainder;} ChapterCard;
static inline void chapter_card_begin(ChapterCard *c,int chapter)
{memset(c,0,sizeof *c);c->chapter=chapter;c->alpha=1.f;}
static inline void chapter_card_step(ChapterCard *c)
{
    switch(c->phase) {
    case 0:c->alpha=1.f;c->phase=1;break;
    case 1:c->alpha-=.04f;if(c->alpha<0){c->alpha=0;c->phase=2;}break;
    case 2:if(c->hold++>90){c->alpha=0;c->phase=3;}break;
    case 3:c->alpha+=.04f;if(c->alpha>1){c->alpha=1;c->phase=4;}break;
    case 4:c->done=1;break;
    }
}
#endif
