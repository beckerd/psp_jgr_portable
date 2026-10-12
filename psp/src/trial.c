#include <string.h>
#include "trial.h"

void trial_begin(Trial *t,const TrialEntry *entries,int count)
{ memset(t,0,sizeof *t);t->entries=entries;t->count=count;t->result_bound=1; }
static void result(Trial *t,int value)
{ if(t->result_bound)t->result=value; }
int trial_inside(const float *q,const float feet[3])
{
    /* 0c7ff4 subtracts point FROM each plane origin, then 103d9c FIPR.
       Accumulate products with guard precision like the SH-4 interpreter;
       a boundary (zero) is inside. Do not add a body-height offset/epsilon. */
    for(int i=0;i<6;i++) {
        float x=q[i*3]-feet[0],y=q[i*3+1]-feet[1],z=q[i*3+2]-feet[2];
        double dot=(double)x*q[18+i*3];dot+=(double)y*q[19+i*3];dot+=(double)z*q[20+i*3];
        if((float)dot<0.f)return 0;
    }
    return 1;
}
static int inside(const TrialEntry *e,const float feet[3],int failure)
{
    unsigned first=failure ? e->na:0,count=failure ? e->nb:e->na;
    if(!e->quads)return 0;
    for(unsigned i=0;i<count;i++)if(trial_inside(e->quads+(first+i)*36,feet))return 1;
    return 0;
}
static void advance(Trial *t,int complete)
{
    t->index++;t->distance=t->counter=0.f; /* 08e578 / 08e5c0 */
    /* A malformed/truncated chain is NOT a successful type-9 sentinel. */
    if(complete && t->index<t->count && t->entries[t->index].type==9)result(t,1);
}
static float word_float(unsigned word)
{ float value;memcpy(&value,&word,sizeof value);return value; }
void trial_action(Trial *t,int action,const float feet[3],float speed,int air_trick)
{
    if(action==TRIAL_FAIL){result(t,2);return;} /* 08e91c needs no +610 */
    if(!t->entries || t->index<0 || t->index>=t->count)return;
    const TrialEntry *e=t->entries+t->index;
    if(action==TRIAL_ADVANCE || action==TRIAL_ADVANCE_DEFER){advance(t,action==TRIAL_ADVANCE);return;}
    if(action==TRIAL_FINISH){if(e->type==9)result(t,1);return;} /* 08e7f0 */
    if(action==TRIAL_FAIL_VOLUME){if(inside(e,feet,1))result(t,2);return;} /* 08e5ec */
    if(!inside(e,feet,0))return; /* 0c0c70: ANY of A, not all */
    switch(action) {
    case TRIAL_GRIND: case TRIAL_WALL: /* 08e62c / 08e6c4 */
        if(e->type==(action==TRIAL_GRIND ? 0u:2u)) {
            t->distance+=speed;
            if(!(word_float(e->p1)>t->distance))advance(t,1);
        } else if(e->type==(action==TRIAL_GRIND ? 1u:3u)) {
            if(!(word_float(e->p2)>t->counter))advance(t,1);
        }
        break;
    case TRIAL_TAG: case TRIAL_TAG_DEFER: /* 08e760 / 08e7a8 */
        if(e->type==5)advance(t,action==TRIAL_TAG);
        break;
    case TRIAL_GROUND_TRICK: /* 08e818: 06c048 brake-release gesture */
        if(e->type==6)advance(t,1);
        break;
    case TRIAL_JUMP: /* 08e860: jump creation flags &800, NOT landing */
        if(e->type==4){if(air_trick)advance(t,1);else result(t,2);}
        else if(e->type==7)advance(t,1);
        break;
    case TRIAL_GROUND_SIX: /* 08e8d4: 06cf70 increments +f8 to exactly 6 */
        if(e->type==8)advance(t,1);
        break;
    }
}
