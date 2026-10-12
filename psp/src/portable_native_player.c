/* Install-only native simulation. Namespace and arithmetic substitutions are
 * confined to this translation unit; gameplay keeps its ordinary renderer. */
#include "portable_native_namespace.h"
#include "portable_native_math.h"
#include "portable_native_pose.h"
#include "player.h"
static void native_follow_input(const Player *,struct FollowInput *);
static void native_landing_sound(Player *);
static void native_collision_sound(Player *,int backward);
#define PLAYER_FOLLOW_INPUT(p,in) native_follow_input((p),(in))
#include "portable_native_player.h"
#include "source_fmac.h"
#include <string.h>
static struct {
    Player *contact_player,*observer_player,*follow_player;
    JgrNativeContact contact;void *contact_user;
    JgrNativeAnimationObserver observer;void *observer_user;
    int defer_finish,observer_failed,ground_below,small_camera;
    int32_t surfaces[19];float small_facing[3];
    int32_t landing_surfaces[64];unsigned landing_count;
    int32_t collision_surfaces[64];unsigned collision_count;
} native_player;
static int native_contact(Player *p,int handler)
{
    if(p!=native_player.contact_player || !native_player.contact)return 0;
    native_player.defer_finish=1;
    int entered=native_player.contact(native_player.contact_user,p,handler);
    native_player.defer_finish=0;return entered;
}
static void native_observe(Player *p,int completed)
{
    if(completed && p->overlay_motion>=0 && p->overlay_blend>0.f){
        /* 03ed98 publishes secondary animation into the same +594 pose
           that the next motion transition saves as its entry pose. Runtime
           rendering applies it lazily, so complete it here for setup and
           retire only the derived render index to avoid applying it twice. */
        for(unsigned joint=0;joint<20;joint++){
            CharPose pose;
            if(jgr_native_player_sample_pose(p,joint,&pose)){native_player.observer_failed=1;break;}
            p->pose[joint]=pose;
        }
        p->overlay_motion=-1;
    }
    if(p==native_player.observer_player && native_player.observer && !native_player.observer_failed &&
       !native_player.observer(native_player.observer_user,p,completed))native_player.observer_failed=1;
}
static void native_ground_query(const Player *p,int hit,const ColHit *h)
{
    if(p!=native_player.follow_player)return;
    native_player.ground_below=0;
    if(hit)for(unsigned i=0;i<19 && native_player.surfaces[i]!=-1;i++)
        if(native_player.surfaces[i]==(int32_t)h->surf){native_player.ground_below=1;break;}
}
#define PLAYER_STATIC_SURFACE_CARRY 1
#define PLAYER_SOURCE_MOVEMENT 1
#define PLAYER_NATIVE_POSE_ENDPOINTS 1
#define PLAYER_CONTACT_SPRAY(p,state) native_contact((p),(state))
#define PLAYER_CONTACT_SPRAY_DEFER_FINISH(p) native_player.defer_finish
#define PLAYER_ANIMATION_OBSERVER(p,completed) native_observe((p),(completed))
#define PLAYER_LANDING_SOUND(p) native_landing_sound(p)
#define PLAYER_COLLISION_SOUND(p,backward) native_collision_sound((p),(backward))
#define PLAYER_QUERY_TRACE(p,col,a,b,c,hit,h) native_ground_query((p),(hit),(h))
#define powf jgr_native_math_power
#define fmaf source_fmac
#define chars_sample_pose jgr_native_chars_sample_pose
#define chars_blend_pose jgr_native_chars_blend_pose
#define chars_overlay_pose jgr_native_chars_overlay_pose
#include "player.c"
#undef powf
#undef fmaf

