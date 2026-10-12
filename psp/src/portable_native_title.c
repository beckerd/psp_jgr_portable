/* Installation-time attract playback from the user's controller/camera bank. */
#include "portable_native_title.h"
#include "portable_native_player.h"
#include "portable_replay_session.h"
#include "mission.h"
#include <stdlib.h>
#include <string.h>

#define TITLE_BASE 0x8cfe0000u
#define TITLE_LIMIT (1024u*1024u)
#define TITLE_FRAMES 30000u
typedef struct {const unsigned char *data;unsigned bytes;} TitleSource;
static unsigned word(const unsigned char *p)
{return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static float real(const unsigned char *p){unsigned w=word(p);float f;memcpy(&f,&w,4);return f;}
static const unsigned char *at(const TitleSource *s,unsigned pointer,unsigned bytes)
{
    if(pointer<TITLE_BASE || pointer-TITLE_BASE>s->bytes || bytes>s->bytes-(pointer-TITLE_BASE))return NULL;
    return s->data+(pointer-TITLE_BASE);
}
static int stream_read(const TitleSource *source,unsigned pointer,CtrlStream *stream)
{
    const unsigned char *p=at(source,pointer,32);if(!p)return -1;
    memset(stream,0,sizeof *stream);stream->chr=(int)word(p);stream->speed=real(p+4);
    stream->x=real(p+8);stream->y=real(p+12);stream->z=real(p+16);
    stream->w5=word(p+20);stream->yaw=(int)word(p+24);stream->w7=word(p+28);
    if(stream->chr<0 || stream->chr>16 || !isfinite(stream->speed) || !isfinite(stream->x) || !isfinite(stream->y) || !isfinite(stream->z))return -1;
    stream->rec=at(source,pointer+32,4);if(!stream->rec)return -1;
    for(unsigned n=0;n<TITLE_FRAMES;n++){
        p=at(source,pointer+32+n*4,4);if(!p)return -1;
        if(p[0]&128){stream->nrec=(int)n+1;return 0;}
    }
    return -1;
}
static int camera_sample(const TitleSource *source,unsigned *pointer,const Player *p,
    unsigned part,JgrReplayNativeSample *sample)
{
    const unsigned char *row=at(source,*pointer,16);if(!row)return -1;
    unsigned cut=part && p->logic_tick==1;
    /* Timeline timestamps end the current row. Camera clock starts at zero
     * and advances after evaluating each completed actor tick. */
    if(word(row)<=p->logic_tick-1){
        *pointer+=16;row=at(source,*pointer,16);if(!row)return -1;cut=1;
    }
    unsigned flags=word(row+8);const unsigned char *record=at(source,word(row+12),40);
    if(word(row+4)!=1 || (flags!=0x20 && flags!=0x2a) || !record)return -1;
    float eye[3];for(unsigned k=0;k<3;k++){eye[k]=real(record+k*4);if(!isfinite(eye[k]))return -1;}
    FollowInput input;jgr_native_player_follow_input(p,&input);
    if(flags&2){
        float matrix[16];const float one[3]={1,1,1};int32_t angles[3]={0,(flags&8)?input.angle[1]:0,0};
        source_trs_fsca(input.pos,angles,one,matrix,jgr_native_player_fsca_sin);
        source_point(matrix,eye,sample->camera_position);
    }else memcpy(sample->camera_position,eye,sizeof eye);
    for(unsigned k=0;k<3;k++)sample->camera_direction[k]=input.body[k]-sample->camera_position[k];
    follow_normalize(sample->camera_direction);
    sample->camera_roll=(int32_t)word(record+24);sample->camera_fov=(int32_t)word(record+28);
    sample->near_clip=-real(record+32);sample->far_clip=-real(record+36);sample->cut=cut;
    return 0;
}
static int tuning_resolve(const JgrNativeConfig *c,unsigned character,unsigned *resolved)
{
    /* The sentinel selects the displayed character independently of its
     * movement record. Resolve the source's pointer alias rather than
     * assuming it uses the selected character's tuning. */
    JgrDiscFile executable;unsigned char pointer[4],table[17*4];
    if(character>16 || jgr_disc_find(c->disc,"1ST_READ.BIN",&executable) ||
       jgr_disc_read(c->disc,&executable,0x8c019538u-0x8c010000u,pointer,4))return -1;
    unsigned address=word(pointer);if(address<0x8c010000u)return -1;
    if(jgr_disc_read(c->disc,&executable,address-0x8c010000u,table,sizeof table))return -1;
    for(unsigned i=0;i<16;i++)if(word(table+i*4)==word(table+character*4)){*resolved=i;return 0;}
    return -1;
}
int jgr_native_title_generate(const JgrNativeConfig *c,unsigned character,FILE *scratch,FILE *output)
{
    if(!c || !c->disc || character>=16 || !scratch || !output)return -1;
    int result=-1;unsigned frames=0,parts=0,stage=0,set=0,terminated=0;char path[1024];
    unsigned char *data=NULL;MissionRec *missions=NULL;Player *p=NULL;Chars bank={0};Collision collision={0};
    PlayerFollow follow={0};JgrNativeWorld *world=NULL;JgrReplayPack *pack=NULL;JgrDiscFile file;
    int32_t surfaces[19];unsigned idle[2];TitleSource source={0};
    jgr_replay_stats_stage(JGR_REPLAY_SETUP);
    if(jgr_disc_source(c->disc,"decompile/afs/PLAYDEMO/000_ST01_DEMO.BIN",&file) || file.size<20 || file.size>TITLE_LIMIT)goto done;
    data=malloc(file.size);missions=calloc(256,sizeof *missions);p=calloc(1,sizeof *p);
    if(!data || !missions || !p || jgr_disc_read(c->disc,&file,0,data,file.size) ||
       jgr_native_asset_path(c,"missions.bin",path,sizeof path))goto done;
    source=(TitleSource){data,file.size};int count=missions_read(path,missions,256);if(count<1)goto done;
    for(int i=0;i<count;i++)if(!strcmp(missions[i].name,"S1M01")){stage=missions[i].st+1;set=missions[i].set;break;}
    free(missions);missions=NULL;
    if(!stage || jgr_native_player_parameters(c,surfaces,idle) || jgr_native_bank_load(c,&bank,"chars.jgc") ||
       jgr_native_collision_load(c,&collision,stage))goto done;
    pack=jgr_replay_pack_create_native(scratch,character);if(!pack)goto done;
    for(unsigned part=0;part<16;part++){
        const unsigned char *row=at(&source,TITLE_BASE+part*20,20);if(!row)goto done;
        unsigned pointer=word(row);if(!pointer){terminated=1;break;}
        CtrlStream stream;unsigned model,tuning;CtrlPlay input={0};
        if(stream_read(&source,pointer,&stream) || tuning_resolve(c,(unsigned)stream.chr,&tuning))goto done;
        unsigned shown=stream.chr==16?character:(unsigned)stream.chr;
        if(shown!=character || jgr_native_player_model(c,shown,&model))goto done;
        jgr_native_player_spawn(p,&bank,model,shown,&stream,stage,idle);
        p->tun=&PLAYER_TUNING[tuning];p->health=p->health_max=p->tun->health_max;
        jgr_native_player_follow_bind(&follow,p,&collision,surfaces);
        world=jgr_native_world_create(c,&collision,p,&follow,&bank,model,stage,set,0,"S1M01",0);if(!world)goto done;
        unsigned camera=word(row+16);if(!at(&source,camera,16))goto done;
        ctrl_play_start(&input,&stream);
        /* Mode 2 initializes the repeat counter from the first record,
         * then clears the live controller until the first advancement. */
        memset(input.cur,0,sizeof input.cur);parts++;
        jgr_replay_stats_stage(JGR_REPLAY_GENERATION);
        while(ctrl_play_tick(&input)){
            float ax,ay;unsigned buttons;ctrl_play_input(&input,&ax,&ay,&buttons);
            unsigned mapped=game_recorded_buttons(buttons),previous=game_recorded_buttons(input.prev);
            unsigned previous_state=(unsigned)p->state;
            if(jgr_native_world_step(world,&input,ax,ay,mapped,mapped&~previous))goto done;
            JgrReplayNativeSample sample;
            if(frames++>=TITLE_FRAMES || jgr_native_sample(p,&follow,part,0,&sample) || camera_sample(&source,&camera,p,part,&sample))goto done;
            /* Installing the ground controller during a landing leaves
             * the source's published state airborne until the next tick. */
            if(previous_state==2 && sample.state==1)sample.state=2;
            /* Effect consumers use integer BAM modulo one turn. Native
             * float heading retains that angle, not the source's temporary
             * signed/unsigned representation after each controller branch. */
            sample.effects.travel_angles[1]&=65535;
            if(jgr_replay_pack_native_sample(pack,&sample))goto done;
            if(c->progress && !(p->logic_tick&31) && !c->progress(frames,c->progress_user)){
                jgr_native_error(c,"Native title preparation cancelled");goto done;
            }
        }
        jgr_native_world_destroy(world);world=NULL;jgr_replay_stats_stage(JGR_REPLAY_SETUP);
    }
    jgr_replay_stats_stage(JGR_REPLAY_PACKING);
    if(!terminated || !parts || !frames || jgr_replay_pack_finish(pack,output))goto done;
    result=0;
done:
    jgr_replay_stats_stage(JGR_REPLAY_SETUP);
    if(result && c->error && c->error_size && !c->error[0])jgr_native_error(c,"Native title preparation failed");
    jgr_native_world_destroy(world);jgr_replay_pack_destroy(pack);chars_pose_forget();
    col_free(&collision);jgr_native_bank_free(c,&bank);free(missions);free(p);free(data);return result;
}
