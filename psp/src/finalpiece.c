#include "finalpiece.h"
int finalpiece_tick(FinalPiece *p,int tagged,int cinematic,int phase)
{
    if(p->deleted)return 0;
    if(++p->frame>=(p->collapsing ? 1:500))p->frame=0;
    if(cinematic)return 0;
    p->collision_frame=p->frame;p->collision_old_frame=p->old_frame;
    p->collision_collapse_frame=p->collapse_frame;p->collision_collapsing=p->collapsing;
    int advance=0;
    if(tagged && ++p->tag_ticks>90) {
        if(!p->collapsing) {
            p->old_frame=p->frame;p->collapse_frame=0;p->collapsing=1;advance=1;
        } else if(phase<5) {
            if(++p->old_frame>=500)p->old_frame=0;
            if(++p->collapse_frame>=640){p->collapse_frame=639;p->deleted=1;}
        } else p->deleted=1;
        p->frame=0;
    }
    return advance;
}
