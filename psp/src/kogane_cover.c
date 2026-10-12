#include "kogane_cover.h"
#include <string.h>
#if JGR_PORTABLE_ASSETS
#include "source_data_kogane_cover_data.inc"
#else
#include "generated/source_data/kogane_cover_data.inc"
#endif
int kogane_cover_load(KoganeCover *c,Collision *collision)
{
    kogane_cover_free(c);c->collision=collision;
    int error=chars_load(&c->models,"kogane_cover.jgc");
    if(!error && c->models.h->n_chars!=2)error=-3;
    if(error)kogane_cover_free(c);
    return error;
}
void kogane_cover_free(KoganeCover *c)
{
    if(c->handle)col_surface_remove(c->collision,c->handle);
    chars_free(&c->models);memset(c,0,sizeof *c);
}
int kogane_cover_select(KoganeCover *c,unsigned flags)
{
    if(!c->models.h)return 0;
    if(c->handle)col_surface_remove(c->collision,c->handle);
    /* 039af0 -> 0399a0, JMDc +e0 bit 0 switches both F and DYN lists.
       The open construction contacts were removed from static stage2.col. */
    c->closed=(flags&1)!=0;
    c->handle=col_surface_add(c->collision,c->closed ? cover_closed:cover_open,NULL,
        c->closed ? sizeof cover_closed/sizeof *cover_closed:sizeof cover_open/sizeof *cover_open);
    return c->handle ? 0:-2;
}
void kogane_cover_draw(KoganeCover *c,const Zones *zones,int sector,const float eye[3],const float planes[6][4],int pass)
{
    if(!c->closed || !c->models.h || !zones || !zone_neighbour(zones,sector,22))return;
    float dx=eye[0]-cover_position[0],dy=eye[1]-cover_position[1],dz=eye[2]-cover_position[2];
    int lod=dx*dx+dy*dy+dz*dz>=90000.f;
    float root[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    root[3]=cover_position[0];root[7]=cover_position[1];root[11]=cover_position[2];
    const float *v=cover_lights[(unsigned)sector<114 ? sector:22];
    CharLight light={{v[3],v[4],v[5]},v[6],v[7],{0xff000000u |
        ((unsigned)(v[0]*255)<<16) | ((unsigned)(v[1]*255)<<8) | (unsigned)(v[2]*255),0},0};
    chars_scene_lighting(&light,NULL);
    chars_draw_material_pass_clipped(&c->models,lod,root,-1,0,pass,planes);
    chars_scene_lighting(NULL,NULL);
}
