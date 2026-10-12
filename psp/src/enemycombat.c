#include "enemycombat.h"
#include "player_body.h"
#include "traffic_contact.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_enemy_gas_body.inc"
#else
#include "generated/source_data/enemy_gas_body.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_enemy_hardpoints.inc"
#else
#include "generated/source_data/enemy_hardpoints.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_dog_source.inc"
#else
#include "generated/source_data/dog_source.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_taban_attack_source.inc"
#else
#include "generated/source_data/taban_attack_source.inc"
#endif
#include "audio.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned random15(void *context)
{EnemyCombatWorld *w=context;w->random=w->random*0x41c64e6du+0x3039u;return (w->random>>16)&32767;}
static int probe(void *context,const float *a,const float *b,unsigned mask,float *point,float *normal)
{
    EnemyCombatWorld *w=context;ColHit h;
    if(!col_probe(w->collision,a,b,mask,&h))return 0;
    memcpy(point,h.p,12);memcpy(normal,h.n,12);return 1;
}
static int body_contact(EnemyCombatWorld *w,const float *point,float radius)
{
    Player *p=w->player;if(!p || p->health<=0 || p->reset_query || p->damage_invincible)return -1;
    float reach=player_body_reach(p)+radius;
    if(isfinite(reach) && (fabsf(point[0]-p->x)>reach || fabsf(point[1]-p->y)>reach || fabsf(point[2]-p->z)>reach))return -1;
    float spheres[20][4],other[4]={point[0],point[1],point[2],radius};
    int n=player_body_spheres(p,spheres);
    for(int i=0;i<n;i++)if(spheres[i][3]>0 && player_body_sphere_overlap(spheres[i],other))return i;
    return -1;
}
static int shot_hit(void *context,const float *origin,int kind)
{
    EnemyCombatWorld *w=context;(void)origin;(void)kind;
    if(!player_bullet_vulnerable(w->player))return 0;
    if(body_contact(w,w->gang.target,.1f)<0)return 0;
    if(!w->pending_attribute)w->pending_attribute=26;
    return 1;
}
static int sphere_impact(EnemyCombatWorld *w,const float *point,const float *previous,float radius,int attribute)
{
    Player *p=w->player;
    if(body_contact(w,point,radius)<0)return 0;
    float matrices[20][16],spheres[20][4],other[4]={point[0],point[1],point[2],radius};
    if(player_body_matrices(p,matrices)<0)return 0;player_body_spheres(p,spheres);
    SourceActorContact nearest={0};float best=0;unsigned group=0;
    for(int i=0;i<20;i++)if(spheres[i][3]>0 && player_body_sphere_overlap(spheres[i],other)) {
        float center[3]={matrices[i][3],matrices[i][7],matrices[i][11]};SourceActorContact c;
        float distance=source_actor_contact(&c,center,w->body_valid?w->body_previous[i]:center,
            PLAYER_BODY[p->pose_model][i].attribute,point,previous,attribute);
        if(!group || distance<best){nearest=c;best=distance;}
        unsigned g=traffic_limb_group(PLAYER_BODY[p->pose_model][i].attribute);
        group=group ? (g==group?g:1):g;
    }
    if(!group)return 0;
    if(!w->pending_attribute) {
        const float zero[3]={0},one[3]={1,1,1};int32_t angles[3],axis[3]={0};
        float root[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};player_motion_angles(p,angles);
        for(int i=0;i<3;i++){int k=i==0?0:i==1?2:1;axis[k]=angles[k];source_trs_apply(zero,axis,one,root,player_fsca_sin);axis[k]=0;}
        float back[3]={-10*root[2],-10*root[6],-10*root[10]};
        w->pending_mode=traffic_contact_direction(&nearest,group,back,player_bam_atan2,&w->pending_yaw);
        w->pending_attribute=attribute;
    }
    return 1;
}
static int missile_contact(void *context,const float *point,const float *previous,int variant)
{(void)variant;return sphere_impact(context,point,previous,3.f,30);}
static int actor_contact(void *context,const float *point,const float *previous,float radius,int attribute)
{
    EnemyCombatWorld *w=context;w->body_track=1;
    return sphere_impact(w,point,previous,radius,attribute);
}
/* Keikan 1b6f44/1b6fa4: two identical authored capsules with contact
   attributes 23/22. A broadphase avoids player posing for distant cops. */
static int police_contact(void *context,const void *member,SourceActorContact *out)
{
    EnemyCombatWorld *w=context;Player *p=w->player;const GangMember *m=member;
    if(!p || p->health<=0 || p->reset_query)return 0;
    float dx=p->x-m->pos[0],dy=p->y-m->pos[1],dz=p->z-m->pos[2];
    if(fabsf(dx)>50 || fabsf(dy)>50 || fabsf(dz)>50)return 0;
    w->body_track=1;
    float root[16],a[3],b[3];
    float (*spheres)[4]=w->police_spheres;
    float c=cosf(m->yaw),s=sinf(m->yaw);
    const float matrix[16]={c,0,s,m->pos[0],0,1,0,m->pos[1],-s,0,c,m->pos[2],0,0,0,1};
    memcpy(root,matrix,sizeof root);
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_enemycombat_c__keikan_0.inc"
#else

#include "generated/source_data/enemycombat_c__keikan_0.inc"

#endif

    const float (*local)[3]=m->police.dog?(const float (*)[3])DOG_CAPSULE:keikan;
    for(int j=0;j<3;j++) {
        a[j]=root[j*4+1]*local[0][1]+root[j*4+2]*local[0][2]+root[j*4+3];
        b[j]=root[j*4+1]*local[1][1]+root[j*4+2]*local[1][2]+root[j*4+3];
    }
    if(!w->police_body_ready) {
        float matrices[20][16];
        if(player_body_matrices(p,matrices)<0)return 0;
        player_body_spheres(p,spheres);
        for(int i=0;i<20;i++)for(int j=0;j<3;j++)w->police_centers[i][j]=matrices[i][j*4+3];
        w->police_body_ready=1;
    }
    int found=0;float best=0;
    for(int i=0;i<20;i++)if(spheres[i][3]>0) {
        float ab[3],ap[3],dot=0,length=0;
        for(int j=0;j<3;j++){ab[j]=b[j]-a[j];ap[j]=spheres[i][j]-a[j];dot+=ab[j]*ap[j];length+=ab[j]*ab[j];}
        float t=length>0?fmaxf(0.f,fminf(1.f,dot/length)):0.f,d2=0;
        for(int j=0;j<3;j++){float d=ap[j]-t*ab[j];d2+=d*d;}
        float radius=(m->police.dog?DOG_CAPSULE[6]:2.362499952f)+spheres[i][3];if(!(d2<radius*radius))continue;
        const float *center=w->police_centers[i];SourceActorContact hit;
        float distance=source_actor_contact(&hit,m->pos,m->police.previous,m->flags&2?22:23,
            center,w->body_valid?w->body_previous[i]:center,PLAYER_BODY[p->pose_model][i].attribute);
        if(!found || distance<best){*out=hit;best=distance;found=1;}
    }
    return found;
}
/* Rival1/2/3 use the same capsule dimensions as Keikan, with attribute
   38 (1cf760/1cf77c/1cf798). The authored root includes the spray lean. */
