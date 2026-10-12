#include "finalfall.h"
static void fade_start(FinalFall *f,float step)
{
    /* 020ea0. A negative transition preserves the visibility byte. */
    f->fade_phase=1;f->step=step;
    if(step<0.f)f->alpha=1.f;
    else {f->alpha=0.f;f->visible=1;}
}
int finalfall_stage(FinalFall *f,float y,int cinematic)
{
    int respawn=0;
    switch(f->phase) {
    case 0:if(!cinematic && y< -100.f)f->phase=1;break;
    case 1:fade_start(f,0x1.47ae14p-5f);f->phase=2; /* 0e544c = 0.04f */
        /* fall through */
    case 2:if(f->fade_phase)break;f->phase=3;
        /* fall through */
    case 3:respawn=1;fade_start(f,-0x1.47ae14p-5f);f->phase=4;
        /* fall through */
    case 4:if(!f->fade_phase){f->phase=0;f->visible=0;f->alpha=f->step=0;}break;
    }
    return respawn;
}
void finalfall_fade(FinalFall *f)
{
    /* 02106c: strict bounds, not >= / <=. Do the float additions; a
       rounded integer duration would lose the actual intermediate alphas. */
    if(f->fade_phase!=1)return;
    f->alpha+=f->step;
    if(f->alpha>1.f){f->alpha=1.f;f->fade_phase=0;}
    else if(f->alpha<0.f){f->alpha=0.f;f->fade_phase=0;f->visible=0;}
}
