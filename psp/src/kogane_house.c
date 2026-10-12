#include "kogane_house.h"
#include "house_state.h"
#include "player_body.h"
#include "source_body.h"
#include "charpose.h"
#include "audio.h"
#include <stdlib.h>
#include <string.h>
typedef struct {unsigned type,frames,first_box,boxes,sound;} HouseType;
typedef struct {float vertices[24],center[3];} HouseBox;
typedef struct {unsigned address,type,sector;float position[3];int32_t angles[3];} HousePlacement;
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_kogane_house_data.inc"
#else
#include "generated/source_data/kogane_house_data.inc"
#endif
#define HOUSE_TYPES (sizeof house_types/sizeof *house_types)
#define HOUSE_COUNT (sizeof house_placements/sizeof *house_placements)
typedef struct {
    HouseState state;float root[16],box[2][24],center[2][3],low[3],high[3];
    unsigned sound;int active;
    unsigned render_revision,render_phase;int render_model,render_sector;
    float render_frame,render_mirror;
} HouseObject;
struct KoganeHouse {
    Chars models;HouseObject objects[HOUSE_COUNT];float previous[20][3];
    unsigned openings,breaks,contacts,ticks;int previous_valid,sector;
};
void kogane_house_reset(KoganeHouse *h)
{
    if(!h)return;
    for(unsigned i=0;i<HOUSE_COUNT;i++){
        HouseObject *o=h->objects+i;if(o->sound)audio_world_stop(o->sound);
        o->sound=0;o->active=0;o->state=(HouseState){.mirror=1};
    }
    h->previous_valid=0;h->openings=h->breaks=h->contacts=h->ticks=0;
}
void kogane_house_free(KoganeHouse **owner)
{
    if(!owner || !*owner)return;
    kogane_house_reset(*owner);chars_free(&(*owner)->models);free(*owner);*owner=NULL;
}
int kogane_house_load(KoganeHouse **owner)
{
    kogane_house_free(owner);KoganeHouse *h=calloc(1,sizeof *h);if(!h)return -2;
    int error=chars_load(&h->models,"kogane_house.jgc");
    if(!error && (h->models.h->n_chars!=HOUSE_TYPES*2 || h->models.h->n_motions!=HOUSE_TYPES*2))error=-3;
    if(!error)for(unsigned i=0;i<HOUSE_TYPES*2;i++)if(h->models.chars[i].nbones>28 || h->models.chars[i].motion0>=HOUSE_TYPES*2)error=-3;
    if(error){chars_free(&h->models);free(h);return error;}
    for(unsigned i=0;i<HOUSE_COUNT;i++){
        const HousePlacement *p=house_placements+i;const HouseType *t=house_types+p->type;
        HouseObject *o=h->objects+i;float one[3]={1,1,1};
        if(h->models.motions[h->models.chars[p->type*2].motion0].nframes!=t->frames){chars_free(&h->models);free(h);return -3;}
        for(int k=0;k<16;k++)o->root[k]=(k%5)==0;
        source_trs_apply(p->position,p->angles,one,o->root,player_fsca_sin);
        for(int k=0;k<3;k++){o->low[k]=INFINITY;o->high[k]=-INFINITY;}
        for(unsigned b=0;b<t->boxes;b++){
            const HouseBox *box=house_boxes+t->first_box+b;
            source_point(o->root,box->center,o->center[b]);
            for(int v=0;v<8;v++){
                source_point(o->root,box->vertices+v*3,o->box[b]+v*3);
                for(int k=0;k<3;k++){float x=o->box[b][v*3+k];if(x<o->low[k])o->low[k]=x;if(x>o->high[k])o->high[k]=x;}
            }
        }
    }
    *owner=h;kogane_house_reset(h);return 0;
}
void kogane_house_tick(KoganeHouse *h,const Player *p,const Zones *zones,int sector)
{
    if(!h)return;
    h->ticks++;if(sector>=0 && sector<114)h->sector=sector;
    for(unsigned i=0;i<HOUSE_COUNT;i++){
        const HousePlacement *place=house_placements+i;const HouseType *type=house_types+place->type;
        HouseObject *o=h->objects+i;
        /* 038d24/03901c/039164: original current + neighbouring sectors.
           An omitted transition volume retains the caller's sticky sector. */
        int active=zones && zone_neighbour(zones,sector,(int)place->sector);
        if(!active){
            if(o->active){if(o->sound)audio_world_stop(o->sound);o->sound=0;o->state=(HouseState){.mirror=1};}
            o->active=0;continue;
        }
        o->active=1;
        if(o->sound && audio_world_finished(o->sound)){audio_world_stop(o->sound);o->sound=0;}
        float dx=p->x-place->position[0],dy=p->y-place->position[1],dz=p->z-place->position[2];
        /* R_y(placement yaw) * (1,0,0), as 05a544/04d2d0. */
        float side=o->state.direction[2]*o->root[0]-o->state.direction[0]*o->root[8];
        if(house_state_tick(&o->state,type->type,type->frames,side,(dx*dx+dy*dy)+dz*dz)){
            if(house_is_door(type->type) || type->type==58)h->openings++;else if(type->type==5 || type->type==18)h->breaks++;
            if(type->sound && !o->sound && !p->sound_blocked)o->sound=audio_world_sound((int)type->sound,place->position);
        }
    }
}
void kogane_house_contacts(KoganeHouse *h,const Player *p)
{
    if(!h)return;
    if(p->reset_query || !p->pose_valid){h->previous_valid=0;return;}
    int nearby=0;float reach=player_body_reach(p),pos[3]={p->x,p->y,p->z};
    for(unsigned i=0;i<HOUSE_COUNT;i++){
        HouseObject *o=h->objects+i;if(!o->active || o->state.phase)continue;
        int outside=0;for(int k=0;k<3;k++)if(pos[k]+reach<o->low[k] || pos[k]-reach>o->high[k])outside=1;
        if(!outside)nearby=1;
    }
    if(!nearby){h->previous_valid=0;return;}
    float m[20][16];if(player_body_matrices(p,m)<0){h->previous_valid=0;return;}
    float spheres[20][4];
    for(int b=0;b<20;b++){
        const float *source=PLAYER_BODY[p->pose_model][b].sphere;
        source_point(m[b],source,spheres[b]);spheres[b][3]=source[3];
        if(!h->previous_valid)for(int k=0;k<3;k++)h->previous[b][k]=m[b][k*4+3];
    }
    for(unsigned i=0;i<HOUSE_COUNT;i++){
        HouseObject *o=h->objects+i;const HouseType *type=house_types+house_placements[i].type;
        if(!o->active || o->state.phase)continue;
        float best=INFINITY;
        for(unsigned box=0;box<type->boxes;box++)for(int b=0;b<20;b++){
            if(spheres[b][3]<=0 || !source_body_box_sphere(o->box[box],spheres[b]))continue;
            float origin[3]={m[b][3],m[b][7],m[b][11]},delta[3];
            for(int k=0;k<3;k++)delta[k]=origin[k]-o->center[box][k];
            float distance=sqrtf(probe_fipr(delta,delta));
            if(distance<best){
                best=distance;o->state.contact=1;h->contacts++;
                for(int k=0;k<3;k++)o->state.direction[k]=origin[k]-h->previous[b][k];
            }
        }
    }
    for(int b=0;b<20;b++)for(int k=0;k<3;k++)h->previous[b][k]=m[b][k*4+3];
    h->previous_valid=1;
}
static unsigned house_draw_order(const KoganeHouse *h,const float eye[3],const float forward[3],
                                 const ObjectView *small,const ObjectView *medium,int pass,unsigned order[HOUSE_COUNT])
{
    unsigned count=0;float depths[HOUSE_COUNT];
    for(unsigned i=0;i<HOUSE_COUNT;i++)if(h->objects[i].active){
        const float *p=house_placements[i].position;
        /* 058428/05c970 use the small circle; 04d488/05a730/0eaa2c
           use the current sector's medium circle. All use 0c32cc. */
        unsigned type=house_types[house_placements[i].type].type;
        const ObjectView *view=type==4 || type==58 ? small:medium;
        if(!object_pickup_visible(view,p[0],p[1],p[2]))continue;
        float depth=(p[0]-eye[0])*forward[0]+(p[1]-eye[1])*forward[1]+(p[2]-eye[2])*forward[2];
        unsigned at=count++;
        /* PVR translucent strips follow opaque geometry. PSP needs an
           explicit far-to-near order for the separate door assemblies. */
        if(pass==1)while(at && depths[at-1]<depth){order[at]=order[at-1];depths[at]=depths[at-1];at--;}
        order[at]=i;depths[at]=depth;
    }
    return count;
}
void kogane_house_draw(KoganeHouse *h,const float eye[3],const float forward[3],const float planes[6][4],
                       const ObjectView *small,const ObjectView *medium,int pass)
{
    if(!h)return;
    unsigned order[HOUSE_COUNT],count=house_draw_order(h,eye,forward,small,medium,pass,order);
    if(!count)return;
    /* 039bxx -> 0c39e0: original per-sector normal light. Imported
       source normals must be shaded here, not left at full white. */
    const float *v=house_lights[h->sector];
    CharLight light={{v[3],v[4],v[5]},v[6],v[7],{0xff000000u |
        ((unsigned)(v[0]*255)<<16) | ((unsigned)(v[1]*255)<<8) | (unsigned)(v[2]*255),0},0};
    chars_scene_lighting(&light,NULL);
    chars_world_frustum(planes);
    for(unsigned draw=0;draw<count;draw++){
        unsigned i=order[draw];
        const HousePlacement *p=house_placements+i;HouseObject *o=h->objects+i;
        float dx=eye[0]-p->position[0],dy=eye[1]-p->position[1],dz=eye[2]-p->position[2];
        int ci=p->type*2+((dx*dx+dy*dy)+dz*dz>=house_lod_distance2);
        float root[16];memcpy(root,o->root,sizeof root);
        for(int k=0;k<3;k++)root[k*4+2]*=o->state.mirror;
        /* Placement roots are immutable. Compare every remaining input to
           the normal-lit world vertices before reusing them, including
           bind-vs-motion frame zero, reflections, LOD and sector light. */
        if(!o->render_revision || o->render_model!=ci || o->render_sector!=h->sector ||
           o->render_phase!=o->state.phase || o->render_frame!=o->state.frame ||
           o->render_mirror!=o->state.mirror) {
            o->render_revision++;o->render_model=ci;o->render_sector=h->sector;
            o->render_phase=o->state.phase;o->render_frame=o->state.frame;o->render_mirror=o->state.mirror;
        }
        /* Moving leaves/fragments change every tick; keep those uncached
           instead of churning the fixed pool during an opening/break. */
        chars_material_cache_scope(o->state.phase==0 || o->state.phase==2 ? o:NULL,o->render_revision);
        chars_material_depth_sort(house_is_door(house_types[p->type].type) ? forward:NULL);
        /* Closed objects use the bind hierarchy; animated fragments retain
           their original per-node scale keys, including the final zeros. */
        if(o->state.phase)chars_draw_material_pass_clipped(&h->models,ci,root,0,o->state.frame,pass,planes);
        else chars_draw_material_bind_clipped(&h->models,ci,root,pass,planes);
    }
    chars_material_cache_scope(NULL,0);
    chars_material_depth_sort(NULL);chars_world_frustum(NULL);chars_scene_lighting(NULL,NULL);
}
void kogane_house_info(const KoganeHouse *h,unsigned out[4])
{out[0]=h?h->openings:0;out[1]=h?h->breaks:0;out[2]=h?h->contacts:0;out[3]=h?h->ticks:0;}
