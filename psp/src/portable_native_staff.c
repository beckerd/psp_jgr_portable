/* Setup-only credits: simulate the common world once, then evaluate each
 * cast from typed native animation events. Every authored input is loaded. */
#include "portable_native_staff.h"
#include "portable_replay_session.h"
#include "portable_native_staff_pose.h"
#include "portable_native_player.h"
#include "portable_native_objects.h"
#include "portable_native_math.h"
#include "staff_replay.h"
#include "game_controls.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define STAFF_PARTS 9
#define STAFF_MAX_FRAMES 30000
#define STAFF_MAX_WALLS 3072
#define STAFF_MAX_SPRAYS 1024

typedef struct {unsigned count,parts,walls;uint32_t segment[STAFF_PARTS][6],wall[STAFF_MAX_WALLS][8];} StaffCommon;
typedef struct {Graffiti *graffiti;const CtrlPlay *input;} StaffContact;
static uint32_t word(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int write_words(FILE *f,const uint32_t *v,unsigned count)
{
    unsigned char bytes[4];
    for(unsigned i=0;i<count;i++){uint32_t w=v[i];bytes[0]=w;bytes[1]=w>>8;bytes[2]=w>>16;bytes[3]=w>>24;
        if(fwrite(bytes,1,4,f)!=4)return 0;}
    return 1;
}
static int read_words(FILE *f,uint32_t *v,unsigned count)
{
    unsigned char bytes[4];for(unsigned i=0;i<count;i++){if(fread(bytes,1,4,f)!=4)return 0;v[i]=word(bytes);}return 1;
}
static int fail(const JgrNativeConfig *c,const char *message)
{jgr_native_error(c,message);return 0;}
static int contact(void *context,Player *p,int handler)
{
    StaffContact *s=context;const CtrlPlay *input=s->input;Graffiti *g=s->graffiti;
    if(!input)return 0;
    unsigned edges=input->cur[1]&~input->prev;
    g->hud_player_state=p->state;g->block_large=handler!=1;
    graffiti_update_input(g,p->x,p->y,p->z,p->yaw,!!(edges&4),!!(edges&1),
        (int)(signed char)input->cur[2]*256,input->cur[3]*(1.f/255.f),p->character_id,1.f/30.f);
    return p->spray_active;
}
static int wall_order(const void *a,const void *b)
{
    const uint32_t *x=a,*y=b;
    for(unsigned i=0;i<3;i++)if(x[i]!=y[i])return x[i]<y[i]?-1:1;
    return 0;
}
static int walls_record(StaffCommon *s,const Graffiti *g,uint32_t *previous)
{
    for(int i=0;i<g->n;i++){
        const Spot *p=g->spots+i;uint32_t values[5]={p->stroke,p->reveal,p->complete,p->started,p->revealing};
        if(p->area<0 || p->area>=6 || p->idx<0 || p->idx>=512 || p->complete>1 ||
           p->started<0 || p->started>1 || p->revealing<0 || p->revealing>2)return 0;
        if(!memcmp(previous+i*5,values,sizeof values))continue;
        if(s->walls>=STAFF_MAX_WALLS)return 0;
        uint32_t *cue=s->wall[s->walls++];cue[0]=s->count;cue[1]=p->area;cue[2]=p->idx;
        memcpy(cue+3,values,sizeof values);memcpy(previous+i*5,values,sizeof values);
    }return 1;
}
static int common_finish(FILE *f,StaffCommon *s)
{
    long offset=ftell(f);if(offset<0 || (unsigned long)offset>UINT32_MAX || s->parts!=STAFF_PARTS || !s->count)return 0;
    qsort(s->wall,s->walls,sizeof *s->wall,wall_order);
    for(unsigned i=1;i<s->walls;i++)if(!wall_order(s->wall[i-1],s->wall[i]))return 0;
    uint32_t footer[5]={1,(uint32_t)offset,s->count,s->parts,s->walls};
    return write_words(f,&s->segment[0][0],s->parts*6) && write_words(f,&s->wall[0][0],s->walls*8) &&
        fwrite("NSF1",1,4,f)==4 && write_words(f,footer,5) && !fflush(f);
}
static int common_read(FILE *f,StaffCommon *s)
{
    unsigned char magic[4];uint32_t footer[5];long length;
    if(fseek(f,0,SEEK_END) || (length=ftell(f))<24 || fseek(f,length-24,SEEK_SET) ||
       fread(magic,1,4,f)!=4 || memcmp(magic,"NSF1",4) || !read_words(f,footer,5) ||
       footer[0]!=1 || !footer[2] || footer[2]>STAFF_MAX_FRAMES || footer[3]!=STAFF_PARTS || footer[4]>STAFF_MAX_WALLS ||
       footer[1]>(unsigned long)length || (unsigned long)footer[1]+STAFF_PARTS*24+footer[4]*32+24!=(unsigned long)length ||
       fseek(f,(long)footer[1],SEEK_SET))return 0;
    s->count=footer[2];s->parts=footer[3];s->walls=footer[4];
    if(!read_words(f,&s->segment[0][0],s->parts*6) || !read_words(f,&s->wall[0][0],s->walls*8))return 0;
    unsigned count=0;for(unsigned i=0;i<s->parts;i++){
        const uint32_t *part=s->segment[i];
        if(part[0]!=i+2 || part[1]!=count || !part[2] || part[2]>s->count-count ||
           part[3]>8 || !part[4] || part[4]>8 || part[5])return 0;
        count+=part[2];
    }
    for(unsigned i=0;i<s->walls;i++){
        const uint32_t *wall=s->wall[i];
        if(wall[0]>=s->count || wall[1]>=6 || wall[2]>=512 || wall[5]>1 || wall[6]>1 || wall[7]>2 ||
           (i && wall_order(s->wall[i-1],wall)>=0))return 0;
    }
    return count==s->count && !fseek(f,0,SEEK_SET);
}
int jgr_native_staff_record(const JgrNativeConfig *c,FILE *common)
{
    if(!c || !c->disc || !common)return fail(c,"Native credits context is unavailable");
    jgr_replay_stats_stage(JGR_REPLAY_SETUP);
    Chars bank={0},spray={0};Collision collision={0};Graffiti graffiti={0};Player *p=calloc(1,sizeof *p);PlayerFollow follow;
    JgrNativeParked *objects=NULL;JgrNativeStaffPoseRecorder *record=NULL;StaffCommon *meta=NULL;
    StaffReplay replay;StaffContact hook={&graffiti,NULL};
    void *staff=NULL,*camera_image=NULL;size_t staff_bytes=0,camera_bytes=0;uint32_t *previous=NULL;
    int32_t surfaces[19];unsigned idle[2],model=0;int ok=0;JgrDiscFile mission;unsigned char mission_header[20];
    if(!p || !jgr_native_math_load(c->disc) || jgr_native_player_parameters(c,surfaces,idle) || jgr_native_player_model(c,0,&model))goto done;
    /* The STAFF controller retains the final mission's parked-object bank.
     * Read its placement selection rather than copying any placement data. */
    if(jgr_disc_source(c->disc,"decompile/afs/E_MISSIONDATA/037_mission_data_s4m02.bin",&mission) ||
       jgr_disc_read(c->disc,&mission,0,mission_header,sizeof mission_header) || memcmp(mission_header,"JMDc",4))goto done;
    unsigned set=word(mission_header+16);
    if(set>255 || jgr_native_bank_load(c,&bank,"chars.jgc") || jgr_native_collision_load(c,&collision,1) ||
       !jgr_native_parked_load(&objects,&collision,c->disc,c->dependency,c->dependency_user,1,set,0) ||
       jgr_native_graffiti_load(c,&graffiti,1))goto done;
    char name[40];snprintf(name,sizeof name,"spraymesh%02u.jgc",model);
    if(jgr_native_bank_load(c,&spray,name) || chars_share_motions(&spray,&bank,model))goto done;
    staff=jgr_native_asset_read(c,"staffroll.bin",&staff_bytes,1024u*1024u);
    camera_image=jgr_native_asset_read(c,"spraycam.bin",&camera_bytes,1024u*1024u);
    if(!staff || !camera_image || !staff_replay_bind(&replay,staff,(unsigned)staff_bytes) ||
       !jgr_native_player_spray_tracks(camera_image,(unsigned)camera_bytes) || graffiti.n<0 || graffiti.n>3072)goto done;
    meta=calloc(1,sizeof *meta);previous=calloc(graffiti.n?graffiti.n*5:1,sizeof *previous);
    record=jgr_native_staff_pose_record_create(common);if(!meta || !previous || !record)goto done;
    chars_set_pose_sine(jgr_native_player_fsca_sin);
    uint32_t prior_camera=0;
    jgr_replay_stats_stage(JGR_REPLAY_GENERATION);
    while(!replay.done && !replay.error && replay.clock<STAFF_MAX_FRAMES){
        int spawned=staff_replay_step(&replay);
        if(spawned){
            unsigned part=(unsigned)replay.row-2;if(part!=meta->parts || part>=STAFF_PARTS)goto done;
            if(!jgr_native_staff_pose_record_begin(record,p,part))goto done;
            jgr_native_player_spawn(p,&bank,model,0,&replay.stream,1,idle);
            jgr_native_player_follow_bind(&follow,p,&collision,surfaces);
            graffiti_reset(&graffiti);graffiti.cancount=graffiti.can_capacity=p->tun->can_capacity;
            graffiti.actor=jgr_native_player_spray;graffiti.actor_context=p;graffiti.hud_sine=jgr_native_player_fsca_sin;
            p->spray_bank=&spray;jgr_native_parked_bind(objects,p);jgr_native_player_contact(p,contact,&hook);
            uint32_t *segment=meta->segment[meta->parts++];segment[0]=replay.row;segment[1]=meta->count;
            segment[3]=replay.tick;segment[4]=p->logic_tick+1;
        }
        if(replay.phase!=1)continue;
        if(meta->count>=STAFF_MAX_FRAMES)goto done;
        float ax,ay;unsigned buttons;ctrl_play_input(&replay.input,&ax,&ay,&buttons);
        unsigned mapped=game_recorded_buttons(buttons),last=game_recorded_buttons(replay.input.prev);
        if(p->spray_active){
            hook.input=NULL;unsigned edges=replay.input.cur[1]&~replay.input.prev;
            graffiti.hud_player_state=p->state;graffiti.block_large=p->state!=1;
            graffiti_update_input(&graffiti,p->x,p->y,p->z,p->yaw,!!(edges&4),!!(edges&1),
                (int)(signed char)replay.input.cur[2]*256,replay.input.cur[3]*(1.f/255.f),p->character_id,1.f/30.f);
        }else{
            hook.input=&replay.input;jgr_native_player_update_axes(p,&collision,ax,ay,mapped,mapped&~last,p->cam_yaw,1.f/30.f);hook.input=NULL;
        }
        jgr_native_player_follow_spray(&follow);FollowInput body;SceneCamera camera;
        jgr_native_player_follow_input(p,&body);staff_replay_camera_target(&replay,body.pos,body.body,&camera);
        unsigned tracks=camera.active && !!(scene_word(&replay.source,replay.camera+8)&32);
        if(!camera.active)camera=follow.view;
        unsigned part=meta->parts-1,cut=spawned || prior_camera!=replay.camera;
        if(jgr_native_player_observer_failed() || !jgr_native_staff_pose_record_frame(record,p,&camera,part,cut,tracks) ||
           !walls_record(meta,&graffiti,previous))goto done;
        meta->segment[part][2]++;meta->count++;prior_camera=replay.camera;
        if(c->progress && !(meta->count&63) && !c->progress(meta->count,c->progress_user))goto done;
    }
    jgr_replay_stats_stage(JGR_REPLAY_PACKING);
    if(replay.error || !replay.done || !jgr_native_staff_pose_record_finish(record) || !common_finish(common,meta))goto done;
    ok=1;
done:
    jgr_replay_stats_stage(JGR_REPLAY_SETUP);
    jgr_native_player_contact(NULL,NULL,NULL);jgr_native_player_observe(NULL,NULL,NULL);
    jgr_native_staff_pose_record_destroy(record);jgr_native_parked_free(&objects);col_free(&collision);
    jgr_native_graffiti_free(&graffiti);chars_pose_forget();jgr_native_bank_free(c,&spray);jgr_native_bank_free(c,&bank);
    jgr_native_player_spray_tracks(NULL,0);free(staff);free(camera_image);free(previous);free(meta);free(p);
    if(!ok && c && c->error && !*c->error)fail(c,"Native credits world generation failed");
    return ok;
}

typedef struct {const JgrNativeConfig *config;JgrReplayPack *pack;StaffCommon *common;unsigned count,sprays,prior_part;uint32_t spray[STAFF_MAX_SPRAYS][8];} StaffCast;
static int cast_sample(void *context,Player *p,const SceneCamera *camera,unsigned part,unsigned cut)
{
    StaffCast *s=context;PlayerFollow follow={0};follow.view=*camera;JgrReplayNativeSample sample;
    if(part>=STAFF_PARTS || s->count>=s->common->count || jgr_native_sample(p,&follow,part,cut,&sample) ||
       jgr_replay_pack_native_sample(s->pack,&sample))return 0;
    uint32_t spray[8]={s->count,p->small_spray_active!=0,p->spray_variant!=0,0,0,0,0,0};
    if(spray[1]){
        if(p->small_spray_ref<0 || p->small_spray_ref>65535 || !isfinite(p->small_spray_frame))return 0;
        spray[3]=p->small_spray_ref;memcpy(spray+4,&p->small_spray_frame,4);memcpy(spray+5,p->small_spray_target,12);
        for(unsigned k=0;k<3;k++)if(!isfinite(p->small_spray_target[k]))return 0;
    }
    int changed=!s->sprays || s->prior_part!=part;
    if(!changed){uint32_t expected[8];memcpy(expected,s->spray[s->sprays-1],sizeof expected);
        if(expected[1]){float frame;memcpy(&frame,expected+4,4);frame+=(float)(s->count-expected[0]);memcpy(expected+4,&frame,4);}
        expected[0]=s->count;changed=memcmp(expected,spray,sizeof spray)!=0;
    }
    if(changed){if(s->sprays==STAFF_MAX_SPRAYS)return 0;memcpy(s->spray[s->sprays++],spray,sizeof spray);}
    s->count++;s->prior_part=part;return 1;
}
int jgr_native_staff_generate(const JgrNativeConfig *c,unsigned character,FILE *common,FILE *rows,FILE *output,FILE *metadata)
{
    if(!c || character>=16 || !common || !rows || !output || !metadata)return fail(c,"Native credits output is unavailable");
    jgr_replay_stats_stage(JGR_REPLAY_SETUP);
    StaffCommon *shared=calloc(1,sizeof *shared);StaffCast *cast=calloc(1,sizeof *cast);
    Chars bank={0},spray={0};unsigned model=0;int ok=0;
    if(!shared || !cast || !common_read(common,shared) || !jgr_native_math_load(c->disc) ||
       jgr_native_player_model(c,character,&model) || jgr_native_bank_load(c,&bank,"chars.jgc"))goto done;
    char name[40];snprintf(name,sizeof name,"spraymesh%02u.jgc",model);
    if(jgr_native_bank_load(c,&spray,name) || chars_share_motions(&spray,&bank,model))goto done;
    chars_set_pose_sine(jgr_native_player_fsca_sin);cast->config=c;cast->common=shared;
    cast->pack=jgr_replay_pack_create_native(rows,character);if(!cast->pack)goto done;
    jgr_replay_stats_stage(JGR_REPLAY_GENERATION);
    if(!jgr_native_staff_pose_replay(c,common,&bank,&spray,model,character,cast_sample,cast) ||
       cast->count!=shared->count || jgr_replay_pack_count(cast->pack)!=shared->count)goto done;
    jgr_replay_stats_stage(JGR_REPLAY_PACKING);
    if(jgr_replay_pack_finish(cast->pack,output))goto done;
    if(32+STAFF_PARTS*24+(cast->sprays+shared->walls)*32>128*1024)goto done;
    uint32_t header[7]={1,character,shared->count,STAFF_PARTS,cast->sprays,shared->walls,0};
    if(fwrite("SRT1",1,4,metadata)!=4 || !write_words(metadata,header,7) ||
       !write_words(metadata,&shared->segment[0][0],STAFF_PARTS*6) || !write_words(metadata,&cast->spray[0][0],cast->sprays*8) ||
       !write_words(metadata,&shared->wall[0][0],shared->walls*8) || fflush(output) || fflush(metadata))goto done;
    ok=1;
done:
    jgr_replay_stats_stage(JGR_REPLAY_SETUP);
    jgr_native_player_observe(NULL,NULL,NULL);chars_pose_forget();jgr_native_bank_free(c,&spray);jgr_native_bank_free(c,&bank);
    if(cast)jgr_replay_pack_destroy(cast->pack);
    free(cast);free(shared);
    if(!ok && c && c->error && !*c->error)fail(c,"Native credits pose generation failed");
    return ok;
}
