/* Synthetic event sequences exercise the shared source task clock. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../src/opening_state.h"
int main(void)
{
    OpeningState s={.mirror=1};
    /* Proximity and floor events cannot start a hanging lantern frame. */
    for(unsigned tick=0;tick<180;tick++){
        s.floor=1;assert(!opening_state_tick(&s,OPENING_REACTIVE,90,0,0));
        assert(s.phase==1 && s.frame==0 && !s.floor);
    }
    for(int side=-1;side<=1;side+=2){
        s.contact=1;assert(opening_state_tick(&s,OPENING_REACTIVE,90,side,0));
        assert(s.phase==2 && s.frame==0 && s.mirror==1 && !s.contact);
        /* Continuous contact must neither restart nor extend this cycle. */
        for(unsigned frame=1;frame<90;frame++){
            s.contact=1;assert(!opening_state_tick(&s,OPENING_REACTIVE,90,side,1e6f));
            assert(s.phase==2 && s.frame==frame && s.mirror==1 && !s.contact);
        }
        s.contact=1;assert(!opening_state_tick(&s,OPENING_REACTIVE,90,side,0));
        assert(s.phase==1 && s.frame==0 && !s.contact);
        /* Once retired, it stays idle until a fresh collision arrives. */
        assert(!opening_state_tick(&s,OPENING_REACTIVE,90,side,0));
        assert(s.phase==1 && s.frame==0);
    }
    /* Existing gates still reverse their closing motion; barriers stay broken. */
    s=(OpeningState){.phase=1,.contact=1,.mirror=1};
    assert(opening_state_tick(&s,OPENING_GATE,60,1,0) && s.mirror==-1);
    for(unsigned frame=0;frame<59;frame++)opening_state_tick(&s,OPENING_GATE,60,0,0);
    assert(s.phase==3 && s.frame==59);
    opening_state_tick(&s,OPENING_GATE,60,0,10000);assert(s.phase==3);
    opening_state_tick(&s,OPENING_GATE,60,0,nextafterf(10000,INFINITY));assert(s.phase==4);
    for(unsigned frame=0;frame<59;frame++)opening_state_tick(&s,OPENING_GATE,60,0,0);
    assert(s.phase==1 && s.frame==0);
    s=(OpeningState){.phase=1,.contact=1,.mirror=1};
    assert(opening_state_tick(&s,OPENING_BARRIER,60,-1,0));
    for(unsigned frame=0;frame<180;frame++)opening_state_tick(&s,OPENING_BARRIER,60,0,1e6f);
    assert(s.phase==3 && s.frame==59 && s.mirror==1);
    puts("Contact-only 90-frame cycles, last-frame retention, retrigger, gate closing and persistent barriers passed.");
}
