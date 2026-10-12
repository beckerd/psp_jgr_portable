/* Native installation-time demos. Only controller/world/model assets are
 * inputs; no recorded world positions or poses are read by this generator. */
#include "portable_native_demo.h"
#include "portable_replay_session.h"
#include "portable_native_player.h"
#include "mission.h"
#include <stdlib.h>
#include <string.h>
#define NATIVE_AUDIO_LIMIT 4096u
#define NATIVE_SPRAY_LIMIT 1024u
typedef struct {unsigned tick,order,kind;int program;} NativeAudio;
struct JgrNativeDemoCues {
    Player *player;unsigned audio_count,spray_count,last_tick,failed,order;
    unsigned active[2],observed[2];
    NativeAudio audio[NATIVE_AUDIO_LIMIT];
    unsigned char sprays[NATIVE_SPRAY_LIMIT][24];
};
static void cue_word(unsigned char *p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void cue_half(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static float cue_float(const unsigned char *p){float v;memcpy(&v,p,4);return v;}
static void native_audio(JgrNativeDemoCues *c,unsigned tick,unsigned kind,int program,unsigned order)
{
    if(c->failed)return;
    if(tick>=65536 || kind<1 || kind>4 || program< -1 || program>127 || c->audio_count==NATIVE_AUDIO_LIMIT){c->failed=1;return;}
    c->audio[c->audio_count++]=(NativeAudio){tick,order,kind,program};
}
JgrNativeDemoCues *jgr_native_demo_cues_create(Player *p)
{JgrNativeDemoCues *c=p?calloc(1,sizeof *c):NULL;if(c)c->player=p;return c;}
void jgr_native_demo_cues_sound(void *context,int kind,int program)
{
    JgrNativeDemoCues *c=context;if(!c || c->failed)return;
    if(kind==PLAYER_SOUND_RESET){c->active[0]=c->active[1]=0;return;}
    /* RDC1 observes original PLAY voice/effect owners. SYS menu cues have
     * their separate gameplay producers and are outside this asset ABI. */
    if(kind==PLAYER_SOUND_SYSTEM)return;
    if(kind<PLAYER_SOUND_VOICE || kind>PLAYER_SOUND_SPRAY){c->failed=1;return;}
    if(kind>=PLAYER_SOUND_GRIND){
        unsigned slot=(unsigned)kind-PLAYER_SOUND_GRIND;
        if(program<0){c->active[slot]=0;return;}
        /* Original persistent owner keeps its program until released. */
        if(c->active[slot])return;c->active[slot]=1;
    }
    if(program<0){c->failed=1;return;}
    native_audio(c,c->player->logic_tick,(unsigned)kind,program,c->order++);
}
int jgr_native_demo_cues_sample(JgrNativeDemoCues *c)
{
    if(!c || c->failed)return -1;
    const Player *p=c->player;unsigned tick=p->logic_tick;
    if(!tick || tick>=65536 || (c->last_tick && tick!=c->last_tick+1) || p->small_spray_ref<0 || p->small_spray_ref>=65536 ||
       (p->small_spray_active && (p->small_spray_ref<160 || p->small_spray_ref>166)))goto fail;
    for(unsigned i=0;i<2;i++){
        if(c->observed[i] && !c->active[i])native_audio(c,tick,3+i,-1,0x80000000u+i);
        c->observed[i]=c->active[i];
    }
    unsigned char cue[24]={0};cue_half(cue,tick);cue_half(cue+2,(unsigned)p->small_spray_ref);
    cue[4]=(unsigned char)!!p->small_spray_active;cue[5]=(unsigned char)!!p->spray_variant;
    memcpy(cue+8,&p->small_spray_frame,4);memcpy(cue+12,p->small_spray_target,12);
    for(unsigned i=0;i<4;i++)if(!isfinite(cue_float(cue+8+i*4)))goto fail;
    int same=0;
    if(c->spray_count){
        const unsigned char *prior=c->sprays[c->spray_count-1];unsigned before=prior[0]|(unsigned)prior[1]<<8;
        same=!memcmp(cue+2,prior+2,4);
        for(unsigned i=0;i<3;i++)if(cue_float(cue+12+i*4)!=cue_float(prior+12+i*4))same=0;
        float expected=(float)((double)cue_float(prior+8)+(p->small_spray_active?tick-before:0));
        if(memcmp(&expected,cue+8,4))same=0;
    }
    if(!same){if(c->spray_count==NATIVE_SPRAY_LIMIT)goto fail;memcpy(c->sprays[c->spray_count++],cue,24);}
    c->last_tick=tick;return c->failed?-1:0;
fail:c->failed=1;return -1;
}
static int native_audio_order(const void *left,const void *right)
{
    const NativeAudio *a=left,*b=right;
    if(a->tick!=b->tick)return a->tick<b->tick?-1:1;
    return a->order<b->order?-1:a->order>b->order;
}
int jgr_native_demo_cues_finish(JgrNativeDemoCues *c,unsigned char **out,unsigned *bytes)
{
    if(!c || !out || !bytes || c->failed || !c->last_tick || !c->spray_count || c->active[0] || c->active[1])return -1;
    qsort(c->audio,c->audio_count,sizeof c->audio[0],native_audio_order);
    unsigned n=16+c->audio_count*4+c->spray_count*24;unsigned char *data=malloc(n);if(!data)return -1;
    memcpy(data,"RDC1",4);cue_word(data+4,c->audio_count);cue_word(data+8,c->spray_count);cue_word(data+12,c->last_tick);
    unsigned active[2]={0,0};
    for(unsigned i=0;i<c->audio_count;i++){
        const NativeAudio *a=c->audio+i;if(a->tick>c->last_tick)goto fail;
        if(a->kind>=3){unsigned k=a->kind-3;if(active[k]!=(a->program<0))goto fail;active[k]=a->program>=0;}
        unsigned char *row=data+16+i*4;cue_half(row,a->tick);row[2]=(unsigned char)a->kind;row[3]=(unsigned char)a->program;
    }
    if(active[0] || active[1])goto fail;
    memcpy(data+16+c->audio_count*4,c->sprays,c->spray_count*24);*out=data;*bytes=n;return 0;
fail:free(data);return -1;
}
void jgr_native_demo_cues_destroy(JgrNativeDemoCues *c){free(c);}

int jgr_native_demo_generate(const JgrNativeConfig *c,const char *mission_name,unsigned ordinal,
    unsigned subset,unsigned character,FILE *scratch,FILE *output,unsigned char **cues,unsigned *cue_bytes)
{
    if(!c || !mission_name || !scratch || !output || !cues || !cue_bytes || character>=16)return -1;
    jgr_replay_stats_stage(JGR_REPLAY_SETUP);
    *cues=NULL;*cue_bytes=0;int result=-1;char path[1024],name[64];unsigned model=0,stage=0,set=0,frames=0;
    MissionRec *records=calloc(256,sizeof *records);Chars bank={0};Collision collision={0};CtrlSet controls={0};
    Player *p=calloc(1,sizeof *p);PlayerFollow follow={0};CtrlPlay input={0};JgrNativeWorld *world=NULL;
    JgrReplayPack *pack=NULL;JgrNativeDemoCues *observer=NULL;int32_t surfaces[19];unsigned idle[2];
    if(!records || !p)goto done;
    if(jgr_native_asset_path(c,"missions.bin",path,sizeof path))goto done;
    int count=missions_read(path,records,256);const MissionRec *record=NULL;if(count<1)goto done;
    for(int i=0;i<count;i++)if(!strcmp(records[i].name,mission_name)){record=mission_chain_select(records,count,records+i,ordinal);break;}
    if(!record)goto done;stage=record->st+1;set=record->set;
    free(records);records=NULL;
    if(jgr_native_player_model(c,character,&model) || jgr_native_player_parameters(c,surfaces,idle) ||
       jgr_native_bank_load(c,&bank,"chars.jgc") || jgr_native_collision_load(c,&collision,stage))goto done;
    snprintf(name,sizeof name,"set%u_ctrl.bin",set);
    if(jgr_native_asset_path(c,name,path,sizeof path) || ctrl_load(&controls,path))goto done;
    const CtrlSub *sub=ctrl_sub(&controls,(int)subset);if(!sub || !sub->ntab || !sub->nseg[0])goto done;
    unsigned selected=sub->segs[0][0].stream;if(selected>=(unsigned)sub->nstream)goto done;
    const CtrlStream *stream=sub->streams+selected;
    jgr_native_player_spawn(p,&bank,model,character,stream,stage,idle);
    jgr_native_player_follow_bind(&follow,p,&collision,surfaces);
    world=jgr_native_world_create(c,&collision,p,&follow,&bank,model,stage,set,subset,mission_name,ordinal);if(!world)goto done;
    observer=jgr_native_demo_cues_create(p);pack=jgr_replay_pack_create_native(scratch,character);if(!observer || !pack)goto done;
    p->sound_blocked=0;p->sound_event=jgr_native_demo_cues_sound;p->sound_context=observer;
    ctrl_play_start(&input,stream);
    jgr_replay_stats_stage(JGR_REPLAY_GENERATION);
    while(ctrl_play_tick(&input)){
        float ax,ay;unsigned buttons;ctrl_play_input(&input,&ax,&ay,&buttons);
        unsigned mapped=game_recorded_buttons(buttons),previous=game_recorded_buttons(input.prev);
        if(jgr_native_world_step(world,&input,ax,ay,mapped,mapped&~previous))goto done;
        if(p->logic_tick>=2){
            JgrReplayNativeSample sample;if(frames++>=30000 || jgr_native_sample(p,&follow,0,0,&sample) ||
                jgr_replay_pack_native_sample(pack,&sample) || jgr_native_demo_cues_sample(observer))goto done;
        }
        if(c->progress && !(p->logic_tick&31) && !c->progress(p->logic_tick,c->progress_user)){jgr_native_error(c,"Native replay preparation cancelled");goto done;}
    }
    jgr_replay_stats_stage(JGR_REPLAY_PACKING);
    if(!frames || jgr_replay_pack_finish(pack,output) || jgr_native_demo_cues_finish(observer,cues,cue_bytes))goto done;
    result=0;
done:
    jgr_replay_stats_stage(JGR_REPLAY_SETUP);
    if(result && c->error && c->error_size && !c->error[0])jgr_native_error(c,"Native demonstration preparation failed");
    if(p){p->sound_event=NULL;p->sound_context=NULL;}
    jgr_native_world_destroy(world);jgr_replay_pack_destroy(pack);jgr_native_demo_cues_destroy(observer);
    chars_pose_forget();ctrl_free(&controls);col_free(&collision);jgr_native_bank_free(c,&bank);free(records);free(p);return result;
}
