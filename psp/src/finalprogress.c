#include "finalprogress.h"
FinalProgressEvents finalprogress_tick(FinalProgress *s,int *phase,int status,int alive)
{
    FinalProgressEvents e={.preload=-1,.play=-1};
    if(s->contact) {
        if((s->flags&1) && status<0) {
            e.reposition=1;(*phase)++;s->flags&=~3u;s->contact=0;
        } else {
            e.play=90;e.remove_environment=1;s->flags|=1;
        }
    } else if(*phase==3 && !(s->flags&2)) {
        e.preload=90;s->flags|=2;
    } else if(*phase==5 && !(s->flags&2)) {
        if(!alive){e.preload=103;s->flags|=2;}
    } else if(*phase==6) {
        e.play=103;(*phase)++;
    } else if(*phase==7 && status<0) {
        e.clear=1;(*phase)++;
    }
    return e;
}
void finalprogress_contact(FinalProgress *s,int phase,int cinematic,int contact)
{ if(phase==4 && !cinematic && contact)s->contact=1; }
void finalprogress_boss_tag(int *phase,int cinematic,int complete)
{ if(*phase==5 && !cinematic && complete)*phase=6; }
float finalprogress_rate(int phase)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_finalprogress_c__rate_0.inc"
#else

#include "generated/source_data/finalprogress_c__rate_0.inc"

#endif

    return (unsigned)phase<9 ? rate[phase]:0;
}
