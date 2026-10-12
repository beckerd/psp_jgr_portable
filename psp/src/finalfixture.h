/* Opt-in EBOOT regression only (`finaltest` = 5). This explicitly places
   the player in original volumes and supplies stick/L input. It never
   writes a tag, reveal, collapse, phase, scene-completion or result flag.
   Placements/contact query and per-target cans are fixtures, not route parity. */
#ifndef FINALFIXTURE_H
#define FINALFIXTURE_H
#include "finalarena.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_spraytab.inc"
#else
#include "generated/source_data/spraytab.h"
#endif
typedef struct {int initialized,target,cue,segment,step,angle,ticks,captured;} FinalFixture;
static void finalfixture_input(FinalFixture *f,FinalArena *a,SceCtrlData *pad,unsigned *pressed)
{
    Graffiti *g=a->graffiti;Player *p=a->player;
    if(!g || !p || a->progression_error)return;
    if(!f->initialized){f->initialized=1;f->target=f->cue=-1;}
    f->ticks++;
    pad->Buttons=*pressed=0;pad->Lx=pad->Ly=128;
    int target=-1;
    for(int i=0;i<4;i++)if(!g->spots[i].complete){target=i;break;}
    if(a->phase==5 && !g->spots[4].started)target=4;
    if(target<0) {
        if(a->phase==4 && !a->progression.contact) {
            for(int i=0;i<a->collider.nworld;i++) {
                if(a->collider.nodes[a->collider.world[i].src].piece!=4)continue;
                float point[3],delta[3];int yaw;
                for(int k=0;k<3;k++)point[k]=a->collider.world[i].v[k];
                a->world->surface_motion(a->world->surface_context,i,point,delta,&yaw);break;
            }
        }
        return;
    }
    Spot *s=g->spots+target;if(!s->active)return;
    float point[3]={0};for(int k=0;k<3;k++)for(int j=0;j<6;j++)point[k]+=s->pts[j*3+k]/6.f;
    if(!g->mg_active) {
        /* Beat holds only 20 cans. A 29-can initial fixture gets clamped
           by normal pickup updates and can never finish all 28 strokes.
           Supply each target's budget explicitly, without changing the
           character limit or any tag/progression state. */
        if(f->target!=target){g->cancount=target==4 ? 1:7;f->target=target;}
        /* A supplied placement must retire the previous platform query
           and airborne state, just like the game's reposition handler. */
        player_reposition(p,point);
        *pressed=GAME_SPRAY;pad->Buttons=*pressed;f->cue=-1;return;
    }
    if(g->mg_wait || s->stroke>=s->nstrokes){f->cue=-1;return;}
    int cue=g->mg_cue;const int *c=CUES[cue];
    if(f->target!=target || f->cue!=cue || f->segment!=g->input.cue){f->target=target;f->cue=cue;f->segment=g->input.cue;f->step=0;f->angle=(c[0]+c[1])/2;}
    int magnitude=f->step!=0;
    if(f->step>=2 && c[6])f->angle+=(c[6]==1 ? 1:-1)*1024;
    float rad=f->angle*(6.283185307179586f/65536.f);
    /* Physical screen directions, matching the displayed graffiti arrow. */
    pad->Lx=128+(int)lroundf(sinf(rad)*127*magnitude);
    pad->Ly=128-(int)lroundf(cosf(rad)*127*magnitude);f->step++;
}
#endif
