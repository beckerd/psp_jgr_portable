/* Setup-only world context for native recorded-input performances. All
 * authored values and geometry come from verified user-generated assets. */
#include "portable_native_world.h"
#include "portable_world_convert.h"
#include "portable_native_player.h"
#include "portable_native_objects.h"
#include "pathmission.h"
#include "canplay.h"
#include "zones.h"
#include "set_collision.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#undef fopen
#undef remove

int jgr_native_error(const JgrNativeConfig *c,const char *message)
{if(c && c->error && c->error_size)snprintf(c->error,c->error_size,"%s",message);return -1;}
static unsigned native_word(const void *p){unsigned value;memcpy(&value,p,4);return value;}
static int native_name(const char *name)
{return name && *name && !strchr(name,'/') && !strchr(name,'\\') && !strstr(name,"..");}
int jgr_native_asset_path(const JgrNativeConfig *c,const char *name,char *path,size_t size)
{
    if(!c || !c->asset_directory || !c->dependency || !native_name(name))return jgr_native_error(c,"Invalid native replay asset name");
    FILE *verified=c->dependency(c->dependency_user,name);if(!verified)return jgr_native_error(c,"Native replay dependency is unavailable");
    if(fclose(verified))return jgr_native_error(c,"Native replay dependency could not be closed");
    int n=snprintf(path,size,"%s/%s",c->asset_directory,name);
    return n<0 || (size_t)n>=size ? jgr_native_error(c,"Native replay asset path is too long"):0;
}
void *jgr_native_asset_read(const JgrNativeConfig *c,const char *name,size_t *size,size_t maximum)
{
    if(!c || !c->dependency || !size || !native_name(name))return NULL;
    FILE *f=c->dependency(c->dependency_user,name);if(!f)return NULL;
    unsigned char *data=NULL;long bytes;
    if(fseek(f,0,SEEK_END) || (bytes=ftell(f))<0 || (size_t)bytes>maximum || fseek(f,0,SEEK_SET))goto done;
    data=malloc(bytes?bytes:1);if(!data)goto done;
    if(fread(data,1,(size_t)bytes,f)!=(size_t)bytes || fgetc(f)!=EOF || ferror(f)){free(data);data=NULL;}
    else *size=(size_t)bytes;
done:if(fclose(f)){free(data);data=NULL;}return data;
}
int jgr_native_graffiti_load(const JgrNativeConfig *c,Graffiti *g,unsigned stage)
{
    char name[64],path[1024];snprintf(name,sizeof name,"spots%u.bin",stage);
    return jgr_native_asset_path(c,name,path,sizeof path) || graffiti_load(g,path,"") ? -1:0;
}
void jgr_native_graffiti_free(Graffiti *g)
{
    /* The source parser does not create any rendering resources. Keep this
     * matching destructor independent of the runtime stage renderer. */
    if(!g)return;graffiti_can_drops_free(g);
    if(g->spots){for(int i=0;i<g->n;i++)free(g->spots[i].source);free(g->spots);}
    free(g->cans);memset(g,0,sizeof *g);
}
int jgr_native_player_model(const JgrNativeConfig *c,unsigned character,unsigned *model)
{
    JgrDiscFile archive,file;unsigned char header[12];unsigned seen=0,models=0;
    if(!c || !c->disc || character>=16 || !model || jgr_disc_find(c->disc,"JETRADIO/PLAYER.AFS",&archive) || jgr_disc_read(c->disc,&archive,0,header,8))return -1;
    unsigned count=native_word(header+4);if(count>512)return -1;
    for(unsigned member=0;member<count;member++){
        if(jgr_disc_afs(c->disc,&archive,member,&file) || file.size<12 || jgr_disc_read(c->disc,&file,0,header,12))return -1;
        if(memcmp(header,"PLAY",4))continue;
        unsigned table=native_word(header+8);if(jgr_disc_read(c->disc,&file,table,header,4))return -1;
        unsigned identity=native_word(header);if(identity>=16 || (seen&(1u<<identity)) || models>=16)return -1;
        if(identity==character)*model=models;models++;seen|=1u<<identity;
    }
    return seen==65535?0:-1;
}
int jgr_native_collision_load(const JgrNativeConfig *c,Collision *world,unsigned stage)
{
    char path[1024],name[40];if(stage<1 || stage>6)return -1;
    if(stage>3){
        snprintf(name,sizeof name,"stage%u.col",stage);
        return jgr_native_asset_path(c,name,path,sizeof path) || col_load(world,path) ? jgr_native_error(c,"Native replay collision is unavailable"):0;
    }
    if(!c->scratch_directory)return jgr_native_error(c,"Native collision scratch directory is unavailable");
    if(c->cache){
        if(jgr_native_cache_collision(c->cache,c->disc,c->scratch_directory,stage,path,sizeof path) || col_load(world,path))
            return jgr_native_error(c,"Native replay cached collision is unavailable");
        return 0;
    }
    int n=snprintf(path,sizeof path,"%s/native-stage%u.col.tmp",c->scratch_directory,stage);
    if(n<0 || (size_t)n>=sizeof path)return jgr_native_error(c,"Native collision scratch path is too long");
    FILE *out=fopen(path,"w+b");if(!out)return jgr_native_error(c,"Native collision scratch could not be opened");
    int bad=jgr_world_native_collision(c->disc,stage,out);if(fclose(out))bad=1;
    if(!bad)bad=col_load(world,path);if(remove(path) && !bad)bad=1;
    return bad?jgr_native_error(c,"Native collision conversion failed"):0;
}
int jgr_native_objects_load(const JgrNativeConfig *c,Collision *world,unsigned stage,unsigned set,unsigned subset)
{
    char name[64];size_t mesh_bytes=0,set_bytes=0;unsigned char *mesh=NULL,*sets=NULL;int ok=0;
    snprintf(name,sizeof name,"stage%u.sob",stage);mesh=jgr_native_asset_read(c,name,&mesh_bytes,8u*1024u*1024u);
    snprintf(name,sizeof name,"set%u_obj.bin",set);sets=jgr_native_asset_read(c,name,&set_bytes,1024u*1024u);
    if(!mesh || !sets || mesh_bytes<8 || set_bytes<8 || memcmp(mesh,"SOB1",4) || memcmp(sets,"SOB1",4))goto done;
    unsigned types=native_word(mesh+4),subsets=native_word(sets+4);if(types>32 || subset>=subsets)goto done;
    const ColTri *tri[32]={0};unsigned counts[32]={0};unsigned char *p=mesh+8,*end=mesh+mesh_bytes;
    for(unsigned i=0;i<types;i++){
        if(end-p<4)goto done;counts[i]=native_word(p);p+=4;
        if(counts[i]>(size_t)(end-p)/sizeof(ColTri))goto done;tri[i]=(const ColTri *)p;p+=counts[i]*sizeof(ColTri);
    }
    p=sets+8;end=sets+set_bytes;col_reset_extra(world);
    for(unsigned i=0;i<=subset;i++){
        if(end-p<4)goto done;unsigned count=native_word(p);p+=4;
        if(count>(size_t)(end-p)/24)goto done;
        for(unsigned j=0;j<count;j++,p+=24)if(i==subset){
            unsigned type=native_word(p+4);float v[4];memcpy(v,p+8,sizeof v);
            for(unsigned k=0;k<4;k++)if(!isfinite(v[k]))goto done;
            if(type<types && counts[type] && set_collision_append(world,tri[type],counts[type],v[0],v[1],v[2],v[3]))goto done;
        }
    }
    ok=!col_rebuild(world);
done:free(mesh);free(sets);return ok?0:jgr_native_error(c,"Native replay object collision is invalid");
}
int jgr_native_player_parameters(const JgrNativeConfig *c,int32_t surfaces[19],unsigned idle_thresholds[2])
{
    JgrDiscFile executable;
    if(!c || !c->disc || jgr_disc_source(c->disc,"decompile/extracted/1ST_READ.BIN",&executable) ||
       jgr_disc_read(c->disc,&executable,0x8c10db0cu-0x8c010000u,surfaces,19*4) ||
       jgr_disc_read(c->disc,&executable,0x8c17ffc8u+0x3cu-0x8c010000u,idle_thresholds,8))return jgr_native_error(c,"Native replay movement parameters are unavailable");
    if(surfaces[18]!=-1)return -1;
    for(unsigned i=0;i<18;i++)if(surfaces[i]<0 || surfaces[i]>255 || SURF_IS_RAIL(surfaces[i]))return -1;
    if(idle_thresholds[0]>30000 || idle_thresholds[1]>30000 || idle_thresholds[1]<idle_thresholds[0])return -1;
    int32_t landing[64];unsigned count=0;
    for(;count<64;count++){
        if(jgr_disc_read(c->disc,&executable,0x8c10dbb4u-0x8c010000u+count*4,landing+count,4))return -1;
        if(landing[count]==-1)break;
        if(landing[count]<0 || landing[count]>=64)return -1;
    }
    if(count==64 || !jgr_native_player_landing_surfaces(landing,count))return -1;
    for(count=0;count<64;count++){
        if(jgr_disc_read(c->disc,&executable,0x8c10dbacu-0x8c010000u+count*4,landing+count,4))return -1;
        if(landing[count]==-1)break;
        if(landing[count]<0 || landing[count]>=64)return -1;
    }
    return count<64 && jgr_native_player_collision_surfaces(landing,count)?0:-1;
}
struct JgrNativeWorld {
    const JgrNativeConfig *config;Collision *collision;Player *player;PlayerFollow *follow;
    PathCampaign traffic;MissionState mission;MissionRec records[256];
    Graffiti graffiti;Zones zones;Chars spray;JgrNativeParked *parked;
    void *traffic_image,*grip_image,*camera_image,*cover_image;
    int cover_handle,rival_cans;unsigned subset;const CtrlPlay *input;
};
static int native_world_before(void *context)
{
    JgrNativeWorld *w=context;Player *p=w->player;if(p->reset_query)return 0;
    if(w->parked && jgr_native_parked_tick(w->parked))return -1;
    const float feet[3]={p->x,p->y,p->z};
    return pathworld_tick(&w->traffic.world,feet,w->follow->view.pos);
}
static int native_world_contact(void *context,Player *p,int handler)
{
    JgrNativeWorld *w=context;const CtrlPlay *input=w->input;if(!input)return 0;
    Graffiti *g=&w->graffiti;unsigned edges=input->cur[1]&~input->prev;
    g->hud_player_state=p->state;g->block_large=handler!=1;
    graffiti_update_input(g,p->x,p->y,p->z,p->yaw,!!(edges&4),!!(edges&1),
        (int)(signed char)input->cur[2]*256,input->cur[3]*(1.f/255.f),p->character_id,1.f/30.f);
    return p->spray_active;
}
void jgr_native_world_destroy(JgrNativeWorld *w)
{
    if(!w)return;
    if(w->player){jgr_native_player_contact(w->player,NULL,NULL);w->player->before_tick=NULL;w->player->surface_contact=NULL;w->player->car_pull_query=NULL;w->player->spray_bank=NULL;}
    pathmission_detach(&w->traffic);mission_free(&w->mission);jgr_native_parked_free(&w->parked);
    if(w->cover_handle)col_surface_remove(w->collision,w->cover_handle);
    jgr_native_graffiti_free(&w->graffiti);zones_free(&w->zones);jgr_native_bank_free(w->config,&w->spray);
    free(w->traffic_image);free(w->grip_image);free(w->camera_image);free(w->cover_image);
    jgr_native_player_spray_tracks(NULL,0);free(w);
}
JgrNativeWorld *jgr_native_world_create(const JgrNativeConfig *c,Collision *col,Player *p,PlayerFollow *follow,
    Chars *bank,unsigned model,unsigned stage,unsigned set,unsigned subset,const char *mission_name,unsigned ordinal)
{
    JgrNativeWorld *w=calloc(1,sizeof *w);if(!w){jgr_native_error(c,"Not enough native replay world memory");return NULL;}
    w->config=c;w->collision=col;w->player=p;w->follow=follow;w->subset=subset;
    char path[1024],name[64];size_t n=0;
    if(stage==1){
        if(!jgr_native_parked_load(&w->parked,col,c->disc,c->dependency,c->dependency_user,stage,set,subset))goto fail;
        jgr_native_parked_bind(w->parked,p);
    }else if(jgr_native_objects_load(c,col,stage,set,subset))goto fail;
    if(jgr_native_asset_path(c,"missions.bin",path,sizeof path))goto fail;
    int count=missions_read(path,w->records,256);if(count<1)goto fail;
    const MissionRec *record=NULL;
    for(int i=0;i<count;i++)if(!strcmp(w->records[i].name,mission_name)){
        record=mission_chain_select(w->records,count,w->records+i,ordinal);break;
    }
    if(!record || record->st+1!=stage || record->set!=set)goto fail;
    if(jgr_native_graffiti_load(c,&w->graffiti,stage))goto fail;
    snprintf(name,sizeof name,"set%u_cans.bin",set);
    if(jgr_native_asset_path(c,name,path,sizeof path) || graffiti_load_cans(&w->graffiti,path))goto fail;
    graffiti_reset(&w->graffiti);graffiti_cans_reset(&w->graffiti,p->tun->can_capacity,0);
    w->rival_cans=p->tun->can_capacity;
    w->graffiti.actor=jgr_native_player_spray;w->graffiti.actor_context=p;w->graffiti.hud_sine=jgr_native_player_fsca_sin;
    snprintf(name,sizeof name,"zones%u.bin",stage);
    if(jgr_native_asset_path(c,name,path,sizeof path) || zones_load(&w->zones,path))goto fail;
    snprintf(name,sizeof name,"spraymesh%02u.jgc",model);
    if(jgr_native_bank_load(c,&w->spray,name) || chars_share_motions(&w->spray,bank,model))goto fail;
    p->spray_bank=&w->spray;
    w->camera_image=jgr_native_asset_read(c,"spraycam.bin",&n,1024u*1024u);
    if(!w->camera_image || !jgr_native_player_spray_tracks(w->camera_image,(unsigned)n))goto fail;
    snprintf(name,sizeof name,"traffic%u.bin",stage);w->traffic_image=jgr_native_asset_read(c,name,&n,8u*1024u*1024u);
    if(!w->traffic_image || pathworld_bind(&w->traffic.world,w->traffic_image,n,col))goto fail;
    snprintf(name,sizeof name,"traffic%u_grips.bin",stage);w->grip_image=jgr_native_asset_read(c,name,&n,1024u*1024u);
    if(!w->grip_image || pathworld_bind_grips(&w->traffic.world,w->grip_image,n))goto fail;
    /* Keep authored traffic clocks and moving surfaces, but prepare exact
       vehicle bodies only when a contact query needs them, as in gameplay. */
    w->traffic.world.defer_bodies=1;
    if(pathmission_attach(&w->traffic,&w->mission) || jgr_native_asset_path(c,"missions.bin",path,sizeof path) || mission_start(&w->mission,record,path,&w->graffiti))goto fail;
    if(stage==2){
        unsigned char *image=w->cover_image=jgr_native_asset_read(c,"source_data_kogane_cover_data.jsd",&n,1024u*1024u);
        if(!image || n<96 || memcmp(image,"JGSD",4) || native_word(image+4)!=1 || native_word(image+8)!=4 || native_word(image+12)!=n)goto fail;
        unsigned index=(mission_word(&w->mission,0xe0)&1)?2:3;
        unsigned offset=native_word(image+32+index*16+4),bytes=native_word(image+32+index*16+8);
        if(offset>n || bytes>n-offset || bytes%sizeof(ColTri))goto fail;
        w->cover_handle=col_surface_add(col,(const ColTri *)(image+offset),NULL,bytes/sizeof(ColTri));if(!w->cover_handle)goto fail;
    }
    p->before_tick=native_world_before;p->before_tick_context=w;
    p->car_pull_query=pathworld_grip_query;p->car_pull_context=&w->traffic.world;
    jgr_native_player_contact(p,native_world_contact,w);return w;
fail:jgr_native_error(c,"Native replay world dependency or layout is invalid");jgr_native_world_destroy(w);return NULL;
}
int jgr_native_world_step(JgrNativeWorld *w,const CtrlPlay *input,float ax,float ay,unsigned buttons,unsigned edge)
{
    Player *p=w->player;Graffiti *g=&w->graffiti;
    int zone=zone_of(&w->zones,p->x,p->y,p->z),area=zone_area(&w->zones,zone);
    graffiti_cans_select(g,w->subset,(unsigned)area<5?1u<<area:0);g->can_area=area;
    canplay_update(g,p,NULL,1.f/30.f);g->block_large=p->state!=1;
    if(p->spray_active){
        graffiti_update_input(g,p->x,p->y,p->z,p->yaw,!!(edge&GAME_SPRAY),!!(edge&PSP_CTRL_CROSS),
            (int)(signed char)input->cur[2]*256,input->cur[3]*(1.f/255.f),p->character_id,1.f/30.f);
    }else{
        w->input=input;
        jgr_native_player_update_axes(p,w->collision,ax,ay,buttons,edge,p->cam_yaw,1.f/30.f);
        w->input=NULL;
    }
    jgr_native_player_follow_spray(w->follow);
    canplay_refresh_contacts(g,p);
    mission_position(&w->mission,p->x,p->y,p->z);mission_tick(&w->mission,g,0);
    return w->traffic.world.error || jgr_native_player_observer_failed() ? jgr_native_error(w->config,"Native replay world step failed"):0;
}
int jgr_native_world_rival_step(JgrNativeWorld *w,const CtrlPlay *input,float ax,float ay,unsigned buttons,unsigned edge,int race)
{
    if(!w || !input)return -1;
    Player *p=w->player;Graffiti *g=&w->graffiti;w->input=NULL;
    if(race){
        /* 01abb8 consumes the target queued from the previous completed
         * actor position. Its small-tag owner never freezes locomotion. */
        int index=graffiti_rival_spray(g,p->x,p->y,p->z,&w->rival_cans,(input->cur[1]&~input->prev)&4);
        if(index>=0)jgr_native_player_spray(p,g,g->spots+index,SPRAY_ACT_SMALL,0);
        graffiti_update_input(g,p->x,p->y,p->z,p->yaw,0,0,0,0,p->character_id,1.f/30.f);
    }
    jgr_native_player_update_axes(p,w->collision,ax,ay,buttons,edge,p->cam_yaw,1.f/30.f);
    mission_position(&w->mission,p->x,p->y,p->z);mission_tick(&w->mission,g,0);
    return w->traffic.world.error || jgr_native_player_observer_failed()?jgr_native_error(w->config,"Native rival world step failed"):0;
}
int jgr_native_world_rival_wait(JgrNativeWorld *w)
{
    if(!w)return -1;w->input=NULL;
    if(w->parked && jgr_native_parked_tick(w->parked))return -1;
    const float feet[3]={w->player->x,w->player->y,w->player->z};
    if(pathworld_tick(&w->traffic.world,feet,w->follow->view.pos))return -1;
    jgr_native_player_rival_wait_tick(w->player);
    mission_position(&w->mission,w->player->x,w->player->y,w->player->z);mission_tick(&w->mission,&w->graffiti,0);
    return w->traffic.world.error || jgr_native_player_observer_failed()?jgr_native_error(w->config,"Native rival wait failed"):0;
}
int jgr_native_sample(const Player *p,const PlayerFollow *follow,unsigned part,unsigned cut,JgrReplayNativeSample *s)
{
    if(!p || !follow || !s || !p->pose_valid || !p->pose_bank)return -1;
    memset(s,0,sizeof *s);s->position[0]=p->x;s->position[1]=p->y;s->position[2]=p->z;
    memcpy(s->angle,p->visual_angles,sizeof s->angle);s->state=p->state;
    memcpy(s->camera_position,follow->view.pos,12);memcpy(s->camera_direction,follow->view.forward,12);
    s->camera_roll=follow->view.roll;s->camera_fov=follow->view.fov;
    s->near_clip=follow->view.near_clip;s->far_clip=follow->view.far_clip;
    s->tick=p->logic_tick;s->part=part;s->cut=cut;
    const Chars *bank=p->spray_variant&&p->spray_bank?p->spray_bank:p->pose_bank;
    unsigned model=bank==p->spray_bank?0:(unsigned)p->pose_model;if(model>=bank->h->n_chars)return -1;
    const JgcChar *ch=bank->chars+model;if(ch->nbones!=20)return -1;
    for(unsigned j=0;j<20;j++){
        CharPose resolved;if(jgr_native_player_sample_pose(p,j,&resolved))return -1;
        const CharPose *pose=&resolved;const JgcBone *bone=bank->bones+ch->bone0+j;JgrReplayNativePose *out=s->pose+j;
        memcpy(out->position,pose->keyed&1?pose->pos:bone->pos,12);
        memcpy(out->angle,pose->keyed&2?pose->ang:bone->ang,12);
        memcpy(out->scale,pose->keyed&4?pose->scl:bone->scl,12);
        out->keyed=pose->keyed;out->evaluation=bone->ev&~pose->keyed;
    }
    SkateInput effects;jgr_native_player_effect_input(p,&effects);
    typedef char EffectsLayoutMustMatch[(sizeof effects==sizeof s->effects)?1:-1];(void)sizeof(EffectsLayoutMustMatch);
    memcpy(&s->effects,&effects,sizeof effects);return 0;
}