static int rival_contact(void *context,const void *actor)
{
    EnemyCombatWorld *w=context;Player *p=w->player;const Gang *g=actor;
    if(!p || p->health<=0 || p->reset_query)return 0;
    float reach=player_body_reach(p)+18.f;
    if(fabsf(p->x-g->pos[0])>reach || fabsf(p->y-g->pos[1])>reach || fabsf(p->z-g->pos[2])>reach)return 0;
    float root[16],a[3],b[3];gang_member_matrix(g,&g->m[0],root);
    for(int j=0;j<3;j++) {
        a[j]=root[j*4+1]*15.297200203f+root[j*4+2]*.237100005f+root[j*4+3];
        b[j]=root[j*4+1]*2.418800116f+root[j*4+2]*.1778f+root[j*4+3];
    }
    float spheres[20][4];int count=player_body_spheres(p,spheres);
    for(int i=0;i<count;i++)if(spheres[i][3]>0) {
        float ab[3],ap[3],dot=0,length=0;
        for(int j=0;j<3;j++){ab[j]=b[j]-a[j];ap[j]=spheres[i][j]-a[j];dot+=ab[j]*ap[j];length+=ab[j]*ab[j];}
        float t=length>0?fmaxf(0.f,fminf(1.f,dot/length)):0.f,d2=0;
        for(int j=0;j<3;j++){float d=ap[j]-t*ab[j];d2+=d*d;}
        float radius=2.362499952f+spheres[i][3];if(d2<radius*radius)return 1;
    }
    return 0;
}
/* 09d2a8 registers the laser capsule independently of bullet damage and
   player invulnerability. Animated body spheres supply next-tick feedback. */
static int beam_contact(void *context,const float *a,const float *b,float radius)
{
    EnemyCombatWorld *w=context;Player *p=w->player;if(!p || p->reset_query)return 0;
    float position[3]={p->x,p->y,p->z},reach=player_body_reach(p)+radius;
    for(int k=0;k<3;k++)if(position[k]<fminf(a[k],b[k])-reach || position[k]>fmaxf(a[k],b[k])+reach)return 0;
    float ab[3],length=0;for(int k=0;k<3;k++){ab[k]=b[k]-a[k];length+=ab[k]*ab[k];}
    float spheres[20][4];int count=player_body_spheres(p,spheres);
    for(int i=0;i<count;i++)if(spheres[i][3]>0) {
        float dot=0;for(int k=0;k<3;k++)dot+=(spheres[i][k]-a[k])*ab[k];
        float t=length>0?fmaxf(0.f,fminf(1.f,dot/length)):0;
        float nearest[4]={a[0]+t*ab[0],a[1]+t*ab[1],a[2]+t*ab[2],radius};
        if(player_body_sphere_overlap(spheres[i],nearest))return 1;
    }
    return 0;
}
/* Rank 1: 1c8630 capsule. Rank 2: 1b6fe8 four-vertex body panel.
   Test the player's contact body only; this is the source 6a reaction. */
static int sniper_contact(void *context,const void *actor)
{
    EnemyCombatWorld *w=context;Player *p=w->player;const Gang *g=actor;
    if(!p || p->health<=0 || p->reset_query)return 0;
    float reach=player_body_reach(p)+21.f;
    if(fabsf(p->x-g->pos[0])>reach || fabsf(p->y-g->pos[1])>reach || fabsf(p->z-g->pos[2])>reach)return 0;
    float root[16];gang_member_matrix(g,g->m,root);
    if(!g->action) {
        float a[3],b[3];for(int k=0;k<3;k++) {
            a[k]=root[k*4+1]*17.609800338745117f+root[k*4+2]*.1777999997138977f+root[k*4+3];
            b[k]=root[k*4+1]*2.7153000831604004f+root[k*4+2]*.414900004863739f+root[k*4+3];
        }
        return beam_contact(context,a,b,2.628000020980835f);
    }
    float spheres[20][4];int count=player_body_spheres(p,spheres);
    for(int i=0;i<count;i++)if(spheres[i][3]>0) {
        float local[3]={0},delta[3];for(int k=0;k<3;k++)delta[k]=spheres[i][k]-root[k*4+3];
        for(int k=0;k<3;k++)for(int j=0;j<3;j++)local[k]+=delta[j]*root[j*4+k];
        float x=local[0]-fmaxf(-3.0536789894104004f,fminf(3.0536789894104004f,local[0]));
        float y=local[1]-fmaxf(0.f,fminf(16.427446365356445f,local[1]));
        float z=local[2]+1.1751630306243896f;
        if(x*x+y*y+z*z<spheres[i][3]*spheres[i][3])return 1;
    }
    return 0;
}
static int member_obstacle(void *context,const float *before,const float *after,float *normal,float height)
{
    EnemyCombatWorld *w=context;if(!w->collision)return 0;
    float a[3]={before[0],before[1]+height,before[2]},b[3]={after[0],after[1]+height,after[2]};
    ColHit hit;if(!col_probe(w->collision,a,b,2,&hit))return 0;
    memcpy(normal,hit.n,12);return 1;
}

static int police_obstacle(void *ctx,const float *a,const float *b,float *n)
{return member_obstacle(ctx,a,b,n,8.682700157f);}
static int dog_obstacle(void *ctx,const float *a,const float *b,float *n)
{return member_obstacle(ctx,a,b,n,6.210000038f);}

