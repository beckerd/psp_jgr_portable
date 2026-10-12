#include "grind_props.h"
#include "grind_lift_state.h"
#include "charpose.h"
#include "player_body.h"
#include "source_body.h"
#include "source_trs.h"
#include "stage.h"
#include "audio.h"
#include "gfx.h"
#include <pspgu.h>
#include <pspgum.h>
#include <stdlib.h>
#include <string.h>
typedef struct {float pos[3];int32_t ang[3];float scale[3];} GrindPlacement;
typedef struct {float vertices[9];unsigned bone,surface,mask,flags;} GrindTriangle;
typedef struct {unsigned first,count,texture,period,times[8];float position[3];unsigned source_flags;} GrindLight;
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_grind_props_data.inc"
#else
#include "generated/source_data/grind_props_data.inc"
#endif
#define GRIND_TRIANGLES (sizeof grind_triangles/sizeof *grind_triangles)
#define GRIND_LIGHTS (sizeof grind_lights/sizeof *grind_lights)
typedef struct {
    GrindLiftState state;float root[16],box[24],low[3],high[3];CharPose pose[6];
    ColTri floors[2][GRIND_TRIANGLES];unsigned handle,buffer,valid,sound[4],pose_frame,geometry_frame,settled;float distance2;
} GrindLift;
struct GrindProps {Chars models;Stage lights;Collision *collision;GrindLift lift[2];unsigned clocks[GRIND_LIGHTS],ticks,draws;};
void grind_props_reset(GrindProps *h)
{
    if(!h)return;
    for(unsigned i=0;i<2;i++){
        GrindLift *o=h->lift+i;if(o->handle)col_surface_remove(h->collision,o->handle);
        for(unsigned k=0;k<4;k++){if(o->sound[k])audio_world_stop(o->sound[k]);o->sound[k]=0;}
        memset(&o->state,0,sizeof o->state);o->handle=o->valid=o->buffer=0;
        o->pose_frame=o->geometry_frame=~0u;o->settled=0;
    }
    memset(h->clocks,0,sizeof h->clocks);h->ticks=h->draws=0;
}
void grind_props_free(GrindProps **owner)
{if(owner && *owner){GrindProps *h=*owner;grind_props_reset(h);chars_free(&h->models);stage_free(&h->lights);free(h);*owner=NULL;}}
int grind_props_load(GrindProps **owner,Collision *collision)
{
    grind_props_free(owner);GrindProps *h=calloc(1,sizeof *h);if(!h)return -2;h->collision=collision;
    int error=chars_load(&h->models,"grind_lifts.jgc");
    if(!error && (h->models.h->n_chars!=2 || h->models.chars[0].nbones!=6 || h->models.chars[1].nbones!=6 || h->models.motions[0].nframes!=226))error=-3;
    if(!error){error=stage_load(&h->lights,"grind_lights.jgs");}
    if(error){grind_props_free(&h);return error;}
    for(unsigned i=0;i<2;i++){const GrindPlacement *p=grind_placements+i;source_trs(p->pos,p->ang,p->scale,h->lift[i].root);}
    grind_props_reset(h);*owner=h;return 0;
}
static void grind_collision_matrix(GrindLift *o,const CharPose *bind,const int *parents,unsigned count,float matrices[6][16])
{
    for(unsigned b=0;b<count;b++){
        CharPose p=bind[b];const CharPose *v=o->pose+b;
        if(v->keyed&1)memcpy(p.pos,v->pos,12);
        if(v->keyed&2)memcpy(p.ang,v->ang,12);
        if(v->keyed&4)memcpy(p.scl,v->scl,12);
        int32_t angles[3]={p.ang[0],p.ang[1],p.ang[2]};
        memcpy(matrices[b],parents[b]<0 ? o->root:matrices[parents[b]],64);
        source_trs_apply(p.pos,angles,p.scl,matrices[b],player_fsca_sin);
    }
}
static int grind_geometry(GrindProps *h,GrindLift *o)
{
    /* A stopped lift owns immutable contact geometry. Publish zero motion
       once after stopping; further ticks reuse the collision broadphase. */
    if(o->valid && o->handle && o->geometry_frame==o->state.frame){
        if(!o->settled){
            if(!col_surface_publish(h->collision,o->handle,o->floors[o->buffer],o->floors[o->buffer],GRIND_TRIANGLES))return -4;
            o->settled=1;
        }
        return 0;
    }
    float matrices[6][16];grind_collision_matrix(o,grind_floor_bind,grind_floor_parents,6,matrices);
    unsigned next=o->buffer^1u;
    for(unsigned i=0;i<GRIND_TRIANGLES;i++){
        const GrindTriangle *s=grind_triangles+i;ColTri *d=o->floors[next]+i;
        d->surf=s->surface;d->mask=s->mask;d->dbl=s->flags;d->src=0;
        for(unsigned v=0;v<3;v++)source_point(matrices[s->bone],s->vertices+v*3,d->v+v*3);
    }
    if(!o->valid)memcpy(o->floors[o->buffer],o->floors[next],sizeof o->floors[0]);
    if(o->handle){if(!col_surface_publish(h->collision,o->handle,o->floors[next],o->floors[o->buffer],GRIND_TRIANGLES))return -4;}
    else {o->handle=col_surface_add(h->collision,o->floors[next],o->floors[o->buffer],GRIND_TRIANGLES);if(!o->handle)return -4;}
    o->valid=1;o->buffer=next;o->geometry_frame=o->state.frame;o->settled=0;
    grind_collision_matrix(o,grind_body_bind,grind_body_parents,5,matrices);
    for(int k=0;k<3;k++){o->low[k]=INFINITY;o->high[k]=-INFINITY;}
    for(unsigned v=0;v<8;v++){
        source_point(matrices[0],grind_box+v*3,o->box+v*3);
        for(int k=0;k<3;k++){float a=o->box[v*3+k];if(a<o->low[k])o->low[k]=a;if(a>o->high[k])o->high[k]=a;}
    }
    return 0;
}
int grind_props_tick(GrindProps *h,const Player *p)
{
    if(!h)return 0;h->ticks++;
    for(unsigned i=0;i<2;i++){
        GrindLift *o=h->lift+i;const float *pos=grind_placements[i].pos;
        float dx=p->x-pos[0],dy=p->y-pos[1],dz=p->z-pos[2];
        o->distance2=(dx*dx+dy*dy)+dz*dz;
        if(p->reset_query)o->state.contact=0;
        int sound=grind_lift_step(&o->state,p->y,dx*dx+dz*dz);
        if((o->state.phase==3 || sound==46) && o->sound[0]){audio_world_stop(o->sound[0]);o->sound[0]=0;}
        for(unsigned k=0;k<4;k++)if(o->sound[k] && audio_world_finished(o->sound[k])){audio_world_stop(o->sound[k]);o->sound[k]=0;}
        if(sound>=45 && !o->sound[sound-45] && !p->sound_blocked)o->sound[sound-45]=audio_world_sound(sound,pos);
        if(o->pose_frame!=o->state.frame){
            for(unsigned b=0;b<6;b++)chars_sample_pose(&h->models,h->models.chars,0,o->state.frame,b,o->pose+b);
            o->pose_frame=o->state.frame;
        }
        if(o->distance2<250000.f){int error=grind_geometry(h,o);if(error)return error;}
        else if(o->handle){col_surface_remove(h->collision,o->handle);o->handle=o->valid=0;}
    }
    return 0;
}
void grind_props_contacts(GrindProps *h,const Player *p)
{
    if(!h || p->reset_query || !p->pose_valid)return;
    float pos[3]={p->x,p->y,p->z},reach=player_body_reach(p);unsigned nearby=0;
    for(unsigned i=0;i<2;i++)if(h->lift[i].handle){
        GrindLift *o=h->lift+i;int outside=0;
        for(int k=0;k<3;k++)if(pos[k]+reach<o->low[k] || pos[k]-reach>o->high[k])outside=1;
        if(!outside)nearby|=1u<<i;
    }
    if(!nearby)return;float matrices[20][16];if(player_body_matrices(p,matrices)<0)return;
    for(unsigned b=0;b<20;b++){
        const float *source=PLAYER_BODY[p->pose_model][b].sphere;if(source[3]<=0)continue;
        float sphere[4];source_point(matrices[b],source,sphere);sphere[3]=source[3];
        for(unsigned i=0;i<2;i++)if((nearby&(1u<<i)) && source_body_box_sphere(h->lift[i].box,sphere))h->lift[i].state.contact=1;
    }
}
void grind_props_draw(GrindProps *h,const float eye[3],const float planes[6][4],const float clip[16],
                      const ObjectView *medium,int pass,int advance)
{
    if(!h)return;
    if(!pass){
        sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();
        for(unsigned i=0;i<GRIND_LIGHTS;i++){
            const GrindLight *l=grind_lights+i;unsigned clock=h->clocks[i],offset=0;
            /* 0ff5fc returns before 0c45d8 (including its UV clock) when
               outside 0c3498. Flags 8 bypasses this for types 0..9. */
            if(!(l->source_flags&8) && !object_circle_visible(medium,l->position[0],l->position[1],l->position[2]))continue;
            for(unsigned k=0;k<7;k++)if(clock>=l->times[k])offset+=30;
            if(clock>=l->times[7])offset=0;
            for(unsigned j=0;j<l->count;j++)stage_draw_obj_uv_clipped(&h->lights,l->first+j,clip,l->texture,0.f,offset/256.f);
            if(advance)h->clocks[i]=(clock+1)%l->period;
        }
    }
    for(unsigned i=0;i<2;i++){
        const float *p=grind_placements[i].pos;float dx=eye[0]-p[0],dy=eye[1]-p[1],dz=eye[2]-p[2];
        /* 0f8724's medium-circle gate precedes its 500-unit LOD test. */
        if(!object_circle_visible(medium,p[0],p[1],p[2]))continue;
        unsigned lod=((dx*dx+dy*dy)+dz*dz)>=250000.f;
        h->models.drawn_tris=0;chars_draw_material_pose_clipped(&h->models,lod,h->lift[i].root,h->lift[i].pose,pass,planes);
        if(h->models.drawn_tris)h->draws++;
    }
}
void grind_props_info(const GrindProps *h,unsigned out[6])
{memset(out,0,6*sizeof *out);if(!h)return;out[0]=h->ticks;out[1]=h->draws;for(unsigned i=0;i<2;i++){out[2+i*2]=h->lift[i].state.phase;out[3+i*2]=h->lift[i].state.frame;}}
