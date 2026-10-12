#include "hud_layout.h"
#include "pause_map.h"
#include "graffiti.h"
#include "font.h"
#include <pspgu.h>
#include <string.h>
#include <stdio.h>
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_pause_map_entries.inc"
#else
#include "generated/source_data/pause_map_entries.inc"
#endif
static void map_matrix(int mode,const float v[16])
{ScePspFMatrix4 m;float *p=(float*)&m;for(int r=0;r<4;r++)for(int c=0;c<4;c++)p[c*4+r]=v[r*4+c];sceGuSetMatrix(mode,&m);}
void pause_map_select(PauseMap *p,int area,const Graffiti *g)
{
    p->region=pause_map_region(p->stage,p->chapter,p->kind,area);
    p->layout=pause_map_layout(p->chapter,p->region);
    /* These missions span several local maps. Use the original city overview
       after clearing a section, including Benten's empty connecting area.
       Recompute on START: entering an unfinished area or retrying restores
       its local map, without retaining a separate progression latch. */
    int explosion=p->stage==0 && p->chapter==4;
    int fight=p->stage==1 && p->chapter==8;
    int burning=p->stage==2 && p->chapter==12;
    int overview=burning ? 5:4;
    if((!explosion && !fight && !burning) || p->kind!=0 || (unsigned)area>=(unsigned)overview || !g)return;
    int local_required=0,local_remaining=0,other_remaining=0;
    for(int i=0;i<g->n;i++){
        const Spot *s=g->spots+i;
        if(!s->active || !s->required || (unsigned)s->area>=6)continue;
        int remaining=!s->tagged && !s->complete;
        if(s->area==area){local_required++;local_remaining+=remaining;}
        else other_remaining+=remaining;
    }
    if((local_required || burning) && !local_remaining && other_remaining)
        p->region=p->layout=overview;
}
const PauseMapEntry *pause_map_entry(const PauseMap *p)
{
    if((unsigned)p->stage>=6 || (unsigned)p->layout>=PAUSE_MAP_COUNTS[p->stage])return NULL;
    return &PAUSE_MAPS[p->stage][p->layout];
}
int pause_map_load(Chars *b)
{
    if(chars_load(b,"pausemap.jgc")){chars_free(b);return -1;}
    if(b->h->n_chars!=25 || b->h->n_tex || b->h->n_motions!=6){chars_free(b);return -1;}
    return 0;
}
void pause_map_marker_matrix(const PauseMapEntry *e,const PauseMapMarker *p,float (*sine)(int),float m[16])
{
    /* 034ca0 / 103404 / 103450 / 1033d8, keeping the source f32 steps. */
    float factor=.01024f-e->scale*.012f;
    float tx=p->x*factor,ty=-(p->z*factor),s=.035f*p->size/e->scale;
    float sx=e->scale*s,sz=e->scale*(.035f/e->scale);
    float sn=sine(p->angle),cs=sine(p->angle+16384);
    memset(m,0,16*sizeof *m);m[0]=sx;m[5]=sx;m[10]=sz;
    if(p->angle){m[0]=sx*cs;m[1]=-sx*sn;m[4]=sx*sn;m[5]=sx*cs;}
    m[3]=e->scale*tx;m[7]=e->scale*ty;m[11]=(float)((double)e->scale*p->depth+e->z);m[15]=1;
}
void pause_map_draw(Chars *b,const PauseMap *p,const PauseMapMarker *markers,int count,float (*sine)(int),const float projection[16],const float view[16])
{
    const PauseMapEntry *e=pause_map_entry(p);if(!e || !b->h)return;
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    /* The original map's gray/outline layers can be only 0.000034
       apart after projection. A world-sized depth interval quantizes
       both to the same PSP 16-bit value. All original pause/map/icon
       vertices stay inside .355..936; keep this HUD-only range tight. */
    const float near=.3f,far=1.f;
    float hp[16]={500.f/320.f,0,1.f-2*e->x/640.f,0,0,500.f/240.f,-(1.f-2*e->y/480.f),0,
        0,0,(far+near)/(near-far),2.f*far*near/(near-far),0,0,-1,0};
    hud_projection(hp,HUD_CENTRE);
    float root[16]={e->scale,0,0,0,0,e->scale,0,0,0,0,e->scale,e->z,0,0,0,1};
    const CharLight light={{1,1,1},1.f/e->scale,.5f/e->scale,{0xffffffff,0xffffffff},1};
    b->drawn_tris=0;map_matrix(GU_VIEW,identity);map_matrix(GU_PROJECTION,hp);
    sceGuClear(GU_DEPTH_BUFFER_BIT);sceGuDepthFunc(GU_GEQUAL);chars_scene_lighting(&light,NULL);
    chars_hud_depth(2,0); /* 02f064 -> 02f000: reciprocal Z / .5 */
    for(int j=0;j<6 && e->models[j]>=0;j++)chars_draw_material(b,e->models[j],root,NULL,-1,0,1,NULL);
    for(int j=0;j<count;j++){
        const PauseMapMarker *m=markers+j;if((unsigned)m->icon>=9)continue;
        if(m->icon==0 && (p->stage==3 || p->clock%30>=15))continue;
        if((m->icon==1 || (m->icon>=5 && m->icon<=7)) && p->clock%30>=15)continue;
        int model=PAUSE_MAP_ICONS[m->icon],motion=b->chars[model].nmotions ? 0:-1;
        /* Animated 0ba228 -> mode 7 / 0ba260: q / (1 - q), not
           unmodified depth and not the static mesh's doubled q. */
        chars_hud_depth(motion<0 ? 2:1,motion<0 ? 0:1);
        pause_map_marker_matrix(e,m,sine,root);
        float frame=motion<0 ? 0:p->clock%b->motions[b->chars[model].motion0].nframes;
        chars_draw_material(b,model,root,NULL,motion,frame,1,NULL);
    }
    chars_hud_depth(1,0);chars_scene_lighting(NULL,NULL);sceGuDepthMask(GU_FALSE);sceGuDepthFunc(GU_GEQUAL);
    map_matrix(GU_PROJECTION,projection);map_matrix(GU_VIEW,view);map_matrix(GU_MODEL,identity);
}
void pause_map_text(const PauseMap *p)
{
    const float sx=HUD_SCALE,sy=HUD_SCALE;const unsigned yellow=0xff00e6e6;
    if(p->kind==0 || p->kind==1){
        char digits[32],number[16];snprintf(number,sizeof number,"%d",p->remaining);
        int n=0;for(const char *q=number;*q && n<30;q++){
            int d=*q=='-' ? 10:*q-'0';if((unsigned)d<11){digits[n++]=PAUSE_MAP_DIGITS[d][0];digits[n++]=PAUSE_MAP_DIGITS[d][1];}
        }digits[n]=0;
        font_real_draw_xy(hud_x(PAUSE_MAP_TEXT_XY[0],HUD_CENTRE),PAUSE_MAP_TEXT_XY[1]*sy,sx,sy,yellow,PAUSE_MAP_TEXT[0]);
        font_real_draw_xy(hud_x(PAUSE_MAP_TEXT_XY[2],HUD_CENTRE),PAUSE_MAP_TEXT_XY[3]*sy,sx,sy,yellow,p->stage==3 ? PAUSE_MAP_TEXT[3]:digits);
    }else if(p->kind==2){
        font_real_draw_xy(hud_x(90,HUD_CENTRE),165*sy,sx,sy,yellow,PAUSE_MAP_TEXT[1]);
        font_real_draw_xy(hud_x(90,HUD_CENTRE),205*sy,sx,sy,yellow,PAUSE_MAP_TEXT[2]);
    }
}
