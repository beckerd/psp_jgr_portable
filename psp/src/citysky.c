#include <math.h>
#include <stdio.h>
#include <string.h>
#include "citysky.h"
#include "player.h"
typedef struct {
    const char *file;
    int model[3],nmodels,ntextures,world;
    unsigned object_count,nobjects,objects[2];
} CitySkyEntry;
/* Source 02f5a4 / 039af0 / 031aa8 / 0f346c / 0f3aac -> 0c34e8.
   Object indices are checked against the exact JGS source maps by the
   asset regression. Null original slots remain null (not compacted). */
static const CitySkyEntry entries[7]={
    {0},
    {"staffsky.jgc",{0,1,-1},2,1,0,1136,1,{1135}},
    {"sky2.jgc",{0,1,2},3,2,0,1051,2,{1049,1050}},
    {"sky3.jgc",{0,-1,1},2,1,1,1647,2,{1645,1646}},
    {0}, /* STAGELAST owns its animated background in finalarena. */
    {"sky5.jgc",{-1,-1,0},1,0,0,497,0,{0}},
    {"sky6.jgc",{0,-1,1},2,1,1,542,0,{0}}
};
int citysky_bind_stage(Stage *stage,int id)
{
    if(id<1 || id>6 || !entries[id].file)return 0;
    const CitySkyEntry *e=entries+id;
    if(!stage->h || (e->object_count && stage_render_near_objects(stage)!=e->object_count))return -1;
    for(unsigned k=0;k<e->nobjects;k++)if(e->objects[k]>=stage->h->n_obj)return -2;
    stage->sky_explicit=1;
    for(unsigned k=0;k<e->nobjects;k++)stage->obj[e->objects[k]].flags|=JGS_O_SKY;
    return 0;
}
void citysky_free(CitySky *sky)
{chars_free(&sky->models);memset(sky,0,sizeof *sky);}
int citysky_load(CitySky *sky,int id)
{
    citysky_free(sky);sky->stage=id;sky->sector=sky->pending_sector=-1;sky->visible=1;
    if(id<1 || id>6 || !entries[id].file)return 0;
    const CitySkyEntry *e=entries+id;
    int error=chars_load(&sky->models,e->file);
    if(!error && (sky->models.h->n_chars!=(unsigned)e->nmodels ||
                  sky->models.h->n_tex!=(unsigned)e->ntextures))error=-10;
    if(error)chars_free(&sky->models);
    return sky->error=error;
}
void citysky_sector(CitySky *sky,int sector)
{
    if(sector<0)return;
    sky->sector=sky->pending_sector=sector;
    if(sky->stage!=2)return;
    /* 03a38c: all other sectors are NO-OPS, including 103 and 107.
       0c34e8 clears/restores all three roots/texlists/rates, not angles. */
    switch(sector) {
    case 54:case 55:case 60:case 99:case 102:case 105:case 106:case 109:
        sky->visible=0;break;
    case 36:case 101:case 104:case 108:sky->visible=1;break;
    }
}
void citysky_camera(CitySky *sky,const Zones *zones,const float eye[3],
                    const float *event_forward,const float *handoff)
{
    if(sky->stage!=2 || !zones || !eye)return;
    float point[3];
    for(int j=0;j<3;j++) {
        point[j]=handoff ? handoff[j]:event_forward ? eye[j]+event_forward[j]:eye[j];
        if(!isfinite(point[j]))return;
    }
    int sector=zone_of(zones,point[0],point[1],point[2]);
    if(sector>=0)sky->pending_sector=sector;
}
void citysky_step(CitySky *sky,float dt)
{
    if(!sky->models.h)return;
    if(isfinite(dt) && dt>0.f)sky->remainder+=dt*30.f;
    unsigned ticks=(unsigned)sky->remainder;sky->remainder-=ticks;
    /* Source sector detection follows camera draw; the stage callback
       consumes that result next tick. Keep this pending across EVENT
       cleanup: do not unconditionally re-enable skies on scene exit. */
    if(ticks && sky->pending_sector!=sky->sector)citysky_sector(sky,sky->pending_sector);
    /* Disabled 0c34e8 rates are zero. Consume time without accumulating
       an angle jump, and leave GU state untouched when all roots are null. */
    if(!sky->visible)return;
    /* All five original loaders supply rates {0,10,0}. The third slot
       is NOT advanced by either source draw routine, even if non-null. */
    sky->yaw[entries[sky->stage].world][1]+=10u*ticks;
}
void citysky_draw(CitySky *sky,const float eye[3],const float planes[6][4],float dt)
{
    if(!sky->models.h || !planes)return;
    citysky_step(sky,dt);
    if(!sky->visible)return;
    const CitySkyEntry *e=entries+sky->stage;
    /* 0c3688 retains view translation; 0c3568 clears it. */
    int world=e->world;
    const float origin[3]={0,0,0},scale[3]={1,1,1};
    for(int slot=0;slot<3;slot++)if(e->model[slot]>=0) {
        int32_t angle[3]={0,(int32_t)(sky->yaw[world][slot]&65535u),0};float root[16];
        player_scene_matrix(world ? origin:eye,angle,scale,root);
        chars_draw_background(&sky->models,e->model[slot],root,planes);
    }
}
