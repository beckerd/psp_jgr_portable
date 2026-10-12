#include "city_openings.h"
#include "opening_state.h"
#include "player_body.h"
#include "source_body.h"
#include "charpose.h"
#include "audio.h"
#include "scene.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef struct {unsigned type,kind,frames,first_box,boxes,sound,bank;} OpeningType;
typedef struct {float vertices[24],center[3];} OpeningBox;
typedef struct {unsigned address,type,sector;float position[3];int32_t angles[3];float scale[3];} OpeningPlacement;
typedef struct {unsigned object;JgsObj original;} OpeningBaked;
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_shibuya_door_data.inc"
#else
#include "generated/source_data/shibuya_door_data.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_benten_opening_data.inc"
#else
#include "generated/source_data/benten_opening_data.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_bantam_opening_data.inc"
#else
#include "generated/source_data/bantam_opening_data.inc"
#endif
typedef struct {
    OpeningState state;float root[16],box[24],center[3],low[3],high[3];
    ColTri floors[4];int handle,active;unsigned sound;float player_distance2;
} OpeningObject;
struct CityOpenings {
    Chars models;Collision *collision;const OpeningType *types;const OpeningPlacement *placements;
    const OpeningBox *boxes;const float (*lights)[8];unsigned count,sectors,sector,stage;
    Stage *static_stage;Scene njs;unsigned char *motion_image;
    OpeningObject objects[12];float previous[5][20][3];unsigned previous_valid[5];
    unsigned openings,breaks,contacts,ticks;
};
static void opening_retire(CityOpenings *h,OpeningObject *o)
{
    if(o->sound)audio_world_stop(o->sound);
    if(o->handle)col_surface_remove(h->collision,o->handle);
    o->handle=0;o->sound=0;o->active=0;o->state=(OpeningState){.mirror=1};
    if(h->stage==1)o->state.phase=1;
}
void city_openings_reset(CityOpenings *h)
{
    if(!h)return;
    for(unsigned i=0;i<h->count;i++)opening_retire(h,h->objects+i);
    memset(h->previous_valid,0,sizeof h->previous_valid);
    h->openings=h->breaks=h->contacts=h->ticks=0;
}
void city_openings_free(CityOpenings **owner)
{
    if(!owner || !*owner)return;
    CityOpenings *h=*owner;city_openings_reset(h);
    if(h->static_stage)for(unsigned i=0;i<8;i++)h->static_stage->obj[shibuya_door_baked[i].object]=shibuya_door_baked[i].original;
    chars_free(&h->models);free(h->motion_image);free(h);*owner=NULL;
}
int city_openings_load(CityOpenings **owner,int stage,Collision *collision,Stage *static_stage)
{
    city_openings_free(owner);if(stage!=1 && stage!=3 && stage!=5)return -3;
    if(stage==1){
        if(!static_stage || !static_stage->h)return -3;
        for(unsigned i=0;i<8;i++){
            const OpeningBaked *b=shibuya_door_baked+i;
            if(b->object>=static_stage->h->n_obj || memcmp(static_stage->obj+b->object,&b->original,sizeof b->original))return -3;
        }
    }
    CityOpenings *h=calloc(1,sizeof *h);if(!h)return -2;
    h->stage=stage;h->collision=collision;
    h->types=stage==1 ? shibuya_door_types:stage==3 ? benten_types:bantam_types;h->placements=stage==1 ? shibuya_door_placements:stage==3 ? benten_placements:bantam_placements;
    h->boxes=stage==1 ? shibuya_door_boxes:stage==3 ? benten_boxes:bantam_boxes;h->lights=stage==1 ? shibuya_door_lights:stage==3 ? benten_lights:bantam_lights;
    h->count=stage==1 ? 8:stage==3 ? sizeof benten_placements/sizeof benten_placements[0]:12;h->sectors=stage==1 ? 63:stage==3 ? 46:71;
    const char *name=stage==1 ? "shibuya_doors.jgc":stage==3 ? "benten_openings.jgc":"bantam_openings.jgc";
    int error=chars_load(&h->models,name);
    unsigned models=stage==1 ? 4u:stage==3 ? sizeof benten_types/sizeof benten_types[0]:2u;
    if(!error && (h->models.h->n_chars!=models || h->models.h->n_motions!=(stage==1 ? 0u:models)))error=-3;
    if(!error)for(unsigned i=0;i<models;i++){
        const JgcChar *c=h->models.chars+i;
        if(c->nbones>28 || (stage==1 ? c->nbones!=1:(c->motion0>=models || h->models.motions[c->motion0].nframes!=h->types[i].frames)))error=-3;
    }
    if(!error && stage==1){
        FILE *f=fopen("shibuya_doors.bin","rb");
        unsigned head[3];
        if(!f)error=-1;
        else {
            if(fread(head,4,3,f)!=3 || memcmp(head,"SDM1",4) || !head[2] || head[2]>65536)error=-3;
            else {
                h->motion_image=malloc(head[2]);
                if(!h->motion_image)error=-2;
                else if(fread(h->motion_image,1,head[2],f)!=head[2])error=-3;
                else {
                    h->njs.image=h->motion_image;h->njs.base=head[1];h->njs.length=head[2];
                    for(unsigned ci=0;ci<4;ci++)for(unsigned variant=0;variant<2;variant++)for(unsigned frame=0;frame<20;frame++){
                        CharPose pose;chars_sample_pose(&h->models,h->models.chars+ci,-1,0,0,&pose);
                        if(!scene_sample_njs(&h->njs,shibuya_door_motions[ci][variant],frame,0,0,&pose))error=-3;
                    }
                }
            }
            fclose(f);
        }
    }
    if(error){city_openings_free(&h);return error;}
    for(unsigned i=0;i<h->count;i++){
        const OpeningPlacement *p=h->placements+i;const OpeningType *t=h->types+p->type;
        OpeningObject *o=h->objects+i;float scale[3];
        for(int k=0;k<3;k++)scale[k]=fabsf(p->scale[k]);
        for(int k=0;k<16;k++)o->root[k]=(k%5)==0;
        source_trs_apply(p->position,p->angles,scale,o->root,player_fsca_sin);
        const OpeningBox *box=h->boxes+t->first_box;source_point(o->root,box->center,o->center);
        for(int k=0;k<3;k++){o->low[k]=INFINITY;o->high[k]=-INFINITY;}
        for(int v=0;v<8;v++){
            source_point(o->root,box->vertices+v*3,o->box+v*3);
            for(int k=0;k<3;k++){float x=o->box[v*3+k];if(x<o->low[k])o->low[k]=x;if(x>o->high[k])o->high[k]=x;}
        }
        if(t->kind==OPENING_DOOR){
            memcpy(o->floors,bantam_door_floors,sizeof o->floors);
            for(int j=0;j<4;j++)for(int v=0;v<3;v++)source_point(o->root,bantam_door_floors[j].v+v*3,o->floors[j].v+v*3);
            o->handle=col_surface_add(collision,o->floors,NULL,4);
            if(!o->handle){city_openings_free(&h);return -2;}
        }
    }
    city_openings_reset(h);
    if(stage==1){for(unsigned i=0;i<8;i++)static_stage->obj[shibuya_door_baked[i].object].n_batch=0;h->static_stage=static_stage;}
    *owner=h;return 0;
}
int city_openings_tick(CityOpenings *h,const Player *p,const Zones *zones,int sector)
{
    if(!h)return 0;
    h->ticks++;if((unsigned)sector<h->sectors)h->sector=sector;
    int yaw=(int)(p->yaw*(65536.f/(2.f*3.14159265358979323846f)));
    /* 0c2800 publishes +d0 travel, which is zero at rest. */
    float direction[3]={p->speed*player_fsca_sin(yaw),0,p->speed*player_fsca_sin(yaw+16384)};
    for(unsigned i=0;i<h->count;i++){
        const OpeningPlacement *place=h->placements+i;const OpeningType *type=h->types+place->type;
        OpeningObject *o=h->objects+i;
        /* 048e6c creates breakable barriers once for the entire stage;
           unlike passage doors they must not respawn on sector changes. */
        if(type->kind!=OPENING_BARRIER && (!zones || !zone_neighbour(zones,sector,place->sector))){if(o->active)opening_retire(h,o);continue;}
        o->active=1;if(o->state.phase==5)continue;
        if(o->sound && audio_world_finished(o->sound)){audio_world_stop(o->sound);o->sound=0;}
        float dx=p->x-place->position[0],dy=p->y-place->position[1],dz=p->z-place->position[2],distance2=(dx*dx+dy*dy)+dz*dz;
        o->player_distance2=distance2;
        if(type->kind==OPENING_DOOR){
            if(distance2<40000.f){if(!o->handle){o->handle=col_surface_add(h->collision,o->floors,NULL,4);if(!o->handle)return -2;}}
            else if(o->handle){col_surface_remove(h->collision,o->handle);o->handle=0;}
        }
        if(p->reset_query)o->state.contact=o->state.floor=0;
        const float *dir=type->kind==OPENING_GATE ? direction:o->state.direction;
        float side=type->kind==OPENING_HINGED ? dir[2]*o->root[0]-dir[0]*o->root[8]:dir[0]*o->root[2]+dir[2]*o->root[10];
        if(opening_state_tick(&o->state,type->kind,type->frames,side,distance2)){
            if(type->kind==OPENING_WINDOW || type->kind==OPENING_BARRIER)h->breaks++;else h->openings++;
            if(!o->sound && !p->sound_blocked)o->sound=type->bank==4 ? audio_world_common(type->sound,place->position):audio_world_sound(type->sound,place->position);
        }
    }
    return 0;
}
void city_openings_surface(void *owner,int triangle)
{
    CityOpenings *h=owner;if(!h || triangle<0)return;
    for(unsigned i=0;i<h->count;i++){
        OpeningObject *o=h->objects+i;
        if(o->active && o->state.phase==1 && o->handle && (unsigned)triangle>=(unsigned)o->handle && (unsigned)triangle<(unsigned)o->handle+4){
            unsigned floor=((unsigned)triangle-(unsigned)o->handle)/2+1;
            if(!o->state.floor)h->contacts++;
            if(!o->state.floor || floor==1)o->state.floor=floor;
        }
    }
}
void city_openings_contacts(CityOpenings *h,const Player *p,unsigned actor)
{
    if(!h || actor>=5)return;
    if(p->reset_query || !p->pose_valid){h->previous_valid[actor]=0;return;}
    float pos[3]={p->x,p->y,p->z},reach=player_body_reach(p);unsigned nearby=0;
    for(unsigned i=0;i<h->count;i++){
        OpeningObject *o=h->objects+i;const OpeningType *t=h->types+h->placements[i].type;
        if(!o->active || o->state.phase!=1 || t->kind==OPENING_AUTO || (actor && h->stage!=5))continue;
        int outside=0;for(int k=0;k<3;k++)if(pos[k]+reach<o->low[k] || pos[k]-reach>o->high[k])outside=1;
        if(!outside)nearby|=1u<<i;
    }
    if(!nearby){h->previous_valid[actor]=0;return;}
    float m[20][16];if(player_body_matrices(p,m)<0){h->previous_valid[actor]=0;return;}
    float sphere[20][4];
    for(int b=0;b<20;b++){
        const float *source=PLAYER_BODY[p->pose_model][b].sphere;
        source_point(m[b],source,sphere[b]);sphere[b][3]=source[3];
        if(!h->previous_valid[actor])for(int k=0;k<3;k++)h->previous[actor][b][k]=m[b][k*4+3];
    }
    for(unsigned i=0;i<h->count;i++)if(nearby&(1u<<i)){
        OpeningObject *o=h->objects+i;const OpeningType *t=h->types+h->placements[i].type;float best=INFINITY;
        for(int b=0;b<20;b++){
            if(sphere[b][3]<=0 || !source_body_box_sphere(o->box,sphere[b]))continue;
            float origin[3]={m[b][3],m[b][7],m[b][11]},delta[3];
            for(int k=0;k<3;k++)delta[k]=origin[k]-o->center[k];
            float distance=probe_fipr(delta,delta);
            if(distance<best){best=distance;if(!o->state.contact)h->contacts++;o->state.contact=1;
                for(int k=0;k<3;k++)o->state.direction[k]=origin[k]-h->previous[actor][b][k];
                if(t->kind==OPENING_WINDOW){int yaw=(int)(p->yaw*(65536.f/(2.f*3.14159265358979323846f)));
                    o->state.direction[0]=p->speed*player_fsca_sin(yaw);o->state.direction[2]=p->speed*player_fsca_sin(yaw+16384);}
            }
        }
    }
    for(int b=0;b<20;b++)for(int k=0;k<3;k++)h->previous[actor][b][k]=m[b][k*4+3];
    h->previous_valid[actor]=1;
}
static int opening_window_visible(unsigned sector)
{
    /* 1005f0 suppresses the upper windows from the indoor sectors. */
    switch(sector){case 15:case 20:case 23:case 27:case 29:case 31:case 38:return 0;default:return 1;}
}
void city_openings_draw(CityOpenings *h,const float eye[3],const float forward[3],const float planes[6][4],int pass)
{
    if(!h)return;
    const float *v=h->lights[h->sector];
    CharLight light={{v[3],v[4],v[5]},v[6],v[7],{0xff000000u|((unsigned)(v[0]*255)<<16)|((unsigned)(v[1]*255)<<8)|(unsigned)(v[2]*255),0},0};
    chars_scene_lighting(&light,NULL);chars_world_frustum(planes);
    unsigned order[12],count=0;float depth[12];
    for(unsigned i=0;i<h->count;i++)if(h->objects[i].active && h->objects[i].state.phase!=5){
        if(h->types[h->placements[i].type].kind==OPENING_WINDOW && !opening_window_visible(h->sector))continue;
        const float *p=h->placements[i].position;float d=0;
        for(int k=0;k<3;k++){float delta=p[k]-eye[k];d+=delta*forward[k];}
        /* 0ee470 uses 0c284c (player), not 0c2b7c (camera). */
        if(h->types[h->placements[i].type].kind==OPENING_BARRIER && h->objects[i].player_distance2>=80000.f)continue;
        unsigned at=count++;if(pass==1)while(at && depth[at-1]<d){order[at]=order[at-1];depth[at]=depth[at-1];at--;}
        order[at]=i;depth[at]=d;
    }
    for(unsigned j=0;j<count;j++){
        unsigned i=order[j],ci=h->placements[i].type;OpeningObject *o=h->objects+i;float root[16];memcpy(root,o->root,sizeof root);
        for(int k=0;k<3;k++)root[k*4+2]*=o->state.mirror;
        if(h->stage==1 || ((h->types[ci].kind==OPENING_WINDOW || h->types[ci].kind==OPENING_REACTIVE) && o->state.phase<2)){
            const JgcChar *ch=h->models.chars+ci;CharPose pose[28];
            for(unsigned b=0;b<ch->nbones;b++){
                chars_sample_pose(&h->models,ch,-1,0,b,pose+b);
                if(h->stage==1 && o->state.phase>1)scene_sample_njs(&h->njs,shibuya_door_motions[ci][o->state.variant],o->state.frame,b,0,pose+b);
            }
            chars_draw_material_pose_clipped(&h->models,ci,root,pose,pass,planes);
        }else chars_draw_material_pass_clipped(&h->models,ci,root,0,o->state.frame,pass,planes);
    }
    chars_world_frustum(NULL);chars_scene_lighting(NULL,NULL);
}
void city_openings_info(const CityOpenings *h,unsigned out[4])
{out[0]=h?h->openings:0;out[1]=h?h->breaks:0;out[2]=h?h->contacts:0;out[3]=h?h->ticks:0;}
