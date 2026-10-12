#include "diagnostics.h"
#include "finalarena.h"
#include "player_body.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pspgu.h>
#ifdef __PSP__
#include <pspkernel.h>
#endif
static FinalArenaProfile final_profile;
static unsigned final_clock(void)
{
#if !JGR_DIAGNOSTICS
    return 0;
#elif defined(FINALARENA_PROFILE_CLOCK)
    return FINALARENA_PROFILE_CLOCK();
#elif defined(__PSP__)
    return sceKernelGetSystemTimeLow();
#else
    return 0;
#endif
}
void finalarena_profile_read(FinalArenaProfile *out)
{*out=final_profile;memset(&final_profile,0,sizeof final_profile);}
static const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static int load_collision(FinalCol *c,unsigned char **image,const char *name)
{
    FILE *f=fopen(name,"rb");
    if(!f)return -1;
    if(fseek(f,0,SEEK_END)){fclose(f);return -2;}
    long n=ftell(f);rewind(f);
    if(n<40 || n>0x400000){fclose(f);return -3;}
    *image=malloc(n);
    if(!*image || fread(*image,1,n,f)!=(unsigned)n){fclose(f);return -4;}
    fclose(f);return finalcol_init(c,*image,n);
}
static int surface(void *context,int triangle,const float *point,float *delta,int *yaw)
{
    FinalArena *a=context;
    if(triangle>=0 && triangle<a->collider.nworld) {
        unsigned node=a->collider.world[triangle].src;
        if(node<a->collider.h->nodes && a->collider.nodes[node].piece==4)
            finalprogress_contact(&a->progression,a->phase,a->cinematic,1);
    }
    return triangle<a->collider.nworld ? finalcol_surface(&a->collider,triangle,point,delta,yaw) :
        finalcol_surface(&a->boss_collider,triangle-a->collider.nworld,point,delta,yaw);
}
static int update_world(FinalArena *a,Collision *world,int measured)
{
    float bossframes[6]={a->boss.orbit,0,0,0,0,0};
    float frames[6],collapse[4];memcpy(frames,a->frames,sizeof frames);
    unsigned active=a->phase>4 ? 0x2f:0x1f,mask=0;
    for(int i=0;i<4;i++) {
        FinalPiece *p=a->pieces+i;
        if(p->deleted)active&=~(1u<<i);
        frames[i]=p->collision_collapsing ? p->collision_old_frame:p->collision_frame;
        collapse[i]=p->collision_collapse_frame;if(p->collision_collapsing)mask|=1u<<i;
    }
    unsigned started=measured ? final_clock():0;
    int n=finalcol_sample_overrides(&a->collider,frames,active,collapse,mask);
    int b=n<0 ? 0:finalcol_sample_world(&a->boss_collider,bossframes,a->phase>4 ? 2:1,
                                      a->phase>4 ? a->boss_world:NULL);
    if(measured){final_profile.collision_samples++;final_profile.collision_us+=final_clock()-started;}
    if(n<0)return n;
    if(b<0)return b;
    if(measured)started=final_clock();
    /* Preserve the arena-then-boss logical contact order while publishing
       each bank directly. Shared source vertices supply grid bounds/cells;
       Collision still owns the contiguous triangles used by all queries. */
    const ColWorldIndexedPart parts[2]={
        {a->collider.world,a->collider.vertices,a->collider.world_indices,
         (unsigned)n,a->collider.h->vertices,a->collider.active},
        {a->boss_collider.world,a->boss_collider.vertices,a->boss_collider.world_indices,
         (unsigned)b,a->boss_collider.h->vertices,a->boss_collider.active}
    };
    const ColWorldStability stable[2]={
        {a->collider.unchanged_triangles,a->collider.unchanged_vertices,a->collider.sample_generation},
        {a->boss_collider.unchanged_triangles,a->boss_collider.unchanged_vertices,a->boss_collider.sample_generation}
    };
    int result=col_set_world_indexed_stable(world,parts,2,stable);
    if(measured){final_profile.world_publishes++;final_profile.world_triangles+=(unsigned)(n+b);
        final_profile.publish_us+=final_clock()-started;}
    return result;
}
void finalarena_free(FinalArena *a)
{
    if(a->boss_voice)a->boss_voice(-2,NULL);
    if(a->attack_sound){a->attack_sound(a->attack_sound_context,0,-1);a->attack_sound(a->attack_sound_context,1,-1);}
    chars_free(&a->emitter_models);
    chars_free(&a->models);chars_free(&a->actors);chars_free(&a->hazard_models);finalcol_free(&a->collider);finalcol_free(&a->boss_collider);
    free(a->image);free(a->boss_image);free(a->collapse_image);free(a->tag_image);memset(a,0,sizeof *a);
}
static int pose_tags(FinalArena *a)
{
    if(!a->graffiti)return 0;
    for(unsigned i=0;i<5;i++) {
        Spot *s=a->graffiti->spots+i;float root[16],column[16];
        if(i<4) {
            FinalPiece *p=a->pieces+i;s->active=!p->deleted;
            if(!s->active)continue;
            if(finalboss_platform_attachment(&a->models,i,p->collapsing ? p->old_frame:p->frame,
                p->collapsing ? p->collapse_frame:-1,identity,root))return -1;
        } else {
            s->active=a->phase>=5;if(!s->active)continue;
            memcpy(root,a->boss_world,64);
        }
        for(int r=0;r<4;r++)for(int c=0;c<4;c++)column[c*4+r]=root[r*4+c];
        finaltags_pose(&a->tags,i,column,s);s->attached=1;
        /* 0e9cfc registers launcher attachment point 0 only in attack/
           wind-down states. It is the subject of an authored spray shot. */
        s->camera_candidates=a->hazard.attachment;
        s->camera_ncandidate=!a->hazard.deleted && (a->hazard.state==4 || a->hazard.state==5);
        /* 0c4ab4 stores GF+c0 = parent * descriptor TRS, not just the
           attachment matrix at GF+100. 01eb80 retains this camera basis. */
        if(!finaltags_camera_reuse(&a->tags,i,root,s->camera_matrix)) {
            const unsigned char *d=a->tags.objects[i].tpl->descriptor;
            float pos[3],scale[3],local[16];int32_t angles[3];
            memcpy(pos,d,12);memcpy(angles,d+12,12);memcpy(scale,d+24,12);
            player_scene_matrix(pos,angles,scale,local);
            for(int r=0;r<4;r++)for(int c=0;c<4;c++) {
                double v=0;for(int k=0;k<4;k++)v+=(double)root[r*4+k]*local[k*4+c];s->camera_matrix[r*4+c]=(float)v;
            }
            finaltags_camera_store(&a->tags,i,root,s->camera_matrix);
        }
    }
    return 0;
}
int finalarena_bind_tags(FinalArena *a,Graffiti *g)
{
    if(!a->loaded || !g || a->graffiti)return -1;
    FILE *f=fopen("finalgraffiti.bin","rb");
    if(!f)return -1;
    long n=0;int err=-1;
    if(!fseek(f,0,SEEK_END) && (n=ftell(f))>=8 && n<=0x100000) {
        rewind(f);a->tag_image=malloc(n);
        if(a->tag_image && fread(a->tag_image,1,n,f)==(unsigned)n)err=finaltags_init(&a->tags,a->tag_image,n);
    }
    fclose(f);
    if(err){free(a->tag_image);a->tag_image=NULL;return -1;}
    Spot *spots=calloc(5,sizeof *spots);
    if(!spots){free(a->tag_image);a->tag_image=NULL;memset(&a->tags,0,sizeof a->tags);return -1;}
    if(g->spots)for(int i=0;i<g->n;i++)free(g->spots[i].source);
    free(g->spots);g->spots=spots;g->n=5;g->near=-1;a->graffiti=g;a->scene_status=-1;
    return pose_tags(a);
}
int finalarena_progress(FinalArena *a)
{
    if(a->error){a->progression_error=1;return -1;}
    if(!a->loaded || !a->scene_begin || a->progression_error)return 0;
    FinalProgress before=a->progression;int phase=a->phase;
    FinalProgressEvents e=finalprogress_tick(&a->progression,&a->phase,a->scene_status,a->event_alive);
    int id=e.play>=0 ? e.play:e.preload;
    if(id>=0 && a->scene_begin(a->scene_context,id,e.play<0)<=0) {
        a->progression=before;a->phase=phase;a->progression_error=1;return -1;
    }
    if(e.remove_environment) {
        /* 0e98b4 child deletion retires its sound owners even between
           the wind-up cue and projectile creation. */
        a->hazard.deleted=1;a->emitter_alive=0;a->emitter_request=0;
        memset(&a->emitter_contact,0,sizeof a->emitter_contact);
        if(a->attack_sound){a->attack_sound(a->attack_sound_context,0,-1);a->attack_sound(a->attack_sound_context,1,-1);}
    }
    if(e.reposition && a->player) {
        
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_finalarena_c__position_0.inc"
#else

#include "generated/source_data/finalarena_c__position_0.inc"

#endif

        player_reposition(a->player,position);
        a->player->yaw=0; /* source angle 1d0f28 = (0,32768,0) */
        player_cam_reset(a->player);
    }
    if(e.clear)a->clear_requested=1;
    return 0;
}
int finalarena_reset(FinalArena *a)
{
    if(!a->loaded)return -1;
    if(a->boss_voice)a->boss_voice(-2,NULL);
    a->error=a->phase=a->cinematic=a->tagged=0;
    if(a->attack_sound){a->attack_sound(a->attack_sound_context,0,-1);a->attack_sound(a->attack_sound_context,1,-1);}
    finalemitter_init(&a->emitter);memset(&a->emitter_contact,0,sizeof a->emitter_contact);
    a->emitter_alive=0;a->emitter_request=a->emitter_serial=a->fire_contacts=0;a->emitter_radius=3;
    if(a->player){a->player->fire_pending=a->player->burn_ticks=a->player->damage_invincible=0;a->player->fire_hits=0;}
    a->progression=(FinalProgress){0};a->scene_status=-1;
    a->event_alive=a->clear_requested=a->progression_error=0;
    a->progress_remainder=a->remainder=a->background=0;
    memset(a->frames,0,sizeof a->frames);memset(a->pieces,0,sizeof a->pieces);
    memset(&a->fall,0,sizeof a->fall);memcpy(a->boss_world,identity,64);
    finalboss_init(&a->boss,&a->actors);a->boss_rng=1;
    finalhazard_init(&a->hazard);if(finalhazard_pose(&a->hazard,&a->hazard_models))return a->error=-9;
    if(a->graffiti){graffiti_reset(a->graffiti);for(unsigned i=0;i<5;i++)finaltags_progress(&a->tags,i,a->graffiti->spots+i);}
    if(pose_tags(a))return a->error=-8;
    /* Fresh stage tasks have no previous registration. Do not carry a
       retrying skater by the displacement from the ending's last pose. */
    a->collider.nworld=a->boss_collider.nworld=0;
    return a->error=update_world(a,a->world,0);
}
int finalarena_load(FinalArena *a,Collision *world)
{
    memset(a,0,sizeof *a);
    memcpy(a->boss_world,identity,sizeof identity);
    int err=chars_load(&a->models,"finalscene.jgc");
    if(err)return err;
    err=chars_load(&a->actors,"finalactors.jgc");
    if(err || a->actors.h->n_chars!=8 || a->actors.h->n_tex!=65){finalarena_free(a);return -5;}
    finalboss_init(&a->boss,&a->actors);
    err=chars_load(&a->hazard_models,"finalhazard.jgc");
    if(err || a->hazard_models.h->n_chars!=2){finalarena_free(a);return -9;}
    finalhazard_init(&a->hazard);
    if(finalhazard_pose(&a->hazard,&a->hazard_models)){finalarena_free(a);return -9;}
    err=chars_load(&a->emitter_models,"finalemitter.jgc");
    if(err || a->emitter_models.h->n_chars!=8 || a->emitter_models.h->n_tex!=8){finalarena_free(a);return -10;}
    a->emitter_radius=3;
    /* No recovered campaign-wide RNG handoff yet.
       The generator and callback threshold are original; the global draw
       order across other game tasks must not be claimed frame-identical. */
    a->boss_rng=1;
    err=load_collision(&a->collider,&a->image,"finalcoll.bin");
    if(!err) {
        FILE *f=fopen("finalcollapse.bin","rb");
        if(!f)err=-6;
        else {
            long n=0;if(fseek(f,0,SEEK_END) || (n=ftell(f))<24 || n>0x100000)err=-6;
            else {rewind(f);a->collapse_image=malloc(n);if(!a->collapse_image || fread(a->collapse_image,1,n,f)!=(unsigned)n)err=-6;
                else err=finalcol_bind_overrides(&a->collider,a->collapse_image,n);}
            fclose(f);
        }
    }
    if(!err)err=load_collision(&a->boss_collider,&a->boss_image,"finalbosscoll.bin");
    if(!err)err=update_world(a,world,0);
    if(err){finalarena_free(a);return err;}
    /* Exact source-input sharing is optional: allocation failure retains
       the ordinary renderer and must not prevent entering the level. */
    chars_final_indexed_prepare(&a->actors);
    chars_final_indexed_prepare(&a->hazard_models);
    chars_final_indexed_prepare(&a->models);
    world->surface_motion=surface;world->surface_context=a;
    a->world=world;a->loaded=1;return 0;
}
static int update_impl(FinalArena *a,Collision *world,float dt,int cinematic)
{
    if(!a->loaded || a->error || !isfinite(dt) || dt<0)return -1;
    /* 0e9368: peripheral frame += 1; 0ed42c: centre += phase table
       1df2f0. 0e9368's collapse is a channel override, NOT a crossfade.
       Completion is copied from the source-shaped graffiti tasks; the
       explicit inspection API remains separate from input-driven progress. */
    if((unsigned)a->phase>=9)return -2;
    a->cinematic=cinematic;
    a->remainder+=dt;int ticks=0;
    while(a->remainder>=1.f/30.f) {
        a->remainder-=1.f/30.f;ticks++;
        if(cinematic)final_profile.scene_ticks++;else final_profile.ticks++;
        if(!a->hazard.deleted) {
            unsigned voice=a->hazard.voice_serial,projectile=a->hazard.projectile_serial;
            a->hazard.request=a->player ? finalhazard_select(a->player->x,a->player->z):4;
            finalhazard_tick(&a->hazard);
            if(finalhazard_pose(&a->hazard,&a->hazard_models))return a->error=-9;
            if(voice!=a->hazard.voice_serial && a->attack_sound && a->attack_sound(a->attack_sound_context,0,1))return a->error=-11;
            if(projectile!=a->hazard.projectile_serial) {
                /* 600-frame attacks leave enough time for the 133-tick
                   emitter and its original shrink/delete tail. */
                if(a->emitter_alive)return a->error=-12;
                finalemitter_init(&a->emitter);memset(&a->emitter_contact,0,sizeof a->emitter_contact);
                a->emitter_alive=1;a->emitter_serial++;
                if(a->attack_sound && a->attack_sound(a->attack_sound_context,1,0))return a->error=-11;
            }
        }
        a->emitter_request=0;
        if(a->hazard.deleted && a->emitter_alive) {
            a->emitter_alive=0;
            if(a->attack_sound){a->attack_sound(a->attack_sound_context,0,-1);a->attack_sound(a->attack_sound_context,1,-1);}
        }
        if(a->emitter_alive) {
            a->emitter_request=finalemitter_step(&a->emitter,a->hazard.attachment,133,&a->emitter_contact,&a->boss_rng);
            memset(&a->emitter_contact,0,sizeof a->emitter_contact);
            if((a->emitter_request&FINAL_EMITTER_STOP_VOICE) && a->attack_sound)a->attack_sound(a->attack_sound_context,1,-1);
            if(a->emitter_request&FINAL_EMITTER_DELETE)a->emitter_alive=0;
            if(a->emitter_request&FINAL_EMITTER_LINE) {
                ColHit h;
                if(col_probe(world,a->hazard.attachment,a->emitter.position[a->emitter.last],3,&h)) {
                    a->emitter_contact.line=1;memcpy(a->emitter_contact.point,h.p,12);
                }
            }
        }
        if(a->graffiti)for(unsigned i=0;i<5;i++) {
            finaltags_progress(&a->tags,i,a->graffiti->spots+i);
            if(i<4 && a->tags.objects[i].progress.complete)a->tagged|=1u<<i;
        }
        for(int i=0;i<4;i++) {
            a->phase+=finalpiece_tick(a->pieces+i,!!(a->tagged&(1u<<i)),cinematic,a->phase);
            a->frames[i]=a->pieces[i].frame;
        }
        a->frames[4]+=finalprogress_rate(a->phase);if(a->frames[4]>=500.f)a->frames[4]-=500.f;
        a->frames[5]=a->frames[4];a->background+=1.f;if(a->background>=500.f)a->background=0;
        /* 0ed42c writes 2fcc50 only outside EVENT execution. 0ecd0c
           and 0ed598 share that retained bone-63 matrix. */
        if(a->phase>4 && !cinematic && finalboss_central_attachment(&a->models,a->frames[4],identity,a->boss_world))
            return a->error=-7;
        finalboss_clocks(&a->boss,&a->actors,a->phase,cinematic);
        if(!cinematic) {
            /* 1de928 is Goji's local voice origin, carried by the final
               platform in 0ecd0c. Keep the original retained-voice clock. */
            float pos[3]={0,14.390000343322754f,0};
            if(a->phase>4)for(unsigned i=0;i<3;i++)pos[i]=a->boss_world[i*4+1]*14.390000343322754f+a->boss_world[i*4+3];
            int playing=a->boss.voice_active && a->boss_voice ? a->boss_voice(-1,pos):0;
            int ref=finalboss_voice(&a->boss,a->phase,playing,finalboss_random15,&a->boss_rng);
            if(ref>=0)a->boss.voice_active=a->boss_voice && a->boss_voice(ref,pos)>0;
        }
        finalboss_animation(&a->boss,&a->actors,finalboss_random15,&a->boss_rng);
        if(!cinematic) {
            unsigned started=final_clock();int error=pose_tags(a);
            final_profile.tag_prepares++;final_profile.tags_us+=final_clock()-started;
            if(error)return a->error=-8;
            if(a->graffiti)finalprogress_boss_tag(&a->phase,0,a->tags.objects[4].progress.complete);
        }
    }
    /* 0e9368/0ed42c/0ecbd8: clocks keep running in an EVENT, but
       collision registration and its carry history do not advance. */
    if(ticks && !cinematic)a->error=update_world(a,world,1);
    return a->error;
}
static int update(FinalArena *a,Collision *world,float dt,int cinematic)
{
    if(!a->loaded)return update_impl(a,world,dt,cinematic);
    unsigned collision=final_profile.collision_us,publish=final_profile.publish_us,tags=final_profile.tags_us;
    unsigned started=final_clock();int result=update_impl(a,world,dt,cinematic);
    unsigned elapsed=final_clock()-started;
    if(cinematic){final_profile.scene_updates++;final_profile.scene_us+=elapsed;}
    else {final_profile.updates++;final_profile.update_us+=elapsed;}
    final_profile.other_us+=elapsed-(final_profile.collision_us-collision)-
        (final_profile.publish_us-publish)-(final_profile.tags_us-tags);
    return result;
}
int finalarena_update(FinalArena *a,Collision *world,float dt)
{ return update(a,world,dt,0); }
int finalarena_scene_update(FinalArena *a,float dt)
{ return update(a,a->world,dt,1); }
int finalarena_inspect_tag(FinalArena *a,unsigned piece)
{ if(!a->loaded || piece>=4 || a->pieces[piece].deleted)return -1;a->tagged|=1u<<piece;return 0; }
int finalarena_inspect_finalform(FinalArena *a)
{
    if(!a->loaded || a->error || a->phase!=4)return -1;
    for(int i=0;i<4;i++)if(!a->pieces[i].deleted)return -1;
    a->phase=5;return 0;
}
int finalarena_inspect_scene(FinalArena *a,int scene)
{
    if(!a->loaded || a->error || a->phase!=0 || scene!=103)return -1;
    for(unsigned i=0;i<4;i++)if(finalarena_inspect_tag(a,i))return -1;
    /* No invented collapse poses: run the recovered 91+640 tick handlers.
       This selects a valid late layout, not an exact campaign entry clock. */
    for(int i=0;i<750;i++)if(finalarena_update(a,a->world,1.f/30.f))return -1;
    if(finalarena_inspect_finalform(a))return -1;
    return finalarena_update(a,a->world,1.f/30.f);
}
int finalarena_tick(void *context)
{ FinalArena *a=context;return finalarena_update(a,a->world,1.f/30.f); }
void finalarena_after_tick(void *context)
{
    FinalArena *a=context;unsigned started=final_clock();
    /* Collision registration produces contacts for the following task
       update; invulnerability gates RESPONSE, never flame truncation. */
    if(a->player && !a->cinematic && a->emitter_alive && (a->emitter_request&FINAL_EMITTER_REGISTER)) {
        float bodies[20][4];int n=player_body_spheres(a->player,bodies);
        if(n>0)for(int j=a->emitter.last;j>=a->emitter.first;j--) {
            float sphere[4];memcpy(sphere,a->emitter.position[j],12);sphere[3]=a->emitter_radius;
            for(int k=0;k<n;k++)if(bodies[k][3]>0 && player_body_sphere_overlap(bodies[k],sphere)) {
                a->emitter_contact.spheres|=1u<<j;a->player->fire_pending=1;a->fire_contacts++;break;
            }
        }
    }
    if(a->player && finalfall_stage(&a->fall,a->player->y,0)) {
        
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_finalarena_c__position_1.inc"
#else

#include "generated/source_data/finalarena_c__position_1.inc"

#endif
 /* source 1d0f10 */
        player_reposition(a->player,position);
    }
    finalfall_fade(&a->fall);
    final_profile.after_ticks++;final_profile.after_us+=final_clock()-started;
}
static void draw_fire(void *context,int model,const float root[16],float alpha,unsigned texture)
{
    FinalArena *a=context;unsigned short remap[8];for(int i=0;i<8;i++)remap[i]=texture;
    chars_draw_material(&a->emitter_models,model,root,NULL,0,0,alpha,remap);
}
void finalarena_draw_fire(FinalArena *a,const float forward[3])
{
    if(!a->loaded || !a->emitter_alive)return;
    unsigned started=final_clock();
    finalemitter_render(&a->emitter,forward,draw_fire,a);
    a->emitter_radius=finalemitter_draw_radius(&a->emitter);
    final_profile.fire_calls++;final_profile.fire_us+=final_clock()-started;
}
void finalarena_draw(FinalArena *a)
{
    if(!a->loaded)return;
    unsigned started=final_clock(),part=started,now;
    finalhazard_draw(&a->hazard,&a->hazard_models);
    now=final_clock();final_profile.hazard_us+=now-part;part=now;
    /* 0e5470 background, then 0e975c / 0ed598 animated platform roots.
       Mode-1 meshes retain their own alpha/blend/cull state; the sky's
       strip bit 0 must not route it through the character outline pass. */
    chars_draw_effect(&a->models,5,identity,a->background,1.f);
    now=final_clock();final_profile.background_us+=now-part;part=now;
    for(int i=0;i<4;i++) {
        FinalPiece *p=a->pieces+i;if(p->deleted)continue;
        if(p->collapsing) {
            const JgcChar *ch=a->models.chars+i;CharPose pose[12];
            if(ch->nbones>12 || ch->nmotions!=2)continue;
            for(unsigned b=0;b<ch->nbones;b++) {
                chars_sample_pose(&a->models,ch,ch->motion0,p->old_frame,b,pose+b);
                chars_overlay_pose(&a->models,ch,ch->motion0+1,p->collapse_frame,b,1.f,pose+b);
            }
            chars_draw_material(&a->models,i,identity,pose,0,0,1.f,NULL);
        } else chars_draw_effect(&a->models,i,identity,a->frames[i],1.f);
    }
    now=final_clock();final_profile.platforms_us+=now-part;part=now;
    chars_draw_one_xf(&a->models,4,identity,0,a->frames[4]);
    now=final_clock();final_profile.center_us+=now-part;part=now;
    unsigned boss_started=part;
    /* 0ece24, phase < 5. The orbit assembly contains TWO models per
       attachment, both using the selected side's clip and clock. */
    if(a->phase<5) {
        Chars *c=&a->actors;FinalBoss *s=&a->boss;
        chars_draw_material(c,3,identity,NULL,0,s->orbit,1.f,NULL);
        now=final_clock();final_profile.boss_root_us+=now-part;part=now;
        unsigned short remap[65];for(unsigned i=0;i<65;i++)remap[i]=i;
        /* 1d0ec0's slots 0..15 bind PVR 6..21. Slot 16 (PVR 22)
           remains unchanged when 0ecaac rotates the first 16 entries. */
        for(unsigned i=0;i<16;i++)remap[6+i]=6+(i+s->texture_rotation)%16;
        chars_draw_material(c,4,identity,NULL,0,0,1.f,remap);
        now=final_clock();final_profile.boss_surface_us+=now-part;part=now;
        float goji[16];memcpy(goji,identity,64);goji[7]=14.39f; /* 1de928 */
        /* Goji also has original translucent strips (live PCW 828c0002 /
           TSP 9491a4ed). The enclosure is a separate environment-mapped
           satellite mesh, not part of this character's material. */
        if(!a->cinematic || a->phase>0)chars_draw_material(c,0,goji,s->pose,0,0,1.f,NULL);
        now=final_clock();final_profile.goji_us+=now-part;part=now;
        float xf[2][16];finalboss_attachments(c,s->orbit,identity,xf);
        now=final_clock();final_profile.attachments_us+=now-part;part=now;
        for(int side=0;side<2;side++) {
            for(int r=0;r<3;r++)xf[side][r*4+3]+=xf[side][r*4+1]*-.2f; /* 1de934 */
            float frame=side ? s->right:s->left;
            chars_draw_one_xf(c,1,xf[side],side,frame);
            chars_draw_material(c,2,xf[side],NULL,side,frame,1.f,NULL);
        }
        now=final_clock();final_profile.orbit_parts_us+=now-part;part=now;
    } else {
        Chars *c=&a->actors;FinalBoss *s=&a->boss;
        /* 0ed598's shell and 0ecfd8's two boss roots all use the
           recovered central attachment, never an identity placement. */
        chars_draw_material(c,7,a->boss_world,NULL,0,0,1.f,NULL);
        chars_draw_material(c,5,a->boss_world,NULL,0,0,1.f,NULL);
        now=final_clock();final_profile.boss_root_us+=now-part;part=now;
        unsigned short remap[65];for(unsigned i=0;i<65;i++)remap[i]=i;
        for(unsigned i=0;i<2;i++)remap[23+i]=23+(i+s->final_rotation)%2;
        chars_draw_material(c,6,a->boss_world,NULL,0,0,1.f,remap);
        now=final_clock();final_profile.boss_surface_us+=now-part;part=now;
        if(!a->cinematic) {
            float goji[16];memcpy(goji,a->boss_world,sizeof goji);
            for(int r=0;r<3;r++)goji[r*4+3]+=goji[r*4+1]*14.39f;
            chars_draw_material(c,0,goji,s->pose,0,0,1.f,NULL);
        }
        now=final_clock();final_profile.goji_us+=now-part;part=now;
    }
    final_profile.boss_us+=now-boss_started;
    final_profile.draw_calls++;final_profile.draw_us+=now-started;
}
