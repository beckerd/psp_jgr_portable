#ifndef JGR_RIVAL_COUNTER_H
#define JGR_RIVAL_COUNTER_H
#include "object_graffiti.h"
/* 019cfc -> 01b7f4. Atlas order is 1..9,0, not 0..9. The source
   passes 9-trunc(reveal*10/288), and draws a second digit for index 9. */
static inline int rival_counter_index(const GfoProgress *p)
{
    if(!p || p->stroke || p->complete)return -1;
    unsigned used=p->reveal*10u/288u;
    return used<10 ? 9-(int)used:-1;
}
#endif
