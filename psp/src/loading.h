#ifndef JGR_LOADING_H
#define JGR_LOADING_H
#include "charanim.h"
/* 0172c8 / 0172e8: two independent source clocks. */
typedef struct {unsigned frame[2];} LoadingHud;
static inline void loading_tick(LoadingHud *h)
{ for(int i=0;i<2;i++)if(++h->frame[i]>=101)h->frame[i]=0; }
int loading_load(Chars *);
void loading_draw(Chars *,const LoadingHud *,float focal);
#endif