void jgr_native_player_contact(Player *p,JgrNativeContact contact,void *user)
{native_player.contact_player=p;native_player.contact=contact;native_player.contact_user=user;}
void jgr_native_player_observe(Player *p,JgrNativeAnimationObserver observer,void *user)
{native_player.observer_player=p;native_player.observer=observer;native_player.observer_user=user;native_player.observer_failed=0;}
int jgr_native_player_observer_failed(void){return native_player.observer_failed;}
void jgr_native_player_animation(Player *p){animation_tick(p);}
void jgr_native_player_visual(Player *p){visual_tick(p);}
int jgr_native_player_landing_surfaces(const int32_t *surfaces,unsigned count)
{
    if(count>64 || (count && !surfaces))return 0;
    if(count)memcpy(native_player.landing_surfaces,surfaces,count*sizeof *surfaces);
    native_player.landing_count=count;return 1;
}
static void native_landing_sound(Player *p)
{
    int program=6;
    for(unsigned i=0;i<native_player.landing_count;i++)
        if((unsigned)native_player.landing_surfaces[i]==p->surf){program=8;break;}
    player_sound(p,PLAYER_SOUND_EFFECT,program);
}
int jgr_native_player_collision_surfaces(const int32_t *surfaces,unsigned count)
{
    if(count>64 || (count && !surfaces))return 0;
    if(count)memcpy(native_player.collision_surfaces,surfaces,count*sizeof *surfaces);
    native_player.collision_count=count;return 1;
}
static void native_collision_sound(Player *p,int backward)
{
    int program=backward?4:3;
    for(unsigned i=0;i<native_player.collision_count;i++)
        if((unsigned)native_player.collision_surfaces[i]==p->contact_surface){program=5;break;}
    player_sound(p,PLAYER_SOUND_EFFECT,program);
}
void jgr_native_player_motion(Player *p,int motion){set_motion(p,motion);}
int jgr_native_player_motion_valid(int motion,int overlay_reference,int overlay_ticks)
{
    return (unsigned)motion<PLAYER_MOTION_COUNT && overlay_ticks>=0 &&
        (unsigned)overlay_reference<PLAYER_REFERENCE_COUNT;
}
void jgr_native_player_effect_input(const Player *p,SkateInput *out)
{
    effect_live_input(p,out);memcpy(out->external,p->portal_translation,12);
    float matrices[20][16];player_body_matrices_masked(p,matrices,0xfffff);
    for(unsigned i=0;i<12;i++) {
        const SkateAnchor *a=SKATE_ANCHORS[p->pose_model]+(i<8?i:i+11);
        source_point(matrices[a->bone],a->pos,out->points[i]);
    }
}
int jgr_native_player_sample_pose(const Player *p,unsigned joint,CharPose *out)
{
    if(!p || !out || !p->pose_valid || joint>=20 || !p->pose_bank)return -1;
    const Chars *bank=p->spray_variant && p->spray_bank?p->spray_bank:p->pose_bank;
    unsigned model=bank==p->pose_bank?(unsigned)p->pose_model:0;
    if(!bank->h || model>=bank->h->n_chars)return -1;
    const JgcChar *ch=bank->chars+model;if(joint>=ch->nbones)return -1;
    *out=p->pose[joint];
    /* Runtime bodies and actor drawing apply secondary animation on top of
       the primary pose; route publication must expose that same final pose. */
    if(p->overlay_motion>=0 && (unsigned)p->overlay_motion<ch->nmotions && p->overlay_blend>0.f)
        chars_overlay_pose(bank,ch,ch->motion0+p->overlay_motion,p->overlay_frame,joint,p->overlay_blend,out);
    return 0;
}
void jgr_native_player_spawn(Player *p,const Chars *bank,unsigned model,unsigned character,
                             const CtrlStream *s,unsigned stage,const unsigned idle_thresholds[2])
{
    player_init(p,s->x,s->y,s->z);player_set_character(p,(int)character);
    p->world_stage=(int)stage;p->yaw=(s->yaw+32768)*(6.283185307f/65536.f);p->speed=s->speed;
    p->surf=0;p->onground=1;
    float forward[3];rotated_basis((int32_t)s->w5,(int32_t)s->w7,0,forward,p->gn);
    p->visual_valid=1;p->visual_angles[0]=(int32_t)s->w5;p->visual_angles[1]=s->yaw;p->visual_angles[2]=(int32_t)s->w7;
    if(idle_thresholds)memcpy(p->idle_thresholds,idle_thresholds,sizeof p->idle_thresholds);
    p->health=p->health_max=p->tun->health_max;p->sound_blocked=1;p->trick_hud.blocked=1;
    player_bind_animation(p,bank,(int)model);
    /* Creation animation precedes the original health initialization. */
    int health=p->health;p->health=0;animation_tick(p);p->health=health;
    player_cam_reset(p);p->reset_query=1;
}
void jgr_native_player_rival_relocate(Player *p,const CtrlStream *s)
{
    /* 0278c0 -> 01ae70/01aeb0(0), then 06bd04(kind1). The source resets
       physical state but does not recreate the animation or render pose. */
    int valid=p->visual_valid;int32_t visual[3];memcpy(visual,p->visual_angles,sizeof visual);
    const float position[3]={s->x,s->y,s->z};player_reposition(p,position);
    p->yaw=(s->yaw+32768)*(6.283185307f/65536.f);p->speed=s->speed;p->mirrored=0;
    float forward[3];rotated_basis((int32_t)s->w5,(int32_t)s->w7,0,forward,p->gn);
    p->visual_valid=valid;memcpy(p->visual_angles,visual,sizeof visual);
}
void jgr_native_player_rival_wait_tick(Player *p)
{
    /* The wait branch returns only from 06bdf0. 0198f8 still publishes the
       idle animation, smoothing, effects, clocks and following camera. */
    p->px=p->x;p->py=p->y;p->pz=p->z;p->state=1;
    set_motion(p,3);animation_tick(p);visual_tick(p);effects_tick(p);damage_clocks(p);
    p->reset_query=0;p->logic_tick++;
    if(p->after_tick)p->after_tick(p->after_tick_context);
    if(p->camera_tick)p->cam_yaw=p->camera_tick(p->camera_context);
}
static void native_follow_input(const Player *p,FollowInput *in)
{
    player_follow_input(p,in);
    if(p==native_player.follow_player)in->ground_below=native_player.ground_below;
}
static float native_follow_tick(void *context)
{
    PlayerFollow *f=context;Player *p=f->player;FollowInput in;
    float yaw=player_follow_tick(context);native_follow_input(p,&in);
    if(p->small_spray_active && native_player.small_camera && !f->region) {
        for(int k=0;k<3;k++) {
            f->view.pos[k]=in.body[k]+native_player.small_facing[k]*30.f;
            f->view.forward[k]=(in.body[k]+p->small_spray_target[k])*.5f-f->view.pos[k];
        }
        follow_normalize(f->view.forward);f->view.fov=0x4000;
    }
    return yaw;
}
void jgr_native_player_follow_bind(PlayerFollow *f,Player *p,const Collision *col,const int32_t surfaces[19])
{
    player_follow_bind(f,p,col);FollowInput in;player_follow_input(p,&in);follow_camera_reset(&f->camera,&in);
    memcpy(f->camera.eye,f->camera.next_eye,12);memcpy(f->camera.target,f->camera.next_target,12);
    f->camera.state=6;p->camera_reset=0;
    native_player.follow_player=p;native_player.ground_below=native_player.small_camera=0;
    memcpy(native_player.surfaces,surfaces,sizeof native_player.surfaces);
    p->camera_tick=native_follow_tick;p->camera_context=f;p->cam_yaw=32768.f/BAM;
}
void jgr_native_player_follow_spray(PlayerFollow *f){player_follow_spray(f);}
int jgr_native_player_spray_tracks(const void *data,unsigned bytes)
{
    if(!data){memset(&spray_tracks,0,sizeof spray_tracks);return 1;}
    return spray_tracks_init(&spray_tracks,data,bytes);
}
void jgr_native_player_spray(void *context,Graffiti *g,Spot *s,int action,int cue)
{
    Player *p=context;player_spray_actor(context,g,s,action,cue);
    if(action==SPRAY_ACT_SMALL && p==native_player.follow_player) {
        native_player.small_camera=p->small_spray_active && s->source;
        if(s->source)memcpy(native_player.small_facing,s->source->facing,12);
    }
}
