/* Installation-time mode-3 actors, driven solely by authored controllers and
 * the reconstructed native world. Completed samples never feed simulation. */
#include "portable_native_rival.h"
#include "portable_replay_session.h"
#include "portable_native_player.h"
#include "mission.h"
#include <stdlib.h>
#include <string.h>

static unsigned rival_ticks(const CtrlStream *s)
{
    unsigned ticks=0;
    if(!s || !s->rec || s->nrec<1)return 0;
    for(int i=0;i<s->nrec;i++){
        const unsigned char *r=s->rec+i*4;
        if(r[0]&128)return ticks>1?ticks-1:0;
        if(ticks>30000u-(unsigned)r[0]-1u)return 0;
        ticks+=(unsigned)r[0]+1;
    }
    return 0;
}
int jgr_native_rival_generate(const JgrNativeConfig *c,const char *mission_name,
    unsigned ordinal,unsigned subset,unsigned character,unsigned member,
    unsigned sections,const PortableReplayGangFixture *fixture,FILE *rows,FILE *output)
{
    if(!c || !c->disc || !mission_name || !rows || !output || rows==output || character>=16 || member>=3 ||
       (sections!=2 && sections!=3) || (sections==3 && !fixture) ||
       (sections==2 && (member || fixture)))return -1;
    jgr_replay_stats_stage(JGR_REPLAY_SETUP);
    int result=-1;char path[1024],name[64];unsigned model=0,stage=0,set=0,frames=0,work=0;
    if(c->progress && !c->progress(0,c->progress_user))
        return jgr_native_error(c,"Native rival preparation cancelled");
    MissionRec *records=calloc(256,sizeof *records);Player *p=calloc(1,sizeof *p);
    Chars bank={0};Collision collision={0};CtrlSet controls={0};PlayerFollow follow={0};
    JgrNativeWorld *world=NULL;JgrReplayPack *pack=NULL;int32_t surfaces[19];unsigned idle[2];
    if(!records || !p)goto done;
    if(jgr_native_asset_path(c,"missions.bin",path,sizeof path))goto done;
    int count=missions_read(path,records,256);const MissionRec *record=NULL;
    for(int i=0;i<count;i++)if(!strcmp(records[i].name,mission_name)){
        record=mission_chain_select(records,count,records+i,ordinal);break;
    }
    if(!record || (sections==2 && record->kind!=5)){jgr_native_error(c,"Native rival mission selection invalid");goto done;}
    stage=record->st+1;set=record->set;free(records);records=NULL;
    if(jgr_native_player_model(c,character,&model) || jgr_native_player_parameters(c,surfaces,idle) ||
       jgr_native_bank_load(c,&bank,"chars.jgc") || jgr_native_collision_load(c,&collision,stage))goto done;
    snprintf(name,sizeof name,"set%u_ctrl.bin",set);
    if(jgr_native_asset_path(c,name,path,sizeof path) || ctrl_load(&controls,path))goto done;
    const CtrlSub *sub=ctrl_sub(&controls,(int)subset);
    if(!sub || member>=(unsigned)sub->ntab || (unsigned)sub->nseg[member]<sections){jgr_native_error(c,"Native rival controller table invalid");goto done;}
    const CtrlSeg *segments=sub->segs[member];
    for(unsigned part=0;part<sections;part++){
        if((unsigned)segments[part].stream>=(unsigned)sub->nstream ||
           segments[part].flags!=(part+1==sections?3u:1u)){
            jgr_native_error(c,"Native rival segment flags or stream index invalid");goto done;
        }
        const CtrlStream *stream=sub->streams+segments[part].stream;
        if((unsigned)stream->chr!=character || !rival_ticks(stream)){jgr_native_error(c,"Native rival controller identity invalid");goto done;}
    }
    const CtrlStream *first=sub->streams+segments[0].stream;
    jgr_native_player_spawn(p,&bank,model,character,first,stage,idle);
    jgr_native_player_observe(p,NULL,NULL);
    jgr_native_player_follow_bind(&follow,p,&collision,surfaces);
    world=jgr_native_world_create(c,&collision,p,&follow,&bank,model,stage,set,subset,mission_name,ordinal);
    pack=jgr_replay_pack_create_native(rows,character);
    if(!world || !pack || jgr_replay_pack_rival(pack,sections))goto done;
    jgr_replay_stats_stage(JGR_REPLAY_GENERATION);
    for(unsigned part=0;part<sections;part++){
        const CtrlStream *stream=sub->streams+segments[part].stream;
        if(part || fixture)jgr_native_player_rival_relocate(p,stream);
        /* Waiting is a controller phase, not a second route. The explicit
         * spectator scenario can hold its idle clock while approaching. */
        unsigned phase=fixture?fixture->idle_phase[member][part]:0;
        if(phase>65535){jgr_native_error(c,"Native rival idle phase invalid");goto done;}
        unsigned wait=part?1:0;
        if(!part && fixture){
            /* The source wait handler publishes idle animation until the
               spectator approaches. Prepare that authored entry pose,
               preserving the explicit fixture clock, without replaying the
               unrelated menu/cutscene lead-in or an arbitrary fixed delay. */
            unsigned warm=0;
            do {
                if(phase && p->logical_motion==3)p->frame=(float)(phase-1);
                if(jgr_native_world_rival_wait(world))goto done;
                if(++warm>256){jgr_native_error(c,"Native rival idle entry did not settle");goto done;}
                if(!(++work&31) && c->progress && !c->progress(work,c->progress_user)){
                    jgr_native_error(c,"Native rival preparation cancelled");goto done;
                }
            }while(p->logical_motion!=3 || p->pose_entry_weight<1.f);
        }
        if(part && fixture){
            unsigned longest=0,own=rival_ticks(sub->streams+segments[part-1].stream);
            for(unsigned peer=0;peer<3;peer++){
                if(peer>=(unsigned)sub->ntab || (unsigned)sub->nseg[peer]<sections)goto done;
                unsigned selected=(unsigned)sub->segs[peer][part-1].stream;
                if(selected>=(unsigned)sub->nstream)goto done;
                unsigned ticks=rival_ticks(sub->streams+selected);if(!ticks)goto done;
                if(ticks>longest)longest=ticks;
            }
            wait=longest-own+1;
        }
        for(unsigned i=0;i<wait;i++){
            if(phase && p->logical_motion==3)p->frame=(float)(phase-1);
            if(jgr_native_world_rival_wait(world))goto done;
            if(!(++work&31) && c->progress && !c->progress(work,c->progress_user)){
                jgr_native_error(c,"Native rival preparation cancelled");goto done;
            }
        }
        if(part || fixture)jgr_native_player_rival_relocate(p,stream);
        CtrlPlay input;ctrl_play_start(&input,stream);unsigned ticks=0;
        /* 0278c0 advances on the activation tick before its actor child. */
        while(ctrl_play_tick(&input)){
            float ax,ay;unsigned buttons;ctrl_play_input(&input,&ax,&ay,&buttons);
            unsigned mapped=game_recorded_buttons(buttons),previous=game_recorded_buttons(input.prev);
            /* Effects consume the prior shallow query. The movement epilogue
               queues its replacement only after publishing this tick's splash. */
            float splash_point[3];memcpy(splash_point,p->shallow_point,sizeof splash_point);
            if(jgr_native_world_rival_step(world,&input,ax,ay,mapped,mapped&~previous,sections==2))goto done;
            JgrReplayNativeSample sample;
            if(++ticks>30000 || ++frames>90000 || jgr_native_sample(p,&follow,part,0,&sample))goto done;
            sample.rival_target=splash_point;
            if(jgr_replay_pack_native_sample(pack,&sample))goto done;
            if(!(++work&31) && c->progress && !c->progress(work,c->progress_user)){
                jgr_native_error(c,"Native rival preparation cancelled");goto done;
            }
        }
        if(ticks!=rival_ticks(stream) || p->logic_tick!=ticks){jgr_native_error(c,"Native rival tick count invalid");goto done;}
    }
    jgr_replay_stats_stage(JGR_REPLAY_PACKING);
    if(jgr_replay_pack_finish(pack,output))goto done;
    result=0;
done:
    jgr_replay_stats_stage(JGR_REPLAY_SETUP);
    if(result && c->error && c->error_size && !c->error[0])jgr_native_error(c,"Native rival preparation failed");
    jgr_native_world_destroy(world);jgr_replay_pack_destroy(pack);chars_pose_forget();
    ctrl_free(&controls);col_free(&collision);jgr_native_bank_free(c,&bank);free(records);free(p);return result;
}
