#include "benten_crane.h"
#include "charpose.h"
#include "source_trs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct {unsigned address,sector;float position[3];int32_t angles[3];float scale[3];} CranePlacement;
typedef struct {unsigned indices[3];unsigned short surface,mask,flags;} CraneTriangle;
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_benten_crane_data.inc"
#else
#include "generated/source_data/benten_crane_data.inc"
#endif
#define CRANE_TRIANGLES (sizeof crane_triangles/sizeof *crane_triangles)
typedef struct {
    unsigned active,phase,frame,buffer,valid,handle;float root[16];
    ColTri triangles[2][CRANE_TRIANGLES];CharPose pose[2];
} CraneInstance;
struct BentenCrane {
    Chars models;Collision *collision;CraneInstance objects[sizeof crane_placements/sizeof *crane_placements];
    unsigned sector,ticks,draws;
};
static void crane_retire(BentenCrane *h,CraneInstance *o)
{
    if(o->handle)col_surface_remove(h->collision,o->handle);
    o->active=o->phase=o->frame=o->buffer=o->valid=o->handle=0;
}
void benten_crane_reset(BentenCrane *h)
{
    if(!h)return;
    for(unsigned i=0;i<sizeof h->objects/sizeof *h->objects;i++)crane_retire(h,h->objects+i);
    h->ticks=h->draws=0;
}
void benten_crane_free(BentenCrane **owner)
{
    if(!owner || !*owner)return;
    BentenCrane *h=*owner;benten_crane_reset(h);chars_free(&h->models);free(h);*owner=NULL;
}
int benten_crane_load(BentenCrane **owner,Collision *collision)
{
    benten_crane_free(owner);BentenCrane *h=calloc(1,sizeof *h);if(!h)return -2;
    h->collision=collision;int error=chars_load(&h->models,"benten_crane.jgc");
    if(!error && (h->models.h->n_chars!=1 || h->models.chars->nbones!=2 ||
        h->models.h->n_motions!=1 || h->models.motions->nframes!=500))error=-3;
    if(error){benten_crane_free(&h);return error;}
    for(unsigned i=0;i<sizeof h->objects/sizeof *h->objects;i++){
        CraneInstance *o=h->objects+i;const CranePlacement *p=crane_placements+i;
        for(int k=0;k<16;k++)o->root[k]=(k%5)==0;
        source_trs_apply(p->position,p->angles,p->scale,o->root,player_fsca_sin);
    }
    *owner=h;return 0;
}
static void crane_pose(BentenCrane *h,CraneInstance *o)
{
    for(unsigned i=0;i<2;i++)chars_sample_pose(&h->models,h->models.chars,0,o->frame,i,o->pose+i);
}
static int crane_geometry(BentenCrane *h,CraneInstance *o)
{
    float matrices[2][16],points[16][3];
    for(unsigned b=0;b<2;b++){
        /* 0be56c: collision consumes all bind channels. The render tree's
           eval flags only suppress unkeyed channels in the visible mesh. */
        CharPose p=crane_bind[b];const CharPose *v=o->pose+b;
        if(v->keyed&1)memcpy(p.pos,v->pos,12);
        if(v->keyed&2)memcpy(p.ang,v->ang,12);
        if(v->keyed&4)memcpy(p.scl,v->scl,12);
        int32_t ang[3]={p.ang[0],p.ang[1],p.ang[2]};
        memcpy(matrices[b],b ? matrices[0]:o->root,sizeof matrices[b]);
        source_trs_apply(p.pos,ang,p.scl,matrices[b],player_fsca_sin);
    }
    for(unsigned i=0;i<16;i++)source_point(matrices[1],crane_vertices[i],points[i]);
    unsigned next=o->buffer^1u;
    for(unsigned i=0;i<CRANE_TRIANGLES;i++){
        ColTri *t=o->triangles[next]+i;const CraneTriangle *s=crane_triangles+i;
        t->surf=s->surface;t->mask=s->mask;t->dbl=s->flags;t->src=0;
        for(unsigned v=0;v<3;v++)memcpy(t->v+v*3,points[s->indices[v]],12);
    }
    if(!o->valid)memcpy(o->triangles[o->buffer],o->triangles[next],sizeof o->triangles[0]);
    if(o->handle){if(!col_surface_publish(h->collision,o->handle,o->triangles[next],o->triangles[o->buffer],CRANE_TRIANGLES))return -4;}
    else {o->handle=col_surface_add(h->collision,o->triangles[next],o->triangles[o->buffer],CRANE_TRIANGLES);if(!o->handle)return -4;}
    o->valid=1;o->buffer=next;return 0;
}
int benten_crane_tick(BentenCrane *h,const Player *p,const Zones *zones,int sector)
{
    if(!h)return 0;
    h->ticks++;if((unsigned)sector<46)h->sector=sector;
    for(unsigned i=0;i<sizeof h->objects/sizeof *h->objects;i++){
        CraneInstance *o=h->objects+i;const CranePlacement *place=crane_placements+i;
        if(zones && !zone_neighbour(zones,sector,place->sector)){if(o->active)crane_retire(h,o);continue;}
        o->active=1;
        /* 0e3ce0: two startup ticks, then +1 at 30 Hz; reset at nframes-1. */
        if(o->phase<2)o->phase++;
        else if(++o->frame>=h->models.motions->nframes-1)o->frame=0;
        crane_pose(h,o);
        float dx=p->x-place->position[0],dy=p->y-place->position[1],dz=p->z-place->position[2];
        if((dx*dx+dy*dy)+dz*dz<62500.f){int error=crane_geometry(h,o);if(error)return error;}
        else if(o->handle){col_surface_remove(h->collision,o->handle);o->handle=o->valid=0;}
    }
    return 0;
}
void benten_crane_draw(BentenCrane *h,const float planes[6][4],const ObjectView *medium,int pass)
{
    if(!h)return;
    const float *v=crane_lights[h->sector];
    CharLight light={{v[3],v[4],v[5]},v[6],v[7],{0xff000000u|((unsigned)(v[0]*255)<<16)|((unsigned)(v[1]*255)<<8)|(unsigned)(v[2]*255),0},0};
    chars_scene_lighting(&light,NULL);chars_world_frustum(planes);
    for(unsigned i=0;i<sizeof h->objects/sizeof *h->objects;i++)if(h->objects[i].active){
        /* 0e3e4c rejects the placement through 0c3498 before posing or
           submitting its mesh; collision and motion clocks keep running. */
        const float *p=crane_placements[i].position;
        if(!object_circle_visible(medium,p[0],p[1],p[2]))continue;
        h->models.drawn_tris=0;chars_draw_material_pose_clipped(&h->models,0,h->objects[i].root,h->objects[i].pose,pass,planes);
        if(h->models.drawn_tris)h->draws++;
    }
    chars_world_frustum(NULL);chars_scene_lighting(NULL,NULL);
}
void benten_crane_info(const BentenCrane *h,unsigned out[5])
{
    memset(out,0,5*sizeof *out);if(!h)return;
    out[0]=h->ticks;out[1]=h->objects[0].active;out[2]=h->objects[0].frame;out[3]=!!h->objects[0].handle;out[4]=h->draws;
}
