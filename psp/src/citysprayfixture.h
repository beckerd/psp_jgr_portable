#ifndef JGR_CITY_SPRAY_FIXTURE_H
#define JGR_CITY_SPRAY_FIXTURE_H
/* Opt-in spraytest=3: one explicit city volume placement and initial can
   budget, followed by ordinary L/nub gestures. Never forces progress, pose,
   camera, invulnerability or campaign result flags. NOT a playthrough. */
#if JGR_PORTABLE_ASSETS
#include "source_data_spraytab.inc"
#else
#include "generated/source_data/spraytab.h"
#endif
typedef struct {int target,entered,cue,segment,step,angle,done,ticks,captured,search_from;} CitySprayFixture;
static void citysprayfixture_input(CitySprayFixture *f,Graffiti *g,Player *p,SceCtrlData *pad,unsigned *pressed)
{
    pad->Lx=pad->Ly=128;pad->Buttons=*pressed=0;
    if(f->done)return;
    f->ticks++;
    if(f->target<0) {
        for(int offset=0;offset<g->n;offset++) {
            int i=(f->search_from+offset)%g->n;
            Spot *s=g->spots+i;
            if(!s->active || !s->required || !s->source || !s->nstrokes || !s->hasvol || s->tagged || s->complete)continue;
            float point[3]={0};for(int j=0;j<6;j++)for(int k=0;k<3;k++)point[k]+=s->pts[j*3+k]/6.f;
            int inside=1;for(int j=0;j<6;j++) {
                double d=0;for(int k=0;k<3;k++)d+=(double)(s->pts[j*3+k]-point[k])*s->nrm[j*3+k];if((float)d<0)inside=0;
            }
            if(!inside)continue;
            f->target=i;g->cancount=10;player_reposition(p,point);f->cue=-1;break;
        }
        if(f->target<0){f->done=-1;return;}
    }
    Spot *s=g->spots+f->target;
    if(!g->mg_active) {
        if(f->entered){f->done=s->complete ? 1:-2;return;}
        *pressed=GAME_SPRAY;pad->Buttons=*pressed;return;
    }
    f->entered=1;if(g->near!=f->target){f->done=-3;return;}
    if(g->mg_wait || s->stroke>=s->nstrokes){f->cue=-1;return;}
    int cue=g->mg_cue;const int *c=CUES[cue];
    if(f->cue!=cue || f->segment!=g->input.cue){f->cue=cue;f->segment=g->input.cue;f->step=0;f->angle=(c[0]+c[1])/2;}
    int magnitude=f->step!=0;
    if(f->step>=2 && c[6])f->angle+=(c[6]==1 ? 1:-1)*1024;
    float rad=f->angle*(6.283185307179586f/65536.f);
    pad->Lx=128+(int)lroundf(sinf(rad)*127*magnitude);pad->Ly=128-(int)lroundf(cosf(rad)*127*magnitude);f->step++;
}
#endif
