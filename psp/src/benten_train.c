#include "benten_train.h"
#include "mission_quad.h"
#include "scene.h"
#include "source_trs.h"
#include "player_body.h"
#include "traffic_contact.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {int parent;float pos[3];int32_t angles[3];float scale[3],vertices[24],center[3];} TrainBody;
typedef struct {unsigned bone;float position[3];} TrainVertex;
typedef struct {unsigned indices[3];unsigned short surface,mask,flags;} TrainTriangle;
#if JGR_PORTABLE_ASSETS
#include "source_data_benten_train_data.inc"
#else
#include "generated/source_data/benten_train_data.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_bantam_train_data.inc"
#else
#include "generated/source_data/bantam_train_data.inc"
#endif
typedef struct {
    unsigned active,phase,horn,sound,handle,valid,near;
    float frame,rate;
    float position[3],body[4][24],center[4][3],previous[4][3];
    ColTri *geometry[2];unsigned buffer,pose_valid;CharPose pose[4];
} TrainInstance;
struct BentenTrain {
    Scene source;unsigned char *image;uint32_t motions[4],wrap_motion;Chars models;
    unsigned stage,route_count,random_enabled;int error;
    Collision *collision;TrainInstance trains[4];MissionQuad boundaries[5];
    float previous[3],limbs[20][3];unsigned previous_valid,limbs_valid,sector;
    unsigned ticks,spawns,drawn,contacts,surfaces;
};
static const float train_identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static float train_distance(const float a[3],const float b[3])
{float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];return (x*x+y*y)+z*z;}
static unsigned train_triangles(unsigned route){return route<2 ? 4:108;}
static unsigned train_count(const BentenTrain *h,unsigned route)
{return h->stage==5 ? 108:train_triangles(route);}
static void train_retire(BentenTrain *h,unsigned route)
{
    TrainInstance *t=h->trains+route;
    if(t->sound)audio_world_stop(t->sound);
    if(t->handle)col_surface_remove(h->collision,t->handle);
    t->active=t->sound=t->handle=t->valid=t->near=t->pose_valid=0;
}
void benten_train_reset(BentenTrain *h)
{
    if(!h)return;
    for(unsigned i=0;i<4;i++)train_retire(h,i);
    h->random_enabled=0;h->error=0;h->previous_valid=h->limbs_valid=0;h->ticks=h->spawns=h->drawn=h->contacts=h->surfaces=0;
}
void benten_train_free(BentenTrain **owner)
{
    if(!owner || !*owner)return;
    BentenTrain *h=*owner;benten_train_reset(h);chars_free(&h->models);
    for(unsigned i=0;i<4;i++)for(unsigned j=0;j<2;j++)free(h->trains[i].geometry[j]);
    free(h->image);free(h);*owner=NULL;
}
static int train_pose(BentenTrain *h,unsigned route,float frame,CharPose pose[4],int collision)
{
    for(unsigned b=0;b<4;b++){
        chars_sample_pose(&h->models,h->models.chars,-1,0,b,pose+b);
        uint32_t motion=h->motions[route];
        /* Original car-3 rotation count excludes its adjacent final key.
           Linear search consumes it, but the wrap uses declared key 137. */
        if(h->stage==3 && route==2 && b==2 && (collision ? frame==0:frame>=889))motion=h->wrap_motion;
        if(!(collision ? scene_sample_collision(&h->source,motion,frame,b,pose+b):
             scene_sample_njs(&h->source,motion,frame,b,1,pose+b)))return -3;
    }
    return 0;
}
static int train_root(BentenTrain *h,unsigned route,float frame,float position[3])
{
    CharPose p={.scl={1,1,1}};
    /* 0fc300/358 take an integer key clock; material/collision sampling
       retains the fractional script rate independently. */
    if(h->stage==5)frame=(float)(int)frame;
    if(!scene_sample_njs(&h->source,h->motions[route],frame,0,0,&p))return -3;
    memcpy(position,p.pos,12);return 0;
}
static int train_load(BentenTrain **owner,Collision *collision,unsigned stage)
{
    benten_train_free(owner);BentenTrain *h=calloc(1,sizeof *h);if(!h)return -2;
    h->collision=collision;h->stage=stage;h->route_count=stage==5 ? 2:4;int error=-1;
    char path[100];const char *name=stage==5 ? "bantam_train":"benten_train";
    snprintf(path,sizeof path,"%s.bin",name);FILE *f=fopen(path,"rb");
    uint32_t head[8];
    if(f){
        error=-3;
        if(fread(head,4,8,f)==8 && !memcmp(head,"BTR1",4) && head[2]>0 && head[2]<=256u*1024u){
            h->image=malloc(head[2]);error=-2;
            if(h->image){error=-3;if(fread(h->image,1,head[2],f)==head[2]){
                h->source.image=h->image;h->source.base=head[1];h->source.length=head[2];
                memcpy(h->motions,head+3,16);h->wrap_motion=head[7];error=0;
            }}
        }fclose(f);
    }
    if(!error){
        snprintf(path,sizeof path,"%s.jgc",name);error=chars_load(&h->models,path);
    }
    if(!error && (h->models.h->n_chars!=1 || h->models.h->n_bones!=4 || h->models.h->n_motions))error=-3;
    for(unsigned i=0;!error && i<h->route_count;i++){
        unsigned frames=scene_word(&h->source,h->motions[i]+4);
        if(frames!=(stage==5 ? 1560u:i<2 ? 785u:890u)){error=-3;break;}
        /* Validate every key interval once, off the gameplay path. */
        for(unsigned frame=0;!error && frame<frames;frame++){
            CharPose poses[4];error=train_pose(h,i,frame,poses,0);
            if(!error)error=train_pose(h,i,frame,poses,1);
        }
        for(unsigned j=0;!error && j<2;j++)if(!(h->trains[i].geometry[j]=calloc(train_count(h,i),sizeof(ColTri))))error=-2;
    }
    if(error){benten_train_free(&h);return error;}
    memcpy(h->boundaries,train_boundaries,sizeof h->boundaries);
    for(unsigned i=0;i<5;i++)mission_quad_prepare(h->boundaries+i);
    *owner=h;return 0;
}
int benten_train_load(BentenTrain **owner,Collision *collision){return train_load(owner,collision,3);}
int bantam_train_load(BentenTrain **owner,Collision *collision){return train_load(owner,collision,5);}
static int train_spawn_route(BentenTrain *h,unsigned route,float frame,float rate,int blocked)
{
    if(route>=h->route_count || !isfinite(frame) || frame<0 || frame>=scene_word(&h->source,h->motions[route]+4) || !isfinite(rate) || rate<0)return -3;
    TrainInstance *t=h->trains+route;
    if(t->active)return 0;
    t->active=1;t->phase=0;t->frame=frame;t->rate=rate;t->horn=0;t->valid=t->near=t->pose_valid=0;
    int error=train_root(h,route,t->frame,t->position);if(error)return error;
    /* Bantam 0fbe60 selects SMSB2 (17:12); Benten 08df00 uses SOSB6. */
    if(!blocked)t->sound=audio_world_sound(h->stage==5 ? -12:12,t->position);
    h->spawns++;return 0;
}
static int train_spawn(BentenTrain *h,unsigned selector,int blocked)
{return train_spawn_route(h,train_selectors[selector][0],train_selectors[selector][1],1,blocked);}
int bantam_train_event(BentenTrain *h,int code,unsigned route,unsigned frame,unsigned percent)
{
    if(!h || h->stage!=5)return 0;
    if(code==0x27)h->random_enabled=1;
    else if(code==0x28)h->random_enabled=0;
    else if(code==0x29)h->error=train_spawn_route(h,route,(float)frame,(float)percent/100.f,0);
    return h->error;
}
static int train_subway_sector(int sector)
{return sector==15 || sector==32 || sector==33 || sector==43 || sector==44 || sector==45;}
static int train_geometry(BentenTrain *h,unsigned route)
{
    TrainInstance *t=h->trains+route;CharPose pose[4];float matrices[4][16];
    int error=train_pose(h,route,t->frame,pose,1);if(error)return error;
    for(unsigned b=0;b<4;b++){
        const TrainBody *body=(h->stage==5 ? bantam_train_bodies:train_bodies)+b;
        int32_t angles[3]={pose[b].ang[0],pose[b].ang[1],pose[b].ang[2]};
        memcpy(matrices[b],body->parent<0 ? train_identity:matrices[body->parent],sizeof matrices[b]);
        source_trs_apply(pose[b].pos,angles,pose[b].scl,matrices[b],player_fsca_sin);
        memcpy(t->previous[b],t->center[b],12);
        source_point(matrices[b],body->center,t->center[b]);
        if(!t->valid)memcpy(t->previous[b],t->center[b],12);
        for(unsigned v=0;v<8;v++)source_point(matrices[b],body->vertices+3*v,t->body[b]+3*v);
    }
    unsigned next=t->buffer^1u,n=train_count(h,route);const TrainTriangle *local=h->stage==5 ? bantam_train_surfaces0:route<2 ? train_surfaces0:train_surfaces1;
    const TrainVertex *vertices=h->stage==5 ? bantam_train_vertices0:route<2 ? train_vertices0:train_vertices1;
    unsigned count=h->stage==5 ? sizeof bantam_train_vertices0/sizeof *vertices:route<2 ? sizeof train_vertices0/sizeof *vertices:sizeof train_vertices1/sizeof *vertices;
    float transformed[104][3];if(count>104)return -3;
    /* Transform each source vertex once; assembling strips must not repeat
       the original matrix work for every adjacent triangle. */
    for(unsigned i=0;i<count;i++)source_point(matrices[vertices[i].bone],vertices[i].position,transformed[i]);
    for(unsigned i=0;i<n;i++){
        ColTri *out=t->geometry[next]+i;out->surf=local[i].surface;out->mask=local[i].mask;out->dbl=local[i].flags;out->src=0;
        for(unsigned v=0;v<3;v++)memcpy(out->v+3*v,transformed[local[i].indices[v]],12);
    }
    if(!t->valid)memcpy(t->geometry[t->buffer],t->geometry[next],n*sizeof(ColTri));
    if(t->handle){if(!col_surface_publish(h->collision,t->handle,t->geometry[next],t->geometry[t->buffer],n))return -4;}
    else {t->handle=col_surface_add(h->collision,t->geometry[next],t->geometry[t->buffer],n);if(!t->handle)return -4;}
    t->buffer=next;t->valid=1;h->surfaces+=n;return 0;
}
int benten_train_tick(BentenTrain *h,const Player *p,int sector)
{
    if(!h)return 0;
    if(h->error)return h->error;
    h->ticks++;if((unsigned)sector<(h->stage==5 ? 71u:46u))h->sector=sector;
    float position[3]={p->x,p->y,p->z};int error=0;
    if(h->stage==3 && h->previous_valid && !p->reset_query && !p->sound_blocked){
        float segment[6];memcpy(segment,h->previous,12);
        for(int k=0;k<3;k++)segment[3+k]=position[k]-h->previous[k];
        for(unsigned i=0;i<5;i++)if(mission_quad_near(h->boundaries+i,segment)){
            unsigned direction=mission_quad_cross(h->boundaries+i,segment);if(!direction)continue;
            int forward=!!(direction&0x10000),selector=-1;
            if(i==0)selector=forward?4:1;
            else if(i==1 && forward)selector=2;
            else if(i==2 && forward)selector=3;
            else if(i==3)selector=forward?0:5;
            else if(i==4)selector=forward?7:6;
            if(selector>=0 && (error=train_spawn(h,selector,0)))return error;
        }
    }
    memcpy(h->previous,position,12);h->previous_valid=1;
    if(p->reset_query)h->limbs_valid=0;
    if(h->stage==3 && !p->sound_blocked){
        if(train_subway_sector(sector)){
            if((error=train_spawn(h,6,0)) || (error=train_spawn(h,7,0)))return error;
        }else for(unsigned i=2;i<4;i++)train_retire(h,i);
    }
    /* 0fc4bc: optional mission-enabled random scheduler. Track entry uses
       event 29 instead and starts both authored routes immediately. */
    if(h->stage==5 && h->random_enabled && !p->sound_blocked && ((rand()&32767)/32768.f)*150.f<1.f){
        unsigned route=(unsigned)(((rand()&32767)/32768.f)*2.f);
        if((error=train_spawn_route(h,route,0,1,0)))return error;
    }
    for(unsigned i=0;i<h->route_count;i++){
        TrainInstance *t=h->trains+i;if(!t->active)continue;
        if(t->phase==0)t->phase=1;
        else if(t->phase==1){if((t->frame+=t->rate)>=scene_word(&h->source,h->motions[i]+4)-1)t->phase=2;}
        else {train_retire(h,i);continue;}
        t->pose_valid=0;
        if((error=train_root(h,i,t->frame,t->position)))return error;
        t->near=train_distance(t->position,position)<360000.f;
        if(t->near){if((error=train_geometry(h,i)))return error;}
        else {if(t->handle)col_surface_remove(h->collision,t->handle);t->handle=t->valid=0;}
        if(t->sound && !p->sound_blocked)audio_world_position(t->sound,t->position);
        if(t->frame+30<scene_word(&h->source,h->motions[i]+4)-1){
            float ahead[3];if((error=train_root(h,i,t->frame+30,ahead)))return error;
            if(train_distance(ahead,position)<90000.f){
                if(!t->horn && !p->sound_blocked)audio_world_sound(43,ahead);
                t->horn=1;
            }else t->horn=0;
        }
    }
    return 0;
}
int benten_train_contacts(BentenTrain *h,const Player *p,int *mode,int *yaw)
{
    if(!h)return 0;
    unsigned nearby=0;for(unsigned i=0;i<4;i++)nearby|=(h->trains[i].active && h->trains[i].near && h->trains[i].valid)<<i;
    if(!nearby || p->reset_query || p->damage_invincible){h->limbs_valid=0;return 0;}
    float m[20][16];if(player_body_matrices(p,m)<0){h->limbs_valid=0;return 0;}
    SourceActorContact nearest={0};float best=INFINITY;unsigned group=0,hits=0;
    for(unsigned b=0;b<20;b++){
        const PlayerBodyNode *node=&PLAYER_BODY[p->pose_model][b];float center[3]={m[b][3],m[b][7],m[b][11]},sphere[4];
        source_point(m[b],node->sphere,sphere);sphere[3]=node->sphere[3];
        if(!h->limbs_valid)memcpy(h->limbs[b],center,12);
        else for(int k=0;k<3;k++)h->limbs[b][k]+=p->portal_translation[k];
        if(sphere[3]>0)for(unsigned i=0;i<4;i++)if(nearby&(1u<<i))for(unsigned car=0;car<4;car++){
            TrainInstance *t=h->trains+i;
            if(!source_body_box_sphere(t->body[car],sphere))continue;
            SourceActorContact contact;float distance=source_actor_contact(&contact,center,h->limbs[b],node->attribute,t->center[car],t->previous[car],24);
            if(distance<best){best=distance;nearest=contact;}
            unsigned g=traffic_limb_group(node->attribute);group=!group?g:group==g?g:1;hits++;
        }
        memcpy(h->limbs[b],center,12);
    }
    h->limbs_valid=1;h->contacts+=hits;if(!hits)return 0;
    const float zero[3]={0},one[3]={1,1,1};int32_t a[3],axis[3]={0};float root[16];memcpy(root,train_identity,sizeof root);
    player_motion_angles(p,a);
    for(int i=0;i<3;i++){int k=i==0?0:i==1?2:1;axis[k]=a[k];source_trs_apply(zero,axis,one,root,player_fsca_sin);axis[k]=0;}
    float forward[3]={-10*root[2],-10*root[6],-10*root[10]};
    *mode=traffic_contact_direction(&nearest,group,forward,player_bam_atan2,yaw);return hits;
}
void benten_train_draw(BentenTrain *h,const float eye[3],const float planes[6][4],int pass)
{
    if(!h)return;
    const float *v=h->stage==5 ? bantam_train_lights[h->sector]:train_lights[h->sector];
    CharLight light={{v[3],v[4],v[5]},v[6],v[7],{0xff000000u|((unsigned)(v[0]*255)<<16)|((unsigned)(v[1]*255)<<8)|(unsigned)(v[2]*255),0},0};
    chars_scene_lighting(&light,NULL);chars_world_frustum(planes);
    for(unsigned i=0;i<4;i++)if(h->trains[i].active){
        /* Render each native material once in its PSP list. The original
           08e2fc distance limit belongs to its modifier-volume redraw,
           not to the visibility of the train's translucent materials. */
        TrainInstance *t=h->trains+i;
        if(!t->pose_valid){if(train_pose(h,i,t->frame,t->pose,0))continue;t->pose_valid=1;}
        h->models.drawn_tris=0;
        chars_draw_material_pose_clipped(&h->models,0,train_identity,t->pose,pass,planes);
        if(h->models.drawn_tris)h->drawn++;
    }
    chars_world_frustum(NULL);chars_scene_lighting(NULL,NULL);
}
void benten_train_info(const BentenTrain *h,unsigned out[6])
{
    memset(out,0,6*sizeof *out);if(!h)return;
    out[0]=h->ticks;out[1]=h->spawns;out[2]=h->drawn;out[3]=h->contacts;out[4]=h->surfaces;
    for(unsigned i=0;i<4;i++)out[5]+=h->trains[i].active;
}