static int rival_wall_type(unsigned s) /* 06e2b0 */
{return s==1 || s==4 || s==18 || s==29;}
static int rival_floor_accept(const ColTri *t,const float *n,const void *context)
{(void)n;(void)context;return !rival_wall_type(t->surf);}
static void rival_terrain(void *context,const float *before,const float *pending,RivalTerrain *out)
{
    EnemyCombatWorld *w=context;memset(out,0,sizeof *out);if(!w->collision)return;
    float a[3]={before[0],before[1]+3.f,before[2]},b[3]={pending[0],pending[1]-1.f,pending[2]};
    ColHit hit;if(!col_probe(w->collision,a,b,99,&hit))return;
    out->hit=1;out->wall=rival_wall_type(hit.surf);memcpy(out->point,hit.p,12);memcpy(out->normal,hit.n,12);
    if(!out->wall){out->floor=1;out->height=hit.p[1];}
    else if(col_probe_filtered(w->collision,a,b,99,rival_floor_accept,NULL,&hit)){
        out->floor=1;out->height=hit.p[1];
    }
}

static void police_aim(void *context,int ticks,float *out)
{
    EnemyCombatWorld *w=context;
    /* 073830: predict from two previous player feet positions. */
    for(int j=0;j<3;j++) {
        float now=w->feet_history[0][j],previous=w->feet_history[1][j],older=w->feet_history[2][j];
        float velocity=now-previous;
        out[j]=velocity*ticks+now+(velocity-(previous-older))*.5f*ticks*ticks;
    }
}
static int police_floor(void *context,const float *before,const float *after,unsigned flags,float *height)
{
    EnemyCombatWorld *w=context;if(!w->collision)return 0;
    /* 0749c4: previous feet +3 -> pending feet -1 (lunges end at feet).
       No long downward cast, and a miss must actually let the officer fall. */
    float a[3]={before[0],before[1]+3.f,before[2]};
    float b[3]={after[0],after[1]-((flags&2)?0.f:1.f),after[2]};
    if(flags&0x2000000){memcpy(a,after,12);a[1]+=2.f;}
    ColHit hit;if(!col_probe(w->collision,a,b,99,&hit))return 0;
    if(hit.n[1]<=0)return 0;
    *height=hit.p[1];return 1;
}

/* 0afe78 reads the previous explosion registrations, not player body
   spheres. 0e3a3c registers blast centers on ages 6..9 for chain reactions. */
static int mine_chain(void *context,const float *p)
{
    EnemyCombatWorld *w=context;
    for(unsigned i=0;i<w->chain_count;i++) {
        float d=0;for(int j=0;j<3;j++){float v=p[j]-w->chain_centers[i][j];d+=v*v;}
        if(d<30.f*30.f)return 1;
    }
    return 0;
}
static void mine_chain_latch(EnemyCombatWorld *w)
{
    w->chain_count=0;
    for(unsigned i=0;i<16;i++)if(w->blasts[i].active && w->blasts[i].age>=6 && w->blasts[i].age<10)
        memcpy(w->chain_centers[w->chain_count++],w->blasts[i].pos,12);
}
static void mine_sound(void *context,const float *p){(void)context;audio_enemy_mine_offer(p);}
static unsigned gun_sound(void *context,unsigned owner,int reference,const float *p){(void)context;return audio_enemy_gun_offer(owner,reference,p);}
static unsigned voice_sound(void *context,unsigned owner,int reference,const float *p){(void)context;return audio_enemy_voice_offer(owner,reference,p);}
static int projectile_contact(void *context,const float *point,float radius)
{return body_contact(context,point,radius)>=0;}
static void sound(void *context,int reference,const float *position)
{
    EnemyCombatWorld *w=context;int error=audio_enemy_sound(reference,position);
    if(error){w->audio_errors++;w->last_audio_error=error;}
}
static int ensure_models(EnemyCombatWorld *w);
static void explosion(void *context,const float *position,int variant)
{
    EnemyCombatWorld *w=context;if(!ensure_models(w))return;w->explosions++;
    for(unsigned i=0;i<16;i++)if(!w->blasts[i].active) {
        w->blasts[i].active=1;w->blasts[i].age=-1;w->blasts[i].variant=variant;
        w->blasts[i].yaw=(int)(random15(w)*2);memcpy(w->blasts[i].pos,position,12);
        sound(w,variant==1?0xd0003:variant==2?0xf0003:variant==3?0xa0002:-0xfff0,position);break;
    }
}
static int ensure_models(EnemyCombatWorld *w)
{
    if(!w->attempted) {
        w->attempted=1;w->error=chars_load(&w->models,"enemyprojectiles.jgc");
        if(!w->error && w->models.h->n_chars!=45){chars_free(&w->models);w->error=-2;}
        if(!w->error)w->shots.impact_frames=w->models.motions[w->models.chars[0].motion0].nframes;
        if(!w->error)w->waves.hit_frames=w->models.motions[w->models.chars[12].motion0].nframes;
    }
    return !w->error;
}
int enemycombat_type_uses_projectiles(int type)
{
    switch(type) {
    case 5:                         /* Bomb explosion (not flame type 23). */
    case 7: case 28:                /* Para members. */
    case 8: case 24:                /* Rocket gun bursts. */
    case 10: case 30:               /* Moving and formation tanks. */
    case 13: case 15: case 32:      /* Helicopter guns and missiles. */
    case 16: case 17:               /* Sniper and Onishima. */
    case 22:                       /* Denki wave. */
    case 26: case 29:               /* Formation police and Killers. */
    case 31:                       /* Mines. */
        return 1;
    default:return 0;
    }
}
int enemycombat_prepare_projectiles(EnemyCombatWorld *w)
{
    if(!w)return 0;
    if(w->attempted)return !w->error;
    if(ensure_models(w))return 1;
    /* Prewarming is optional: other level-load allocations can temporarily
       consume space that is available by the normal first attack. Keep its
       lazy attempt and failure memoization intact instead of disabling the
       weapon for this mission after an early out-of-memory result. */
    chars_free(&w->models);w->attempted=0;return 0;
}
int enemycombat_release_idle_projectiles(EnemyCombatWorld *w)
{
    if(!w || !w->models.h)return 0;
    for(unsigned i=0;i<ENEMY_SHOT_CAPACITY;i++)if(w->shots.shots[i].active)return 0;
    for(unsigned i=0;i<ENEMY_MISSILE_CAPACITY;i++)if(w->missiles.missiles[i].active)return 0;
    for(unsigned i=0;i<ENEMY_SHELL_CAPACITY;i++)if(w->shells.shells[i].active)return 0;
    for(unsigned i=0;i<ENEMY_WAVE_CAPACITY;i++)if(w->waves.waves[i].active)return 0;
    for(unsigned i=0;i<sizeof w->blasts/sizeof *w->blasts;i++)if(w->blasts[i].active)return 0;
    chars_free(&w->models);w->attempted=w->error=0;return 1;
}
int enemycombat_prepare_denki(EnemyCombatWorld *w)
{return ensure_models(w);}
int enemycombat_prepare_flames(EnemyCombatWorld *w)
{
    if(!w->flame_attempted) {
        w->flame_attempted=1;w->flame_error=chars_load(&w->flame_models,"eventflare.jgc");
        if(!w->flame_error && w->flame_models.h->n_chars!=16){chars_free(&w->flame_models);w->flame_error=-2;}
        printf("ENEMY_HAND_FLAMES load=%d\n",w->flame_error);
    }
    return !w->flame_error;
}
static void player_burn_tick(EnemyCombatWorld *w)
{
    Player *p=w->player;PlayerBurn *b=&w->player_burn;
    if(!p)return;
    /* Fire can be accepted by an enemy contact or the player's queued
       arena/spray damage. Bullets share burn_ticks but not fire_hits. */
    int start=p->fire_hits!=w->player_fire_seen;
    w->player_fire_seen=p->fire_hits;
    if(p->health<=0 || p->reset_query){memset(b,0,sizeof *b);return;}
    if(!start && b->remaining<=0)return;
    float anchors[2][3];
    if(!player_sound_points(p,anchors)){memset(b,0,sizeof *b);return;}
    if(start) {
        memset(b,0,sizeof *b);b->remaining=5.f;
        memcpy(b->previous,anchors[0],sizeof b->previous);
        enemycombat_prepare_flames(w);
    }
    /* +75c is collision anchor 18. The original draws the previous sample,
       then captures this tick's torso position for the next update. */
    memcpy(b->pos,b->previous,sizeof b->pos);
    memcpy(b->previous,anchors[0],sizeof b->previous);
    b->image=b->frame;b->frame=(b->frame+1)&15;
    b->remaining-=1.f/18.f;
    b->scale=fminf(b->remaining,1.f)+random15(w)*(.5f/32768.f);
    if(b->remaining<=0){b->remaining=0;b->scale=0;}
}
/* 07ba50: the same two source hand anchors as EVENT's 0eacf0 children.
   Pose only inside the 70-unit collision broadphase; no offscreen poses. */
