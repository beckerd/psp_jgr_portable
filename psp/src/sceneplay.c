#include "sceneplay.h"
#include "font.h"
#include "gfx.h"
#include "audio.h"
#include "audio_spatial.h"
#include "charpose.h"
#include "finalboss.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_story_intro_tables.inc"
#else
#include "generated/source_data/story_intro_tables.inc"
#endif
#include "scene_pose.h"
#include "event_audio.h"
#include "scene_models.h"
#include "scene_archive.h"
static const unsigned skater_models[16]={4,6,3,2,1,7,10,0,15,8,13,11,12,5,9,14};
static int load_mouth(Chars *bank,int character)
{
    if((unsigned)character>=16)return -1;
    char path[80];snprintf(path,sizeof path,"recruitmouth%02u.jgc",skater_models[character]);
    int err=chars_load(bank,path);
    return err;
}

void sceneplay_free(ScenePlay *p)
{
    if(p->event_audio_loaded)audio_free_event();
    chars_free(&p->flare);chars_free(&p->blast);chars_free(&p->alternate);
    for(unsigned i=0;i<p->model_count;i++){chars_free(&p->models[i].bank);chars_free(&p->models[i].mouth);}free(p->models);
    if(p->actors_loaded)chars_free(&p->actors);if(p->effects_loaded)chars_free(&p->effects);if(p->mouth_loaded)chars_free(&p->mouth);if(p->player_mouth_loaded)chars_free(&p->player_mouth);if(p->audio_loaded || p->recruit_audio_loaded)audio_free_scene_banks();if(p->object_audio_loaded)audio_free_final_voice();if(p->object_bank_loaded)audio_free_final_objects();if(p->wide_font_owned)font_real_free_wide();scene_storage_free(&p->image);memset(p,0,sizeof *p);
}
static int scene_has_command(const Scene *s,unsigned code)
{
    for(unsigned phase=0;phase<3;phase++)for(uint32_t pc=s->scripts[phase];scene_range(s,pc,20);pc+=20) {
        unsigned op=scene_word(s,pc);if(op==code)return 1;if(op==100)break;
    }
    return 0;
}
int sceneplay_preflight(int id,unsigned *bytes)
{
    if(!scene_supported(id) && !scene_dialogue_mode(id) && !scene_bespoke_supported(id))return 0;
    FILE *f=fopen("scenes.bin","rb");
    uint32_t rec[5];int result=scene_archive_record(f,id,rec);
    if(f)fclose(f);
    if(result>0 && bytes)*bytes=rec[4];
    return result;
}
static int begin(ScenePlay *p,int id,int character,int preload,int final_route,int recruit_mode)
{
    /* Enable only the source/runtime-audited route. Other descriptors remain
       explicitly unsupported; packaging their source bytes is not execution. */
    int dialogue=scene_dialogue_mode(id);
    int recruitment=scene_recruit_supported(id,recruit_mode) || (dialogue && dialogue==recruit_mode);
    int generic=final_route && scene_bespoke_supported(id);
    if(!scene_supported(id) && !(final_route && (id==103 || id==90)) && !recruitment && !generic)return 0;
    /* 04eab8 is idempotent for the resident descriptor; repeated play
       clears preload, but must not restart an already running EVENT. */
    if(p->image.pages&&p->vm.id==id&&scene_busy(&p->vm)){if(!preload)scene_play(&p->vm);return 1;}
    sceneplay_free(p);
    FILE *f=fopen("scenes.bin","rb");if(!f)return -1;
    uint32_t rec[5];int record=scene_archive_record(f,id,rec);
    if(record<0){fclose(f);return record;}
    int loaded=scene_storage_load(&p->image,f,rec[3],rec[4]);
    fclose(f);
    if(loaded<0){sceneplay_free(p);return loaded;}
    if(!scene_start_pages(&p->vm,p->image.pages,rec[4],rec[2],id,rec[1],character)){sceneplay_free(p);return -5;}
    p->vm.language=1; /* original language 1 = English, 0 = Japanese */
    p->vm.sin_bam=player_fsca_sin;
    p->vm.recruit_mode=recruit_mode;
    p->vm.mission_context=1;p->generic=generic;
    uint32_t primary_cues,recruit_cues;
    if(!scene_voice_usage(&p->vm,&primary_cues,&recruit_cues)){sceneplay_free(p);return -5;}
    if(generic){p->inspection_rng=1;p->vm.random15=finalboss_random15;p->vm.random_ctx=&p->inspection_rng;}
    p->voice_ref=-1;
    if(id==4) {
        /* EVENT_0092 uses the mission BGM command and no 36/43 object or
           player voice command. Do not load unrelated PLAY SOSB voices. */
    } else if(scene_supported(id) || recruitment || (generic && (primary_cues || recruit_cues))) {
        p->audio_error=audio_load_scene_cues(character,primary_cues,recruit_cues);
        if(!p->audio_error)p->audio_loaded=1;
        else printf("SCENE audio banks unavailable, id=%d character=%d error=%d cues=%08x/%08x\n",id,character,p->audio_error,(unsigned)primary_cues,(unsigned)recruit_cues);
    } else if(generic) {
        /* Scenes without a primary voice do not need its PLAY sound bank. */
    } else if(id==103) {
        p->audio_error=audio_load_final_voice();
        if(!p->audio_error)p->object_audio_loaded=1;
        else printf("SCENE 103: original Goji ADX voice unavailable (%d)\n",p->audio_error);
    } else {
        p->audio_error=audio_load_final_objects();
        if(!p->audio_error)p->object_bank_loaded=1;
        printf("SCENE 90: original SOSB/SMSB banks loaded=%d; refs 3/4/6 enabled, explosion ref 1 unrecovered\n",p->object_bank_loaded);
    }
    const char *bank=id==4 ? "scene0092.jgc":id==7 ? "scene0100.jgc":id==97 ? "scene5056.jgc":id==90 ? "scene3025.jgc":id==103 ? "scene6198.jgc":NULL;
    int err=0;
    if(generic) {
        if(p->audio_error || (err=audio_load_event(id))){sceneplay_free(p);return -32;}
        p->event_audio_loaded=1;
        if(scene_load_models(p)){sceneplay_free(p);return -34;}
        if(scene_has_command(&p->vm,0x5e) || scene_has_command(&p->vm,0x5f) || scene_has_command(&p->vm,0x5d)) {
            if(scene_load_bank(&p->flare,"eventflare.jgc")){sceneplay_free(p);return -35;}p->flare_loaded=1;
        }
        if(scene_has_command(&p->vm,0x5b)) {
            if(scene_load_bank(&p->blast,"eventblast.jgc")){sceneplay_free(p);return -35;}p->blast_loaded=1;
        }
        int text=scene_has_command(&p->vm,0x15);
        for(unsigned op=0x18;op<=0x1e;op++)text|=scene_has_command(&p->vm,op);
        if(text && !font_real_wide_ready()) {
            err=font_real_load_wide("fontwide.bin");
            if(err){sceneplay_free(p);return -33;}p->wide_font_owned=1;
        }
        if(scene_has_command(&p->vm,0x39)) {
            if(load_mouth(&p->player_mouth,character)){sceneplay_free(p);return -31;}p->player_mouth_loaded=1;
        }
        if(scene_has_command(&p->vm,0x45)) {
            int other=recruit_mode>=1 && recruit_mode<=13?recruit_mode-1:recruit_mode==14?15:character;
            if(load_mouth(&p->mouth,other)){sceneplay_free(p);return -31;}p->mouth_loaded=1;
            if(id==62) {
                char path[80];snprintf(path,sizeof path,"spraymesh%02u.jgc",skater_models[other]);
                if(scene_load_bank(&p->alternate,path)){sceneplay_free(p);return -34;}p->alternate_loaded=1;
            }
            if(scene_has_command(&p->vm,0x4d)) {
                if(audio_load_recruit_cues(other,recruit_cues)){sceneplay_free(p);return -32;}p->recruit_audio_loaded=1;p->recruit_voice_ref=-1;
            }
        }
    }
    if(recruitment) {
        if(p->audio_error){sceneplay_free(p);return -32;}
        if(!font_real_wide_ready()) {
            err=font_real_load_wide("fontwide.bin");
            if(err){sceneplay_free(p);return -33;}p->wide_font_owned=1;
        }
        /* Bind only source/runtime-audited descriptors. Exported mouths
           alone do not enable the remaining scenes. */
        err=load_mouth(&p->mouth,recruit_mode-1);
        if(err){sceneplay_free(p);return -31;}p->mouth_loaded=1;
        if(id==12 || id==17 || id==54 || id==55) {
            err=load_mouth(&p->player_mouth,character);
            if(err){sceneplay_free(p);return -31;}p->player_mouth_loaded=1;
        }
        err=audio_load_recruit_cues(recruit_mode-1,recruit_cues);
        if(err){sceneplay_free(p);return -32;}p->recruit_audio_loaded=1;p->recruit_voice_ref=-1;
    }
    /* 74/102 use only the selected PLAY skater; no substitute actor bank. */
    if(bank) {
        err=chars_load(&p->actors,bank);
        if(err){chars_free(&p->actors);sceneplay_free(p);return -10+err;}p->actors_loaded=1;
    }
    if(scene_has_command(&p->vm,0x5c)) {
        err=chars_load(&p->effects,"scenefx.jgc");
        if(err){sceneplay_free(p);return -1;}p->effects_loaded=1;
        /* Explicit inspection seed, NOT a recovered campaign RNG state. */
        p->inspection_rng=1;p->vm.random15=finalboss_random15;p->vm.random_ctx=&p->inspection_rng;
    }
    scene_tick(&p->vm);if(p->vm.error){sceneplay_free(p);return -30;}
    if(!preload)scene_play(&p->vm);
    printf("SCENE begin id=%d flags=%x phase=%d source_bytes=%u page_bytes=%u pages=%u\n",id,(unsigned)rec[1],p->vm.phase,(unsigned)rec[4],SCENE_PAGE_SIZE,((unsigned)rec[4]+SCENE_PAGE_SIZE-1)>>SCENE_PAGE_SHIFT);
    return 1;
}
int sceneplay_begin(ScenePlay *p,int id,int character,int preload)
{ return begin(p,id,character,preload,0,0); }
int sceneplay_event_begin(ScenePlay *p,int id,int character,int secondary,int preload)
{return scene_bespoke_supported(id)?begin(p,id,character,preload,1,secondary):0;}
int sceneplay_recruit_begin(ScenePlay *p,int id,int character,int mode,int preload)
{return begin(p,id,character,preload,0,mode);}
int sceneplay_inspect(ScenePlay *p,int id,int character)
{ return begin(p,id,character,0,1,0); }
int sceneplay_final_begin(ScenePlay *p,int id,int character,int preload,uint32_t *rng)
{
    if(id!=90 && id!=103)return 0;
    int result=begin(p,id,character,preload,1,0);
    if(result>0 && rng){p->vm.random15=finalboss_random15;p->vm.random_ctx=rng;}
    return result;
}
int sceneplay_status(const ScenePlay *p)
{
    /* VM phase 11 is an authored wait, NOT EVENT task cleanup. The
       original execution-status word stays set across these waits. */
    int phase=p->vm.phase==0x11 ? p->vm.return_phase:p->vm.phase;
    return p->image.pages && phase>=0x12 && phase<0x14 ? p->vm.id:-1;
}
int sceneplay_resident(const ScenePlay *p)
{ return p->image.pages!=NULL; }
static void scene_event_audio_tick(ScenePlay *p,const SceneCamera *camera)
{
    for(unsigned slot=0;slot<EVENT_AUDIO_HANDLES;slot++) {
        unsigned serial,kind,bank,ref;int active;const float *pos;
        if(slot<30) {
            const SceneTypedSound *v=p->vm.typed_sound+slot;const SceneActor *a=p->vm.actors+slot;
            serial=v->serial;kind=v->kind;bank=v->bank;ref=v->reference;active=v->active && a->active;pos=a->pos;
        } else if(slot<60) {
            const SceneObject *o=p->vm.objects+slot-30;
            serial=o->voice_serial;kind=o->voice_kind;bank=o->voice_bank;ref=o->voice_ref;active=o->voice>=0 && o->transform.active;pos=o->transform.pos;
        } else if(slot<92) {
            const struct SceneAuxEffect *e=p->vm.aux+slot-60;
            serial=e->sound_serial?e->serial:0;kind=e->kind;bank=ref=kind==0x5b?0x1000d:0x90005;
            active=e->active && e->sound_serial;pos=e->pos[0];
        } else {
            const SceneEffect *e=p->vm.effects+slot-92;
            serial=e->sound_serial?e->serial:0;kind=0x5c;bank=ref=0xffff0010u;
            active=e->active && e->sound_serial;pos=e->pos;
        }
        unsigned *prior=slot<30?p->typed_serial+slot:slot<60?p->object_serial+slot-30:slot<92?p->aux_serial+slot-60:p->effect_serial+slot-92;
        int *was_active=slot<30?p->typed_active+slot:slot<60?p->object_active+slot-30:slot<92?p->aux_active+slot-60:p->effect_active+slot-92;
        if(serial!=*prior || (!active && *was_active)) {
            int err=0;if(active)err=audio_event_voice(slot,kind,bank,ref);else audio_event_stop(slot);
            if(err)p->audio_error=err;
            *prior=serial;*was_active=active;
            printf("SCENE event sound id=%d clock=%.0f slot=%u kind=%x bank=%u ref=%u active=%d status=%d\n",p->vm.id,p->vm.clock,slot,kind,bank,ref,active,err);
        }
        const EventAudioCue *c=active?audio_event_cue(slot):NULL;
        if(c && camera->active) {
            float dx=pos[0]-camera->pos[0],dy=pos[1]-camera->pos[1],dz=pos[2]-camera->pos[2];
            int direction=player_bam_atan2(dx,dz),listener=player_bam_atan2(camera->forward[0],camera->forward[2]);
            if(direction<0)direction+=65536;
            if(listener<0)listener+=65536;
            int difference=direction-listener,sign=difference>0?-1:difference<0?1:0;if(difference<0)difference=-difference;
            SourceAudioContext context={.mode=2,.bank=c->logical_bank,.program=c->program};
            SourceAudioLevels levels=source_audio_levels(&c->profile,&context,sqrtf((dx*dx+dy*dy)+dz*dz),difference,sign);
            audio_event_levels(slot,levels.volume,levels.pan);
        }
    }
}
int sceneplay_tick(ScenePlay *p,Player *player,float dt)
{
    /* 04f3d6 publishes status -1 at cleanup, but the EVENT (task 956)
       survives until its next phase-14 callback. Do not conflate these:
       the final controller may reposition now but must not preload yet. */
    if(p->image.pages && p->vm.phase==0x14 && p->transferred) {
        p->remainder+=dt;
        /* A 60 Hz display can reach this half a source tick before the
           retirement callback. Keep the EVENT frame until its allocations
           are gone; thawing here let the HUD allocate into the EVENT peak. */
        if(p->remainder<1.f/30.f)return 1;
        sceneplay_free(p);return 0;
    }
    if(!scene_busy(&p->vm)||p->vm.phase==0xf)return 0;
    p->remainder+=dt;
    while(p->remainder>=1.f/30.f&&scene_busy(&p->vm)) {
        /* 0b064c samples the previously published 020580 camera before
           this tick's EVENT camera child runs. The sound object position
           is current; using the new camera shifts the pan transition. */
        SceneCamera listener=p->vm.camera;
        p->remainder-=1.f/30.f;scene_tick(&p->vm);
        if(!listener.active)listener=p->vm.camera;
        if(p->event_audio_loaded)scene_event_audio_tick(p,&listener);
        if(p->vm.error){if(p->audio_loaded)audio_scene_voice(-1);if(p->recruit_audio_loaded)audio_recruit_voice(-1);if(p->object_audio_loaded)audio_final_voice(-1);if(p->object_bank_loaded)for(unsigned i=0;i<30;i++)audio_final_object(i,-1);printf("SCENE unsupported pc=%08x\n",(unsigned)p->vm.pc);return 0;}
        if(p->audio_loaded && (p->vm.voice_serial!=p->voice_serial || (p->vm.voice_ref<0 && p->voice_ref>=0))) {
            int err=audio_scene_voice(p->vm.voice_ref);
            if(err)p->audio_error=err;
            p->voice_serial=p->vm.voice_serial;p->voice_ref=p->vm.voice_ref;
            printf("SCENE sound clock=%.0f ref=%d remaining=%d status=%d\n",p->vm.clock,p->vm.voice_ref,p->vm.voice_remaining,err);
        }
        if(p->recruit_audio_loaded) {
            const SceneRecruit *r=&p->vm.recruit;
            if(r->voice_serial!=p->recruit_voice_serial || (r->voice_ref<0 && p->recruit_voice_ref>=0)) {
                int err=audio_recruit_voice(r->voice_ref);if(err)p->audio_error=err;
                p->recruit_voice_serial=r->voice_serial;p->recruit_voice_ref=r->voice_ref;
                printf("SCENE recruit sound clock=%.0f ref=%d status=%d\n",p->vm.clock,r->voice_ref,err);
            }
        }
        if(p->object_audio_loaded) {
            const SceneObject *o=p->vm.objects;
            int active=o->transform.active && o->voice>=0;
            if(active && o->voice_ref==2 && p->vm.camera.active) {
                const SceneCamera *camera=&listener;
                float dx=o->transform.pos[0]-camera->pos[0],dy=o->transform.pos[1]-camera->pos[1],dz=o->transform.pos[2]-camera->pos[2];
                /* 02d2ca/0b084a put Z in FR5 and X in FR4; 103df0
                   is atan2(FR4,FR5), i.e. atan2(X,Z). */
                int direction=player_bam_atan2(dx,dz),listener=player_bam_atan2(camera->forward[0],camera->forward[2]);
                if(direction<0)direction+=65536;
            if(listener<0)listener+=65536;
                int difference=direction-listener,sign=difference>0?-1:difference<0?1:0;
                if(difference<0)difference=-difference;
                const SourceAudioProfile profile={2,1000.f,.5f,.05f};
                const SourceAudioContext context={.mode=2,.bank=29,.program=2};
                SourceAudioLevels levels=source_audio_levels(&profile,&context,sqrtf((dx*dx+dy*dy)+dz*dz),difference,sign);
                audio_final_voice_levels(levels.volume,levels.pan);
            }
            if(o->voice_serial!=p->object_voice_serial || (!active && p->object_voice_active)) {
                int err=audio_final_voice(active ? o->voice_ref:-1);
                if(err)p->audio_error=err;
                p->object_voice_serial=o->voice_serial;p->object_voice_active=active;
                printf("SCENE object sound id=%d clock=%.0f slot=0 ref=%d remaining=%d status=%d\n",p->vm.id,p->vm.clock,active ? o->voice_ref:-1,o->voice_remaining,err);
            }
        }
        if(p->object_bank_loaded)for(unsigned slot=0;slot<30;slot++) {
            const SceneObject *o=p->vm.objects+slot;int active=o->transform.active && o->voice>=0;
            if(active && o->voice_ref==6 && p->vm.camera.active) {
                /* 0b064c listener = published camera position/direction;
                   02d140 computes source-minus-listener, atan2(X,Z), and
                   an unwrapped signed difference. 0e6ee0 sees EVENT mode 2.
                   No camera-to-player substitution or linear gain curve. */
                const SceneCamera *camera=&listener;
                float dx=o->transform.pos[0]-camera->pos[0],dy=o->transform.pos[1]-camera->pos[1],dz=o->transform.pos[2]-camera->pos[2];
                int direction=player_bam_atan2(dx,dz),listener=player_bam_atan2(camera->forward[0],camera->forward[2]);
                if(direction<0)direction+=65536;
            if(listener<0)listener+=65536;
                int difference=direction-listener,sign=difference>0?-1:difference<0?1:0;
                if(difference<0)difference=-difference;
                const SourceAudioProfile profile={0,1000.f,.5f,.05f};
                const SourceAudioContext context={.mode=2,.bank=3,.program=1};
                SourceAudioLevels levels=source_audio_levels(&profile,&context,sqrtf((dx*dx+dy*dy)+dz*dz),difference,sign);
                audio_final_object_levels(slot,levels.volume,levels.pan);
            }
            if(o->voice_serial!=p->object_serial[slot] || (!active && p->object_active[slot])) {
                int err=audio_final_object(slot,active ? o->voice_ref:-1);
                if(err)p->audio_error=err;
                p->object_serial[slot]=o->voice_serial;p->object_active[slot]=active;
                printf("SCENE object sound id=%d clock=%.0f slot=%u ref=%d remaining=%d status=%d\n",p->vm.id,p->vm.clock,slot,active ? o->voice_ref:-1,o->voice_remaining,err);
            }
        }
    }
    if(p->vm.phase==0x14&&!p->transferred) {
        if(p->vm.handoff&1)player_scene_handoff(player,p->vm.result_player.pos,p->vm.result_player.angle,p->vm.result_player.motion,p->vm.result_player.frame);
        if(p->vm.handoff&2) {
            player->cam_ex=p->vm.result_camera.pos[0];player->cam_ez=p->vm.result_camera.pos[2];
            player->cam_yaw=atan2f(player->x-player->cam_ex,player->z-player->cam_ez);player->cam_init=1;
        }
        p->transferred=1;
        printf("SCENE handoff id=%d clock=%.0f pos=%.9g,%.9g,%.9g yaw=%.9g eye=%.9g,%.9g,%.9g\n",p->vm.id,p->vm.clock,player->x,player->y,player->z,player->yaw,p->vm.result_camera.pos[0],p->vm.result_camera.pos[1],p->vm.result_camera.pos[2]);
    }
    return 1; /* freeze gameplay on the transfer tick, too */
}
void sceneplay_draw(ScenePlay *p,Chars *skaters)
{
    Scene *s=&p->vm;float m[16];
    if(s->id==4 && p->actors_loaded) {
        SceneObject *o=s->objects;SceneActor *a=&o->transform;
        if(a->active) {
            int motion=a->raw_motion==0x8c90c89cu ? 0:a->raw_motion==0x8c903ff8u ? 1:-1;
            if(motion<0 || o->root!=0x8c91aeb8u || o->texlist!=0x8c900174u || o->flags!=2){s->error=1;return;}
            player_scene_matrix(a->pos,a->angle,a->scale,m);
            chars_draw_one_xf(&p->actors,0,m,motion,a->frame);
        }
    }
    if(s->id==7 && p->actors_loaded)for(unsigned slot=0;slot<30;slot++) {
        SceneObject *o=&s->objects[slot];SceneActor *a=&o->transform;
        if(!a->active)continue;
        unsigned model=0,motion=0;
        while(model<9 && (INTRO7_MODELS[model].root!=o->root || INTRO7_MODELS[model].texlist!=o->texlist))model++;
        while(motion<2 && INTRO7_MOTIONS[motion]!=a->raw_motion)motion++;
        if(model==9 || motion==2 || o->flags!=2){s->error=1;return;}
        player_scene_matrix(a->pos,a->angle,a->scale,m);
        chars_draw_one_xf(&p->actors,model,m,motion,a->frame);
    }
    if(s->id==90 && p->actors_loaded) {
        static const unsigned slots[4]={0,3,1,2};CharPose poses[128];
        for(unsigned model=0;model<4;model++) {
            SceneObject *o=&s->objects[slots[model]];SceneActor *a=&o->transform;
            if(!a->active)continue;
            const JgcChar *ch=p->actors.chars+model;
            if(ch->nbones>128){s->error=1;return;}
            for(unsigned bone=0;bone<ch->nbones;bone++) {
                chars_sample_pose(&p->actors,ch,-1,0,bone,poses+bone);
                if(!scene_sample_njs(s,o->njs_motion,a->frame,bone,(o->flags&2)!=0,poses+bone)) {
                    printf("SCENE NJS unsupported model=%u bone=%u frame=%g\n",model,bone,a->frame);s->error=1;return;
                }
            }
            player_scene_matrix(a->pos,a->angle,a->scale,m);
            if(o->flags&2)chars_draw_one_pose(&p->actors,model,m,poses,-1,0,0);
            else chars_draw_material(&p->actors,model,m,poses,0,0,1,NULL);
        }
    }
    if(s->id==103 && p->actors_loaded)for(int i=0;i<2;i++) {
        SceneObject *o=&s->objects[i];SceneActor *a=&o->transform;if(!a->active)continue;
        player_scene_matrix(a->pos,a->angle,a->scale,m);
        /* Both original command flags are 2: 0cd1c4 selects mode 0.
           Their source meshes/motion share the same 302-frame CNK header. */
        chars_draw_one_xf(&p->actors,i,m,0,a->frame);
    }
    if(p->generic && !scene_draw_models(p)){s->error=1;return;}
    if(p->generic)scene_draw_aux(p);
    /* Opcode 5c is shared by Explosion and other EVENTs, not just scene 90. */
    if(p->effects_loaded)for(unsigned i=0;i<16;i++) {
        const SceneEffect *e=s->effects+i;if(!e->active)continue;
        int32_t angle[3]={0,e->yaw,0};float scale[3]={e->scale,e->scale,e->scale};
        player_scene_matrix(e->pos,angle,scale,m);
        for(unsigned k=0;k<2;k++)chars_draw_material(&p->effects,(e->type==0 ? 2:0)+k,m,NULL,0,e->frame[k],1.f+e->alpha_offset[k],NULL);
    }
    for(int i=0;i<30;i++) {
        SceneActor *a=&s->actors[i];if(!a->active||!p->actors_loaded)continue;
        player_scene_matrix(a->pos,a->angle,a->scale,m);
        chars_draw_one_xf(&p->actors,0,m,0,a->frame);
    }
    if(s->recruit.actor.active && skaters && p->mouth_loaded) {
        SceneRecruit *r=&s->recruit;SceneActor *a=&r->actor;CharPose poses[64];float mouth[16];
        int model=skater_models[r->character];
        player_scene_matrix(a->pos,a->angle,a->scale,m);
        if(!scene_event_skater_pose(s,a,skaters,model,poses) || !scene_mouth_matrix(skaters,model,poses,m,mouth,player_fsca_sin)){s->error=1;return;}
        if(a->variant && p->alternate_loaded)chars_draw_one_pose(&p->alternate,0,m,poses,-1,0,0);
        else chars_draw_one_pose(skaters,model,m,poses,-1,0,0);
        chars_draw_one_xf(&p->mouth,0,mouth,r->companion_mode,r->companion_frame);
    }
    SceneActor *a=&s->player;if(!a->active||!skaters)return;
    /* Match the identity used to load this EVENT's primary voice/mouth.
       A demonstration may have a different current gameplay character. */
    int model=skater_models[s->character];
    player_scene_matrix(a->pos,a->angle,a->scale,m);
    if(p->player_mouth_loaded) {
        CharPose poses[64];float mouth[16];
        if(!scene_event_skater_pose(s,a,skaters,model,poses) || !scene_mouth_matrix(skaters,model,poses,m,mouth,player_fsca_sin)){s->error=1;return;}
        chars_draw_one_pose(skaters,model,m,poses,-1,0,0);
        chars_draw_one_xf(&p->player_mouth,0,mouth,s->player_mouth_mode,s->player_mouth_frame);
        return;
    }
    if(a->mode==3&&a->blend>0)
        chars_draw_one_xf_blend(skaters,model,m,a->old_motion,a->old_frame,a->motion,0.f,a->frame/a->blend,-1,0.f,0.f);
    else chars_draw_one_xf(skaters,model,m,a->motion,a->frame);
}
static void draw_glyphs(const SceneGlyph *glyph,unsigned count)
{
    for(unsigned i=0;i<count;) {
        unsigned first=i;char line[1025];int n=0;uint32_t c=glyph[i].argb;
        do {
            unsigned code=glyph[i++].character;
            if(code>255)line[n++]=code>>8;line[n++]=code&255;
        }while(i<count && glyph[i].y==glyph[first].y && glyph[i].argb==c &&
               glyph[i].x==glyph[i-1].x+(glyph[i-1].character>255 ? 24:12));
        line[n]=0;unsigned colour=GCOLOR((c>>16)&255,(c>>8)&255,c&255,c>>24);
        font_real_draw_xy(glyph[first].x*.75f,glyph[first].y*(272.f/480.f),.75f,272.f/480.f,colour,line);
    }
}
void sceneplay_overlay(const ScenePlay *p)
{
    const Scene *s=&p->vm;
    if(s->colour_active) {
        unsigned hi=s->colour_max,lo=s->colour_min,c=s->colour_overlay?s->overlay_argb:0;
        gfx_colour_clamp(GCOLOR(hi>>16&255,hi>>8&255,hi&255,255),
                         GCOLOR(lo>>16&255,lo>>8&255,lo&255,255),
                         GCOLOR(c>>16&255,c>>8&255,c&255,c>>24));
    }
    /* 026dc8 defaults are asymmetric: 70 pixels above, 90 below. */
    float top=s->letterbox*70.f*(272.f/480.f),bottom=s->letterbox*90.f*(272.f/480.f);
    if(top>0){gfx_quad(0,0,480,top,GCOLOR(0,0,0,255));gfx_quad(0,272-bottom,480,bottom,GCOLOR(0,0,0,255));}
    SceneGlyph glyph[512];unsigned count=scene_dialog_glyphs(s,glyph,512);draw_glyphs(glyph,count);
    count=scene_caption_glyphs(s,glyph,512);draw_glyphs(glyph,count);
}