static void burning_hands(void *context,const void *group,const void *member)
{
    EnemyCombatWorld *w=context;const Gang *g=group;const GangMember *m=member;
    Chars *c=w->actors;const float *target=w->gang.target;float d2=0;
    for(int j=0;j<3;j++){float d=m->pos[j]-target[j];d2+=d*d;}
    if(d2>100.f*100.f || !c || !c->h || c->h->n_chars<=32 || c->chars[32].nbones!=12)return;
    const JgcChar *ch=c->chars+32;CharPose pose[12],other;float root[16],matrices[12][16];
    gang_member_matrix(g,m,root);int motion=gang_police_motion(m);
    for(unsigned b=0;b<12;b++) {
        chars_sample_pose(c,ch,ch->motion0+motion,m->frame,b,pose+b);
        if(m->police.blend>0 && m->police.old_motion>=0 && (unsigned)m->police.old_motion<ch->nmotions) {
            chars_sample_pose(c,ch,ch->motion0+m->police.old_motion,m->police.old_frame,b,&other);
            chars_blend_pose(pose+b,&other,m->police.blend);
        } else if(m->anim==1 && m->police.gait>0) {
            chars_sample_pose(c,ch,ch->motion0+gang_police_gait_motion(m),m->frame,b,&other);
            chars_blend_pose(pose+b,&other,m->police.gait);
        }
    }
    chars_attachment_matrices(c,ch,pose,root,matrices);
    for(int i=0;i<2;i++) {
        float point[3],hand_d2=0;const float *a=matrices[9+i*2];
        for(int j=0;j<3;j++){point[j]=a[j*4+3]-3.27f*a[j*4+1]+2.77f*a[j*4+2];float d=point[j]-target[j];hand_d2+=d*d;}
        if(hand_d2>=50.f*50.f)continue;
        /* 1de840 -> 1de830: local center (0,3,0), radius 3, attr 29. */
        point[1]+=3.f;actor_contact(w,point,point,3.f,29);
    }
}
#include "burning_combat.inc"
static int ensure_gas_models(EnemyCombatWorld *w)
{
    if(!w->gas_attempted) {
        w->gas_attempted=1;w->gas_error=chars_load(&w->gas_models,"enemygas.jgc");
        if(!w->gas_error && (w->gas_models.h->n_chars!=10 || w->gas_models.chars[8].nbones!=6 || w->gas_models.chars[9].nbones!=6)) {
            chars_free(&w->gas_models);w->gas_error=-2;
        }
        if(!w->gas_error)w->gases.frames=w->gas_models.motions[w->gas_models.chars[1].motion0].nframes;
    }
    return !w->gas_error;
}
static void gas_create(void *context,const float *origin,int aim)
{
    EnemyCombatWorld *w=context;Player *p=w->player;if(!p || !ensure_gas_models(w))return;
    const float feet[3]={p->x,p->y,p->z};int32_t angles[3];player_motion_angles(p,angles);
    enemy_gas_create(&w->gases,origin,feet,p->speed,angles[1],aim,w->gas_skin);
}
static void gas_contacts(EnemyCombatWorld *w)
{
    if(!player_bullet_vulnerable(w->player) || !w->gas_models.h)return;
    Chars *c=&w->gas_models;
    for(int i=0;i<ENEMY_GAS_CAPACITY;i++) {
        const EnemyGas *s=w->gases.gas+i;
        if(!s->active || s->state!=3 || s->alpha<=.4f)continue; /* 07ad28 */
        const JgcChar *ch=c->chars+8+s->variant;float root[16],matrices[6][16];
        float sn=player_fsca_sin(w->gas_camera_yaw),co=player_fsca_sin(w->gas_camera_yaw+16384);
        const float m[16]={co,0,sn,s->pos[0],0,1,0,s->pos[1],-sn,0,co,s->pos[2],0,0,0,1};memcpy(root,m,64);
        for(int j=0;j<6;j++) {
            CharPose pose;chars_sample_pose(c,ch,ch->motion0,s->frame,j,&pose);
            const JgcBone *b=c->bones+ch->bone0+j;
            memcpy(matrices[j],b->parent<0?root:matrices[b->parent-ch->bone0],64);
            int32_t angles[3]={pose.ang[0],pose.ang[1],pose.ang[2]};
            source_trs_apply(pose.pos,angles,pose.scl,matrices[j],player_fsca_sin);
            const float *local=GAS_SPHERES[s->variant][j];if(local[3]<=0)continue;
            float center[3];
            for(int r=0;r<3;r++){const float *v=matrices[j]+r*4;center[r]=(float)((double)v[0]*local[0]+(double)v[1]*local[1]+(double)v[2]*local[2]+v[3]);}
            /* 0b70ca copies the authored radius without scaling. */
            if(body_contact(w,center,local[3])>=0){if(!w->pending_attribute)w->pending_attribute=26;return;}
        }
    }
}
static unsigned shot_create(void *context,const float *a,const float *b,int kind,int mode)
{EnemyCombatWorld *w=context;if(mode!=2 && !ensure_models(w))return 0;return enemyshot_create(&w->shots,a,b,kind,mode);}
static int shot_result(void *context,unsigned token)
{return enemyshot_result(&((EnemyCombatWorld *)context)->shots,token);}
static void missile_create(void *context,const float *a,const float *b,int variant)
{EnemyCombatWorld *w=context;if(ensure_models(w))enemy_missile_create(&w->missiles,a,b,variant);}
static void tank_shell_create(void *context,const float *position,float yaw,float frame,int aim)
{
    EnemyCombatWorld *w=context;Chars *c=w->actors;
    if(!c || !c->base || c->h->n_chars<=29 || !ensure_models(w))return;
    const JgcChar *ch=c->chars+29;if(ch->nbones>64 || ch->nbones<3)return;
    /* 0a35b0: barrel attachment, including the member's torso aim. */
    float co=cosf(yaw),si=sinf(yaw);
    float root[16]={co,0,si,position[0],0,1,0,position[1],-si,0,co,position[2],0,0,0,1};
    float matrices[64][16],from[3],to[3];CharPose poses[64];
    for(unsigned i=0;i<ch->nbones;i++)chars_sample_pose(c,ch,ch->motion0,frame,i,poses+i);
    poses[2].ang[1]+=aim;chars_attachment_matrices(c,ch,poses,root,matrices);
    const float *b=matrices[2],*local=ENEMY_HARDPOINTS[12].pos;
    for(int r=0;r<3;r++)from[r]=(float)((double)b[r*4]*local[0]+(double)b[r*4+1]*local[1]+(double)b[r*4+2]*local[2]+b[r*4+3]);
    /* Tank +b8/+174/+180 are feet positions, not the +75c body anchor
       retained by the bullet/sniper history ring. */
    for(int j=0;j<3;j++)to[j]=j==1?w->feet_history[0][j]:w->feet_history[0][j]+20.f*(2.f*w->feet_history[0][j]-3.f*w->feet_history[1][j]+w->feet_history[2][j]);
    enemy_shell_create(&w->shells,from,to);sound(w,0xa0000,from);
}
static void shell_create(void *context,const void *object,const void *actor)
{
    (void)object;const GangMember *m=actor;
    tank_shell_create(context,m->pos,m->yaw,m->frame,m->aim);
}
static void electric_create(void *context,const void *object)
{
    EnemyCombatWorld *w=context;const Gang *g=object;Chars *c=w->actors;
    if(!c || !c->base || c->h->n_chars<=41 || !ensure_models(w))return;
    const JgcChar *ch=c->chars+41;if(ch->nbones>64 || ch->nbones<12)return;
    float root[16],matrices[64][16],point[3]={0};CharPose poses[64];gang_member_matrix(g,g->m,root);
    for(unsigned i=0;i<ch->nbones;i++)chars_sample_pose(c,ch,ch->motion0+3,g->m[0].frame,i,poses+i);
    chars_attachment_matrices(c,ch,poses,root,matrices);
    for(int h=14;h<16;h++) {
        const float *m=matrices[ENEMY_HARDPOINTS[h].bone],*p=ENEMY_HARDPOINTS[h].pos;
        for(int r=0;r<3;r++)point[r]+=(float)((double)m[r*4]*p[0]+(double)m[r*4+1]*p[1]+(double)m[r*4+2]*p[2]+m[r*4+3])*.5f;
    }
    enemy_wave_create(&w->waves,point,(int)(g->m[0].yaw*(65536.f/6.28318530718f)));sound(w,0x160001,point);
}
static int electric_contact(void *context,const float *pos,const float *direction)
{
    EnemyCombatWorld *w=context;Player *p=w->player;
    if(!p || p->health<=0 || p->reset_query)return 0;
    float dx=p->x-pos[0],dy=p->y-pos[1],dz=p->z-pos[2];
    if(dx*dx+dy*dy+dz*dz>=2500.f)return 0; /* source 50-unit registration gate */
    float spheres[20][4];int n=player_body_spheres(p,spheres);
    for(int i=0;i<n;i++) {
        float projection=0;for(int j=0;j<3;j++)projection+=(spheres[i][j]-pos[j])*direction[j];
        if(projection>0x1.4f17c2p+4f)projection=0x1.4f17c2p+4f;
        if(projection< -0x1.4f17c2p+4f)projection=-0x1.4f17c2p+4f;
        float capsule[4]={pos[0]+direction[0]*projection,pos[1]+direction[1]*projection,pos[2]+direction[2]*projection,10.f};
        if(spheres[i][3]>0 && player_body_sphere_overlap(spheres[i],capsule))return 1;
    }
    return 0;
}
static int electric_route(void *context,const float *a,const float *b,int yaw,float *point,float *normal,int *heading)
{
    EnemyCombatWorld *w=context;ColHit hit;
    if(!col_probe(w->collision,a,b,4,&hit))return 0;
    memcpy(normal,hit.n,12);
    return player_wave_surface(w->collision,&hit,yaw,point,heading);
}
static void electric_hit(void *context,const float *point)
{
    EnemyCombatWorld *w=context;
    /* 0fe398 writes speed +dc=0 and toggles rail-side +1e0; no health
       charge, yaw reversal or replacement movement handler. */
    w->player->speed=0;w->player->rail_side=!w->player->rail_side;sound(w,0x160006,point);
}
static void electric_hit_position(void *context,float *point)
{
    const Player *p=((EnemyCombatWorld *)context)->player;
    point[0]=p->x;point[1]=p->y;point[2]=p->z;
}
static int anchor(void *context,const void *object,int index,float *out)
{
    EnemyCombatWorld *w=context;const Gang *g=object;Chars *c=w->actors;
    if((unsigned)index>=sizeof ENEMY_HARDPOINTS/sizeof *ENEMY_HARDPOINTS || !c || !c->base)return 0;
    int model=index<10?(g->kind==13?35:37):index<12?38:29;
    if((unsigned)model>=c->h->n_chars)return 0;
    const JgcChar *ch=c->chars+model;if(ch->nbones>64 || ENEMY_HARDPOINTS[index].bone>=(int)ch->nbones)return 0;
    GangMember member=g->m[0];memcpy(member.pos,g->pos,12);
    int motion;
    if(index<10) {
        member.pitch=g->heli_angles[0]*(6.28318530718f/65536.f);
        member.yaw=g->heli_angles[1]*(6.28318530718f/65536.f);member.roll=g->heli_angles[2]*(6.28318530718f/65536.f);
        motion=g->unit_state==1?6:g->unit_state==2?7:g->unit_state==3?8:5;
    } else if(index<12) {
        motion=sniper_motion(g->action,g->unit_state,member.aim);
    } else motion=member.motion<0?0:member.motion;
    float root[16],matrices[64][16];CharPose poses[64];gang_member_matrix(g,&member,root);
    for(unsigned i=0;i<ch->nbones;i++) {
        chars_sample_pose(c,ch,ch->motion0+motion,member.frame,i,poses+i);
        if(index>=10 && index<12 && g->unit_state<2 && member.secondary_anim>=0)
            chars_overlay_pose(c,ch,ch->motion0+member.secondary_anim,0,i,member.sniper.blend,poses+i);
    }
    chars_attachment_matrices(c,ch,poses,root,matrices);
    const float *m=matrices[ENEMY_HARDPOINTS[index].bone],*p=ENEMY_HARDPOINTS[index].pos;
    for(int r=0;r<3;r++)out[r]=(float)((double)m[r*4]*p[0]+(double)m[r*4+1]*p[1]+(double)m[r*4+2]*p[2]+m[r*4+3]);
    return 1;
}
static void taban_sound(void *context,const float *position)
{(void)context;audio_enemy_chain_offer(position);}
static int ensure_taban_texture(EnemyCombatWorld *w)
{
    if(!w->taban_attempted) {
        w->taban_attempted=1;w->taban_error=tex2d_load(&w->taban_texture,"tabanfx.bin");
        if(!w->taban_error && w->taban_texture.n!=1){tex2d_free(&w->taban_texture);w->taban_error=-2;}
        printf("TABAN_FX load=%d textures=%d\n",w->taban_error,w->taban_texture.n);
    }
    return !w->taban_error;
}
static int taban_anchor(void *context,const void *object,float out[2][3])
{
    EnemyCombatWorld *w=context;const Gang *g=object;Chars *c=w->actors;
    if(!c || !c->h || c->h->n_chars<=40)return 0;
    const JgcChar *ch=c->chars+40;if(ch->nbones!=12)return 0;
    if(!ensure_taban_texture(w))return 0;
    /* Two points on source bone 9. Keep local coordinates: the chain is
       integrated locally and the current actor root transforms it later. */
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    CharPose pose[12];float matrices[12][16];
    for(unsigned i=0;i<12;i++)chars_sample_pose(c,ch,ch->motion0+gang_taban_motion(g),g->m[0].frame,i,pose+i);
    chars_attachment_matrices(c,ch,pose,identity,matrices);
    const float *m=matrices[TABAN_ANCHOR_BONE];
    for(int k=0;k<2;k++)for(int j=0;j<3;j++) {
        const float *p=TABAN_ANCHORS[k];
        out[k][j]=(float)((double)m[j*4]*p[0]+(double)m[j*4+1]*p[1]+(double)m[j*4+2]*p[2]+m[j*4+3]);
    }
    return 1;
}
static int ensure_warning_models(EnemyCombatWorld *w)
{
    if(!w->warning_attempted) {
        w->warning_attempted=1;w->warning_error=chars_load(&w->warning_models,"enemywarning.jgc");
        if(!w->warning_error && w->warning_models.h->n_chars!=1){chars_free(&w->warning_models);w->warning_error=-2;}
        printf("ENEMY_WARNING load=%d\n",w->warning_error);
    }
    return !w->warning_error;
}
unsigned enemycombat_graphics_for_kinds(uint64_t kinds)
{
    unsigned banks=0;
    for(unsigned type=0;type<64;type++)if(kinds&(UINT64_C(1)<<type)) {
        switch(type) {
        case 4: case 27:banks|=ENEMY_GRAPHICS_GAS;break;
        case 21:banks|=ENEMY_GRAPHICS_CHAIN;break;
        case 6:case 9:case 25:banks|=ENEMY_GRAPHICS_BURNING|ENEMY_GRAPHICS_FLAME;break;
        case 23:banks|=ENEMY_GRAPHICS_FLAME;break;
        }
        switch(type) {
        case 7:case 8:case 9:case 13:case 15:case 16:case 17:
        case 24:case 25:case 26:case 28:case 29:case 32:
            banks|=ENEMY_GRAPHICS_WARNING;break;
        }
    }
    return banks;
}
unsigned enemycombat_prepare_graphics(EnemyCombatWorld *w,uint64_t kinds)
{
    if(!w)return 0;
    unsigned wanted=enemycombat_graphics_for_kinds(kinds),ready=0;
    if(wanted&ENEMY_GRAPHICS_WARNING) {
        int attempted=w->warning_attempted;
        if(ensure_warning_models(w))ready|=ENEMY_GRAPHICS_WARNING;
        else if(!attempted){chars_free(&w->warning_models);w->warning_attempted=0;}
    }
    if(wanted&ENEMY_GRAPHICS_CHAIN) {
        int attempted=w->taban_attempted;
        if(ensure_taban_texture(w))ready|=ENEMY_GRAPHICS_CHAIN;
        else if(!attempted){tex2d_free(&w->taban_texture);w->taban_attempted=0;}
    }
    if(wanted&ENEMY_GRAPHICS_GAS) {
        int attempted=w->gas_attempted;
        if(ensure_gas_models(w))ready|=ENEMY_GRAPHICS_GAS;
        else if(!attempted){chars_free(&w->gas_models);w->gas_attempted=0;}
    }
    /* Reserve the shared flare dependency first. If it fails, do not read
       the much larger fire bank only to fail on the same dependency. */
    if(wanted&ENEMY_GRAPHICS_FLAME) {
        int attempted=w->flame_attempted;
        if(enemycombat_prepare_flames(w))ready|=ENEMY_GRAPHICS_FLAME;
        else if(!attempted){chars_free(&w->flame_models);w->flame_attempted=0;}
    }
    if((wanted&ENEMY_GRAPHICS_BURNING) && (ready&ENEMY_GRAPHICS_FLAME)) {
        int attempted=w->burning_attempted;
        if(enemycombat_prepare_burning(w))ready|=ENEMY_GRAPHICS_BURNING;
        else if(!attempted) {
            free(w->fires);w->fires=NULL;chars_free(&w->burning_models);w->burning_attempted=0;
        }
    }
    return ready;
}
unsigned enemycombat_release_idle_graphics(EnemyCombatWorld *w,const Gang *gangs,unsigned count)
{
    if(!w || (count && !gangs))return 0;
    unsigned keep=0,released=0;
    if(w->player_burn.remaining>0)keep|=ENEMY_GRAPHICS_FLAME;
    for(unsigned i=0;i<count;i++)if(gangs[i].active) {
        switch(gangs[i].kind) {
        case 21:keep|=ENEMY_GRAPHICS_CHAIN;break;
        case 6:case 9:case 25:keep|=ENEMY_GRAPHICS_BURNING|ENEMY_GRAPHICS_FLAME;break;
        case 23:keep|=ENEMY_GRAPHICS_FLAME;break;
        }
    }
    for(unsigned i=0;i<ENEMY_GAS_CAPACITY;i++)if(w->gases.gas[i].active)keep|=ENEMY_GRAPHICS_GAS;
    if(w->warning.scale>0 || w->gang.warning || w->gang.report)keep|=ENEMY_GRAPHICS_WARNING;
    for(unsigned i=0;i<ENEMY_FIRE_CAPACITY;i++)if(w->fires && w->fires[i].token)
        keep|=ENEMY_GRAPHICS_BURNING|ENEMY_GRAPHICS_FLAME;
    for(unsigned i=0;i<ENEMY_BURN_CAPACITY;i++)if(w->burns[i].life)
        keep|=ENEMY_GRAPHICS_BURNING|ENEMY_GRAPHICS_FLAME;
    if(!(keep&ENEMY_GRAPHICS_WARNING) && w->warning_models.h && !w->warning_error) {
        chars_free(&w->warning_models);w->warning_attempted=w->warning_error=0;released|=ENEMY_GRAPHICS_WARNING;
    }
    if(!(keep&ENEMY_GRAPHICS_CHAIN) && w->taban_texture.data && !w->taban_error) {
        tex2d_free(&w->taban_texture);w->taban_attempted=w->taban_error=0;released|=ENEMY_GRAPHICS_CHAIN;
    }
    if(!(keep&ENEMY_GRAPHICS_GAS) && w->gas_models.h && !w->gas_error) {
        chars_free(&w->gas_models);w->gas_attempted=w->gas_error=0;released|=ENEMY_GRAPHICS_GAS;
    }
    if(!(keep&ENEMY_GRAPHICS_BURNING) && w->burning_models.h && !w->burning_error) {
        free(w->fires);w->fires=NULL;chars_free(&w->burning_models);w->burning_attempted=w->burning_error=0;released|=ENEMY_GRAPHICS_BURNING;
    }
    if(!(keep&ENEMY_GRAPHICS_FLAME) && w->flame_models.h && !w->flame_error) {
        chars_free(&w->flame_models);w->flame_attempted=w->flame_error=0;released|=ENEMY_GRAPHICS_FLAME;
    }
    return released;
}
void enemycombat_bind(EnemyCombatWorld *w,Player *p,const Collision *col,Chars *actors)
{
    w->player=p;w->collision=col;w->actors=actors;
    w->shots.context=w;w->shots.probe=probe;w->shots.hit=shot_hit;w->shots.sound=sound;
    w->shots.random15=random15;w->shots.atan_bam=player_bam_atan2;w->shots.sin_bam=player_fsca_sin;
    w->missiles.context=w;w->missiles.probe=probe;w->missiles.contact=missile_contact;
    w->missiles.explosion=explosion;w->missiles.random15=random15;
    w->missiles.atan_bam=player_bam_atan2;w->missiles.sin_bam=player_fsca_sin;
    w->shells.context=w;w->shells.probe=probe;w->shells.contact=projectile_contact;w->shells.explosion=explosion;
    w->waves.context=w;w->waves.probe=probe;w->waves.contact=electric_contact;w->waves.hit=electric_hit;
    w->waves.sin_bam=player_fsca_sin;w->waves.atan_bam=player_bam_atan2;w->waves.route=electric_route;
    w->waves.hit_position=electric_hit_position;
    w->gases.context=w;w->gases.probe=probe;w->gases.sound=sound;
    w->gases.random15=random15;w->gases.atan_bam=player_bam_atan2;w->gases.sin_bam=player_fsca_sin;
    w->gang.random15=random15;w->gang.gas=gas_create;
    w->gang.context=w;w->gang.shot=shot_create;w->gang.shot_result=shot_result;
    w->gang.mine_chain=mine_chain;w->gang.mine_sound=mine_sound;w->gang.explosion=explosion;w->gang.gun_sound=gun_sound;
    w->gang.burning_hands=burning_hands;w->gang.fire_stream=fire_stream;w->gang.strong_attack=strong_attack;
    w->gang.voice_sound=voice_sound;
    w->gang.sound=sound;w->gang.missile=missile_create;w->gang.anchor=anchor;
    w->gang.taban_anchor=taban_anchor;w->gang.taban_sound=taban_sound;
    w->gang.shell=shell_create;
    w->gang.tank_shell=tank_shell_create;
    w->gang.contact=actor_contact;
    w->gang.sniper_contact=sniper_contact;w->gang.beam_contact=beam_contact;w->gang.rival_contact=rival_contact;w->gang.rival_terrain=rival_terrain;w->gang.police_contact=police_contact;w->gang.police_obstacle=police_obstacle;w->gang.dog_obstacle=dog_obstacle;w->gang.police_floor=police_floor;w->gang.police_aim=police_aim;
    w->gang.grips=&p->police_grips;
    w->gang.grip_eligible=p->health>0 && !p->reset_query && police_grip_allowed(p->grinding?3:p->state,p->air_trick);
    w->gang.grip_anchors_valid=police_grip_count(&p->police_grips) && player_grab_points(p,w->gang.grip_anchors);
    w->gang.electric=electric_create;
    float points[2][3];
    if(player_sound_points(p,points)){memcpy(w->gang.target,points[0],12);memcpy(w->warning.head,points[1],12);}
    else {w->gang.target[0]=p->x;w->gang.target[1]=p->y;w->gang.target[2]=p->z;}
    w->gang.velocity[0]=p->x-p->px;w->gang.velocity[1]=p->y-p->py;w->gang.velocity[2]=p->z-p->pz;
    w->gang.vulnerable=player_bullet_vulnerable(p);
    w->gang.player_direction[0]=sinf(p->yaw);w->gang.player_direction[1]=0;w->gang.player_direction[2]=cosf(p->yaw);
    w->gang.player_state=p->grinding?3:p->state;w->gang.player_speed=p->speed;
}
void enemycombat_tick(EnemyCombatWorld *w)
{
    ++w->fire_tick;
    if(w->fires)for(int i=0;i<ENEMY_FIRE_CAPACITY;i++)if(w->fires[i].token && w->fire_tick-w->fires[i].seen>1)w->fires[i].token=0;
    mine_chain_latch(w); /* original 070054 swaps the previous registrations */
    w->pending_attribute=0;w->gang.warning=w->gang.report=0;w->police_body_ready=0;w->gang.police_attacking_group=NULL;w->gang.dog_attacking_group=NULL;police_grip_begin(&w->player->police_grips);
    burning_impacts_tick(w);
    float feet[3]={w->player->x,w->player->y,w->player->z};
    if(!w->feet_valid){for(int i=0;i<3;i++)memcpy(w->feet_history[i],feet,12);w->feet_valid=1;}
    else {memcpy(w->feet_history[2],w->feet_history[1],12);memcpy(w->feet_history[1],w->feet_history[0],12);memcpy(w->feet_history[0],feet,12);}
    if(!w->gang.history_valid) {
        for(int i=0;i<30;i++)memcpy(w->gang.history[i],w->gang.target,12);
        w->gang.history_valid=1;w->gang.history_at=0;
    } else w->gang.history_at=(w->gang.history_at+1)%30;
    memcpy(w->gang.history[w->gang.history_at],w->gang.target,12);
    for(unsigned i=0;i<16;i++)if(w->blasts[i].active) {
        int age=++w->blasts[i].age;
        if(age>=(int)w->models.motions[w->models.chars[8].motion0].nframes){w->blasts[i].active=0;continue;}
        if(age<20) {
            /* 1d019c -> 1d0158: radius-20 sphere on the first child of
               the animated cloud, registered for the first 20 ticks. */
            const JgcChar *ch=w->models.chars+8;CharPose root,child;
            chars_sample_pose(&w->models,ch,ch->motion0,age,0,&root);
            chars_sample_pose(&w->models,ch,ch->motion0,age,1,&child);
            float radius=20.f*fabsf(root.scl[0]*child.scl[0]);
            sphere_impact(w,w->blasts[i].pos,w->blasts[i].pos,radius,28);
        }
    }
    enemy_gas_tick(&w->gases);gas_contacts(w);
    enemyshot_tick(&w->shots,w->gang.target);
    float p[3]={w->player->x,w->player->y,w->player->z};enemy_missile_tick(&w->missiles,p);
    enemy_shell_tick(&w->shells);
    enemy_wave_tick(&w->waves);
}
static void retain_body_history(EnemyCombatWorld *w)
{
    int live=w->body_track;w->body_track=0;
    for(unsigned i=0;i<ENEMY_MISSILE_CAPACITY;i++)live|=w->missiles.missiles[i].active;
    for(unsigned i=0;i<16;i++)live|=w->blasts[i].active || w->shells.shells[i].active;
    float matrices[20][16];
    w->body_valid=live && player_body_matrices(w->player,matrices)>=0;
    if(w->body_valid)for(int i=0;i<20;i++)for(int j=0;j<3;j++)w->body_previous[i][j]=matrices[i][j*4+3];
}
int enemycombat_receive(EnemyCombatWorld *w)
{
    audio_enemy_chain_tick(0);audio_enemy_held_tick(0);
    if(w->gang.warning || w->gang.report)ensure_warning_models(w);
    float warning_frames=w->warning_models.h?w->warning_models.motions[w->warning_models.chars[0].motion0].nframes:0;
    enemy_warning_tick(&w->warning,w->gang.report,w->gang.warning,warning_frames);
    /* Retain after ALL projectile and actor contacts, before changing the
       player's pose. Actor callbacks must see the previous tick too. */
    retain_body_history(w);
    int accepted=0;
    if(w->pending_attribute==26)accepted=player_receive_bullet(w->player);
    else if(w->pending_attribute==28)accepted=player_receive_explosion(w->player,w->pending_mode,w->pending_yaw);
    else if(w->pending_attribute==24 || w->pending_attribute==30)accepted=player_receive_vehicle(w->player,w->pending_mode,w->pending_yaw);
    else if(w->pending_attribute==27)accepted=player_receive_taban(w->player,w->pending_mode,w->pending_yaw);
    else if(w->pending_attribute==29)accepted=player_fire_hit(w->player);
    police_grip_end(&w->player->police_grips);
    accepted|=player_police_receive(w->player);
    player_burn_tick(w);
    w->pending_attribute=0;w->received+=accepted;return accepted;
}
void enemycombat_reset(EnemyCombatWorld *w)
{memset(&w->player_burn,0,sizeof w->player_burn);w->player_fire_seen=w->player?w->player->fire_hits:0;memset(w->burns,0,sizeof w->burns);if(w->fires)memset(w->fires,0,ENEMY_FIRE_CAPACITY*sizeof *w->fires);w->fire_tick=0;audio_enemy_chain_tick(1);audio_enemy_held_tick(1);enemy_gas_reset(&w->gases);memset(w->blasts,0,sizeof w->blasts);w->chain_count=0;memset(&w->warning,0,sizeof w->warning);w->body_valid=w->body_track=w->feet_valid=0;enemyshot_reset(&w->shots);enemy_missile_reset(&w->missiles);enemy_shell_reset(&w->shells);enemy_wave_reset(&w->waves);w->pending_attribute=0;w->gang.warning=w->gang.report=0;w->gang.history_valid=0;}
void enemycombat_free(EnemyCombatWorld *w)
{enemycombat_reset(w);free(w->fires);w->fires=NULL;chars_free(&w->burning_models);w->burning_attempted=w->burning_error=0;audio_free_enemies();chars_free(&w->flame_models);w->flame_attempted=w->flame_error=0;tex2d_free(&w->taban_texture);w->taban_attempted=w->taban_error=0;chars_free(&w->gas_models);w->gas_attempted=w->gas_error=0;chars_free(&w->models);chars_free(&w->warning_models);w->attempted=w->error=w->warning_attempted=w->warning_error=0;}
