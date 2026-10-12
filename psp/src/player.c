#include "diagnostics.h"
/* Source-backed movement laws; collision, camera and animation-state
   integration are still incomplete transcriptions. See movement tests and
   decompile/NOTES.md. The game ticks at 30 Hz, independent of rendering. */
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "player.h"
#ifndef PLAYER_NATIVE_POSE_ENDPOINTS
#define PLAYER_NATIVE_POSE_ENDPOINTS 0
#endif
#if PLAYER_SOURCE_MOVEMENT
#include "source_probe.h"
#endif
#include "spray_tracks.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_motmap.inc"
#else
#include "generated/source_data/motmap.h"
#endif
#include "trial.h"
#include "small_spray.h"
#include "finaldamage.h"
#include "enemy_damage.h"
#include "hang.h"

#ifdef __PSP__
#include <pspkernel.h>
#endif
static PlayerProfile player_profile;
static unsigned player_clock(void)
{
#if defined(__PSP__) && JGR_DIAGNOSTICS
    return sceKernelGetSystemTimeLow();
#else
    return 0;
#endif
}
void player_profile_read(PlayerProfile *out)
{*out=player_profile;memset(&player_profile,0,sizeof player_profile);}
void player_profile_totals(PlayerProfile *out)
{*out=player_profile;}

#define TICK (1.0f / 30.0f)
#define BAM (65536.0f / (2.0f * 3.14159265f))
/* motion ids seen in the trace (+0x78) */
#define MOT_IDLE 0
#define MOT_COAST 4
#define MOT_SKATE 6
#define MOT_DASH 7   /* PLAY anim 0x47: skating with the dash flag (+0x108) */
#define MOT_BRAKE 12
#define MOT_JUMP_UP 65
#define MOT_JUMP_DOWN 66
#define MOT_LAND 70
#define MOT_SJUMP_UP 71
#define MOT_SJUMP_DOWN 72
#define MOT_STUMBLE 0x62
#define MOT_GRIND 9

#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_player_tuning.inc"
#else
#include "generated/source_data/player_tuning.inc"
#endif
typedef struct {
    int type, clip, oneshot, low_clip, clip_flags;
    int end_clip, end_oneshot, end_low_clip, end_flags, blend_kind, flags;
    float rate, blend_step, entry_blend;
} MotionSpec;
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_player_motion.inc"
#else
#include "generated/source_data/player_motion.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_player_fsca.inc"
#else
#include "generated/source_data/player_fsca.inc"
#endif

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_player_c__ground_types_0.inc"
#else

#include "generated/source_data/player_c__GROUND_TYPES_0.inc"

#endif

int surf_is_ground(unsigned s) { for (unsigned i = 0; i < sizeof GROUND_TYPES; i++) if (GROUND_TYPES[i] == s) return 1; return 0; }
int player_anim(int id)
{
    /* The original type-0 idle handler (FUN_8c03fb54) does not loop the
       table's 28-frame entry clip (animation 95).  Descriptor 0 points at
       logical motion 112, whose PLAY animation is the 85-frame steady idle
       (87; 78 at low health).  Until the full blend graph is represented in
       Player, select that actual steady target here. */
    if (id >= 0 && id < PLAYER_MOTION_COUNT && (MOTION_SPEC[id][0].type == 0 || MOTION_SPEC[id][0].type == 6 || MOTION_SPEC[id][0].type == 7)) return MOTION_SPEC[id][0].clip;
    return (id >= 0 && id < NMOTMAP && MOTMAP[id] >= 0) ? MOTMAP[id] : 0;
}

void player_cam_reset(Player *p) { p->cam_ex = p->x - sinf(p->yaw) * 30.f; p->cam_ez = p->z - cosf(p->yaw) * 30.f; p->cam_yaw = p->yaw; p->cam_init = 1; p->camera_reset=1; }
void player_init(Player *p, float x, float y, float z)
{
    memset(p, 0, sizeof *p); p->x = x; p->y = y; p->z = z; p->px = x; p->py = y; p->pz = z;
    p->tun = &BEAT_TUNING; p->state = 1; p->motion = player_anim(MOT_IDLE); p->surf = 0xffff;
    p->gn[1] = 1.f;p->ground_y=y;
    p->health_max = p->health = 110;   /* character record word 0 (Beat 110; table 0x8c182884) */
    p->pose_motion=p->motion; p->pose_motion2=-1;p->overlay_motion=-1;
    p->skate_rate=p->entry_weight=1.f;
    p->rnd=1; /* 018fb0: +8c0 is initialized to one, NOT allocator zero */
}
void player_set_character(Player *p, int id) { p->character_id=(unsigned)id<16 ? id : 0; p->tun=&PLAYER_TUNING[p->character_id]; }
void player_restart_score(Player *p)
{
    /* The retry adapter reuses Player rather than recreating the stage's
       actor/HUD tasks. Discard the old attempt before resetting its score;
       ordinary combo failure still retains pending numeric task awards. */
    int blocked=p->trick_hud.blocked;
    memset(&p->trick_hud,0,sizeof p->trick_hud);p->trick_hud.blocked=blocked;
    p->combo=p->chain=p->score_add=0;p->grind_dist=0;
    p->turn_delay=0; /* discard the old attempt's not-yet-created Turn task */
}
void player_reset_controls(Player *p)
{
    memset(&p->water,0,sizeof p->water);
    if(p->sound_event)p->sound_event(p->sound_context,PLAYER_SOUND_RESET,0);
    p->grind_voice_ticks=0;p->sound_contact_override=0;p->shallow_pending=0;
    p->rnd=1; /* 01aeb0 reset branch, shared by mission/scene/reposition adapters */
    p->spray_active=0;p->spray_camera.active=0;p->spray_mode=p->spray_variant=0;
    p->small_spray_active=0;p->small_spray_frame=0;p->pose_overlays_blocked=0;
    memset(&p->police_grips,0,sizeof p->police_grips);p->police_grab_damage_clock=p->police_knockdown=0;
    p->spray_hint_ready=0;
    p->ground_ticks=0;
    p->stumble_ticks=0;
    memset(&p->effects,0,sizeof p->effects);
    p->visual_valid=0;
    /* Platform bookkeeping must not carry a queued press/brake into a new
       mission or a teleported playback segment. Preserve position/stats. */
    p->pending_pressed = 0; p->tick_acc = 0.f;
    p->braking = p->brake_released = p->brake_forced = 0; p->brake_step = 0.f;
    p->brake_contact=p->contact_headon=0;
    p->idle_ticks=0;
    p->turn_lock=p->turn_delay=p->practice_event=0;
    p->stick_history[0] = p->stick_history[1] = 0.f;
    p->jump_held = p->rise_ticks = 0; p->boost = p->boost_cool = p->dash_held = 0;
    p->air_probe_valid = 0;
    p->body_valid=0;p->contact_mode=8;p->contact_speed=-1.f;
    p->air_pitch=0;
    p->obstacle_cooldown=p->contact_protected=0;p->wall_used=0;
    p->ramp_active=0;
    p->reset_query=0;
    p->vehicle_recovery=0;
    memset(&p->car_pull,0,sizeof p->car_pull);p->queued_root_yaw=0;
    p->local_shift=0.f;
    memset(p->portal_pending,0,sizeof p->portal_pending);
    memset(p->portal_translation,0,sizeof p->portal_translation);
    memset(p->surface_motion,0,sizeof p->surface_motion);
    memset(p->air_surface_motion,0,sizeof p->air_surface_motion);
    p->slip_yaw=p->slip_mode=0;p->slip_speed=p->slip_decay=0;
    memset(p->input_mag,0,sizeof p->input_mag); memset(p->input_yaw,0,sizeof p->input_yaw);
}
static float wrap_pi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static void set_motion(Player *p, int id) {
    if (id != p->logical_motion) {
        if (p->pose_valid) memcpy(p->entry_pose,p->pose,sizeof p->pose);
        const MotionSpec *old=&MOTION_SPEC[p->logical_motion][!!p->mirrored];
        int preserve=(old->flags&1) && id==15;
        /* 03ed98: transfer the outgoing pose's root yaw into the travel
           heading. Without this, a completed turn/trick snaps back to the
           old heading. Only the frozen entry pose is counter-rotated. */
        if (p->pose_valid && p->state!=10 && p->state!=19 && p->state!=20) {
            const MotionSpec *next=&MOTION_SPEC[id][!!p->mirrored];
            if ((old->flags&2) && !preserve && next->type!=7) {
                int delta=(short)p->entry_pose[0].ang[1]+(p->mirrored ? 32768 : 0);
                p->yaw=wrap_pi(p->yaw+delta/BAM);
                p->queued_root_yaw+=delta;
                p->visual_angles[1]=(int)lroundf(p->yaw*BAM)-32768;
                p->entry_pose[0].ang[1]=p->mirrored ? -32768 : 0;
            }
            if (next->flags&0x40) {
                p->yaw=wrap_pi(p->yaw+(short)p->entry_pose[0].ang[1]/BAM);
                p->visual_angles[1]=(int)lroundf(p->yaw*BAM)-32768;
                p->entry_pose[0].ang[1]=0;p->speed=-p->speed;p->kb=-p->kb;
            }
        }
        /* FUN_8c03ed98: the old descriptor can change handedness before the
           new descriptor is looked up again. Logical 17 preserves it. */
        if (id!=17) {
            if (old->flags&8) p->mirrored=1;
            else if (old->flags&32) p->mirrored=0;
            const MotionSpec *next=&MOTION_SPEC[id][!!p->mirrored];
            if (next->flags&4) p->mirrored=1;
            else if (next->flags&16) p->mirrored=0;
        }
        int skating=(id==6 && p->logical_motion==7) || (id==7 && p->logical_motion==6);
        p->entry_weight=skating ? 1.f : MOTION_SPEC[id][!!p->mirrored].entry_blend;
        if (!skating && (id==6 || id==7)) p->skate_dash=id==7 ? 1.f : 0.f;
        if (id==4) p->coast_blend=fminf(1.f,fabsf((float)p->pitch)/6371.f);
        p->logical_motion=id; p->motion=player_anim(id);
        if (!preserve && !skating) p->frame=0;
        p->motion_done=p->motion_phase=0; p->motion_blend=0; p->motion_hold=0;
    }
}
/* FUN_8c0bdf30. Completion is strictly past the final sample; looping does
   one wrap followed by the original out-of-range guards, not fmod. */
static int animation_advance(float *frame, float rate, int n, int clamp)
{
    *frame+=rate;
    if (clamp) {
        if (*frame>n-1) { *frame=n-1; return 1; }
        if (*frame<0.f) { *frame=0.f; return 1; }
    } else {
        if (*frame>=n) *frame-=n;
        if (*frame<0.f) *frame+=n;
        if (*frame>=n) *frame=0.f;
        if (*frame<0.f) *frame=n-1;
    }
    return 0;
}
/* FUN_8c22d2e0: degree-ten Bernstein polynomial, not a sampled lookup or
   a generic smoothstep. The eleven control points are executable data. */
static float animation_curve(int kind, float t)
{
    if (kind==0) return t;
    static const int choose[11]={1,10,45,120,210,252,210,120,45,10,1};
    float value=0.f;
    for (int i=0;i<=10;i++) {
        float weight=(float)choose[i]*powf(t,(float)i);
        weight*=powf(1.f-t,(float)(10-i));
        value=fmaf(weight,MOTION_BLEND_CURVE[kind-1][i],value);
    }
    return value;
}
static int animation_clip(const Player *p,int clip,int low)
{
    return low>=0 && p->health<PLAYER_LOW_HEALTH[p->character_id] ? low : clip;
}
float player_recorded_spray_reveal_step(const Player *p)
{
    /* A completed replay pose already ran 04089e..0408ae. Recover its
       published reveal rate without advancing animation, RNG or callbacks. */
    if(!p || (unsigned)p->character_id>=sizeof PLAYER_CLIP_FRAMES/sizeof *PLAYER_CLIP_FRAMES ||
       (unsigned)p->logical_motion>=PLAYER_MOTION_COUNT)return 0.f;
    const MotionSpec *s=&MOTION_SPEC[p->logical_motion][!!p->mirrored];
    if(s->type!=7 || !(s->clip_flags&0x100))return 0.f;
    int clip=animation_clip(p,s->clip,s->low_clip);
    if((unsigned)clip>=sizeof PLAYER_CLIP_FRAMES[0]/sizeof PLAYER_CLIP_FRAMES[0][0])return 0.f;
    unsigned frames=PLAYER_CLIP_FRAMES[p->character_id][clip];
    return frames ? 1.f/frames:0.f;
}
static void player_sound(Player *p,int kind,int program)
{if(!p->sound_blocked && p->sound_event){unsigned started=player_clock();p->sound_event(p->sound_context,kind,program);player_profile.audio_us+=player_clock()-started;}}
static void player_voice(Player *p,int kind)
{
    int program=source_player_voice(kind,&p->rnd,p->speed,p->logic_tick,&p->grind_voice_ticks);
    if(program>=0)player_sound(p,PLAYER_SOUND_VOICE,program);
}
static int locomotion_clip(const Player *p,int ref)
{
    return animation_clip(p,LOCOMOTION_CLIPS[ref][0],LOCOMOTION_CLIPS[ref][1]);
}
static void sample_ref(const Player *p,int ref,float frame,unsigned bone,CharPose *out)
{
    const Chars *c=p->spray_variant && p->spray_bank ? p->spray_bank:p->pose_bank;
    const JgcChar *ch=&c->chars[c==p->pose_bank ? p->pose_model:0];
    chars_sample_pose(c,ch,ch->motion0+locomotion_clip(p,ref),frame,bone,out);
}
static void mix_ref(const Player *p,int a,int b,float frame,float weight,unsigned bone,CharPose *out)
{
#if PLAYER_NATIVE_POSE_ENDPOINTS
    /* Setup uses the same exact endpoint rules as its source-rounded blend.
       Do not sample a pose that the blend immediately discards. */
    if(weight==0.f){sample_ref(p,a,frame,bone,out);return;}
    if(weight==1.f){sample_ref(p,b,frame,bone,out);return;}
#endif
    CharPose other;sample_ref(p,a,frame,bone,out);sample_ref(p,b,frame,bone,&other);
    chars_blend_pose(out,&other,weight);
}
/* FUN_8c03ffa8 / 040380. Two speed-dependent skating trees, each with
   its own turn poses, followed by the six-tick skate/dash transition.
   Preserve the nested blends: a flat weighted sum changes Ninja angles. */
static void skating_pose(const Player *p,unsigned bone,CharPose *out)
{
    float v=p->speed,f=p->pose_frame,w=0.f;int a,b;
    if (p->mirrored) { sample_ref(p,58,(int)f,bone,out);return; }
    CharPose normal,dash,turn,other;
#if PLAYER_NATIVE_POSE_ENDPOINTS
    const int need_normal=p->pose_dash!=1.f,need_dash=p->pose_dash!=0.f;
    if(need_normal){
#endif
    if (v<0x1.da12f6p-2f) a=b=0;
    else if (v<0x1.da12f6p-1f) { a=0;b=1;w=(v-0x1.da12f6p-2f)/0x1.da12f6p-2f; }
    else if (v<0x1.da12f6p+0f) { a=1;b=2;w=(v-0x1.da12f6p-1f)/0x1.da12f6p-1f; }
    else if (v<0x1.284bdap+1f) { a=2;b=3;w=(v-0x1.da12f6p+0f)/0x1.da12f8p-2f; }
    else a=b=3;
    mix_ref(p,a,b,f,w,bone,&normal);
#if PLAYER_NATIVE_POSE_ENDPOINTS
    }
    if(need_dash){
#endif
    if (v<0x1.da12f6p-1f) { a=b=2;w=0.f; }
    else if (v<0x1.284bdap+1f) { a=2;b=3;w=(v-0x1.da12f6p-1f)/0x1.638e38p+0f; }
    else { a=b=3;w=0.f; }
    mix_ref(p,a,b,f,w,bone,&dash);
    if (p->pose_boost>0.f) {
        sample_ref(p,8,f,bone,&other);
        /* 0402b6 writes +5a4, not +5a0; 040380 then rebuilds +5a4.
           Consequently the boost pose affects the dash tree only. */
        chars_blend_pose(&dash,&other,p->pose_boost);
    }
#if PLAYER_NATIVE_POSE_ENDPOINTS
    }
#endif
    if (p->skate_lean!=0.f) {
        int side=p->skate_lean<0.f ? 4 : 6;
#if PLAYER_NATIVE_POSE_ENDPOINTS
        if(need_normal){
#endif
        if (v<0x1.da12f6p-1f) sample_ref(p,side,(int)f,bone,&turn);
        else if (v<0x1.da12f6p+0f) mix_ref(p,side,side+1,f,(v-0x1.da12f6p-1f)/0x1.da12f6p-1f,bone,&turn);
        else sample_ref(p,side+1,(int)f,bone,&turn);
        chars_blend_pose(&normal,&turn,fabsf(p->skate_lean));
#if PLAYER_NATIVE_POSE_ENDPOINTS
        }
        if(need_dash){
#endif
        sample_ref(p,side+1,(int)f,bone,&turn);chars_blend_pose(&dash,&turn,fabsf(p->skate_lean));
#if PLAYER_NATIVE_POSE_ENDPOINTS
        }
#endif
    }
#if PLAYER_NATIVE_POSE_ENDPOINTS
    if(!need_normal){*out=dash;return;}
    if(!need_dash){*out=normal;return;}
#endif
    *out=normal;chars_blend_pose(out,&dash,p->pose_dash);
}
static void animation_pose(Player *p)
{
    if (!p->pose_bank) return;
    const Chars *c=p->spray_variant && p->spray_bank ? p->spray_bank:p->pose_bank;
    const JgcChar *ch=&c->chars[c==p->pose_bank ? p->pose_model:0];
    const MotionSpec *s=&MOTION_SPEC[p->logical_motion][!!p->mirrored];
    for (unsigned bi=0;bi<ch->nbones && bi<64;bi++) {
        CharPose pose,other;
        if (s->type==3 || s->type==4) skating_pose(p,bi,&pose);
        else {
            chars_sample_pose(c,ch,ch->motion0+p->pose_motion,p->pose_frame,bi,&pose);
            if (p->pose_motion2>=0) {
                chars_sample_pose(c,ch,ch->motion0+p->pose_motion2,p->pose_frame2,bi,&other);
                chars_blend_pose(&pose,&other,p->pose_blend);
            }
        }
        if (p->pose_valid && p->pose_entry_weight<1.f) {
            other=p->entry_pose[bi];chars_blend_pose(&other,&pose,p->pose_entry_weight);pose=other;
        }
        p->pose[bi]=pose;
    }
    p->pose_valid=1;
}
void player_bind_animation(Player *p,const Chars *bank,int model)
{
    if (!bank || !bank->h || (unsigned)model>=bank->h->n_chars) return;
    if (p->pose_bank==bank && p->pose_model==model && p->pose_valid) return;
    p->pose_bank=bank;p->pose_model=model;p->pose_valid=0;p->pose_entry_weight=1.f;
    memset(&p->effects,0,sizeof p->effects);
    animation_pose(p);memcpy(p->entry_pose,p->pose,sizeof p->pose);
}
static void animation_small_spray(Player *p)
{
    if(p->pose_overlays_blocked || !p->small_spray_active)return;
    int clip=53+p->small_spray_ref-160;
    if((unsigned)(clip-53)>=7){p->small_spray_active=0;return;}
    const Chars *c=p->spray_variant && p->spray_bank ? p->spray_bank:p->pose_bank;
    if(c && p->pose_valid) {
        const JgcChar *ch=&c->chars[c==p->pose_bank ? p->pose_model:0];
        float frame=fminf(p->small_spray_frame,(float)c->motions[ch->motion0+clip].nframes-1.f);
        for(unsigned bi=0;bi<ch->nbones && bi<64;bi++)
            chars_overlay_pose(c,ch,ch->motion0+clip,frame,bi,1.f,p->pose+bi);
    }
    if(animation_advance(&p->small_spray_frame,1.f,PLAYER_CLIP_FRAMES[p->character_id][clip],1))p->small_spray_active=0;
}
static int dc_yaw(float yaw);
static void animation_tick_impl(Player *p)
{
    const MotionSpec *s=&MOTION_SPEC[(unsigned)p->logical_motion<PLAYER_MOTION_COUNT ? p->logical_motion : 0][!!p->mirrored];
    int first=animation_clip(p,s->clip,s->low_clip);
    int second=animation_clip(p,s->end_clip,s->end_low_clip);
    p->motion_done=0; p->motion_progress=0.f;p->spray_reveal_step=0.f;
    p->pose_motion2=-1; p->pose_blend=0.f;
    if (s->type==7) { /* 0407d4: integer pose BEFORE advancing, always clamp. */
        p->entry_weight=1.f; /* +5b8: this handler replaces the entry pose outright. */
        p->motion=p->pose_motion=first;p->pose_frame=(int)p->frame;
        int n=PLAYER_CLIP_FRAMES[p->character_id][first];
        p->motion_progress=p->frame/n;p->motion_flags=s->clip_flags;
        p->motion_done=animation_advance(&p->frame,s->rate,n,1);
        if(s->clip_flags&0x100)p->spray_reveal_step=1.f/n; /* 04089e..0408ae */
    } else if (s->type==6) { /* FUN_8c040900, trick/jump/crash/recovery and spray */
        if (p->motion_phase==0) {
            p->motion=p->pose_motion=first; p->pose_frame=p->frame;
            int n=PLAYER_CLIP_FRAMES[p->character_id][first];
            p->motion_progress=p->frame/n; p->motion_flags=s->clip_flags;
            if (animation_advance(&p->frame,s->rate,n,s->oneshot)) {
                if (second<0) p->motion_done=1;
                else {
                    p->motion_hold=p->frame; p->motion_blend=s->blend_step;
                    p->motion_phase=1; p->frame=0.f;
                }
            }
        } else if (p->motion_phase==1) {
            p->motion=second; p->motion_flags=s->end_flags;
            p->pose_motion=first; p->pose_frame=p->motion_hold;
            p->pose_motion2=second; p->pose_frame2=0.f;
            p->pose_blend=animation_curve(s->blend_kind,p->motion_blend);
            p->motion_blend+=s->blend_step;
            if (p->motion_blend>=1.f) { p->motion_blend=1.f; p->motion_phase=2; }
        } else {
            p->motion=p->pose_motion=second; p->pose_frame=p->frame;
            p->motion_flags=s->end_flags;
            p->motion_done=animation_advance(&p->frame,s->rate,
                PLAYER_CLIP_FRAMES[p->character_id][second],s->end_oneshot);
        }
    } else if (s->type==3 || s->type==4) {
        int before=(int)p->frame;
        p->skate_boost=fmaxf(0.f,fminf(1.f,p->skate_boost+(p->boost ? 0x1.555556p-3f : -0x1.555556p-3f)));
        p->pose_boost=p->skate_boost;p->pose_dash=p->skate_dash;
        p->motion=p->pose_motion=locomotion_clip(p,p->mirrored ? 58 : 0);
        p->pose_frame=p->frame;p->motion_flags=64;
        p->skate_dash=fmaxf(0.f,fminf(1.f,p->skate_dash+(s->type==4 ? 0x1.555556p-3f : -0x1.555556p-3f)));
        float rate=s->rate*p->skate_rate;
        if (s->type==4 && fabsf(p->skate_lean)>=.1f) rate=s->rate*(1.f+fabsf(p->skate_lean));
        if (p->boost) rate=1.3f;else rate=fminf(2.f,rate);
        animation_advance(&p->frame,rate,PLAYER_CLIP_FRAMES[p->character_id][locomotion_clip(p,0)],0);
        if(source_player_foot_contact(s->type,before,(int)p->frame)) {
            int program=source_player_skate_sound(p->surf,p->boost,p->sound_contact_override);
            if(program>=0)player_sound(p,PLAYER_SOUND_EFFECT,program);
        }
    } else if (s->type==8 || s->type==9) {
        /* 040590/040724: original stairs, sampled BEFORE advancing.
           Ascending blends walking ref 0 into ref 91 with slope heading;
           descending uses ref 92. These are animation references, not
           logical motion IDs. Keep the original integer/fractional split. */
        int before=(int)p->frame,ref=s->type==9 ? 92:0;
        int angle=abs((int16_t)(p->ground_heading-dc_yaw(p->yaw)));
        if(s->type==8 && angle>=27306)ref=91;
        p->motion=p->pose_motion=animation_clip(p,PLAYER_ANIMATION_REFS[ref][0],PLAYER_ANIMATION_REFS[ref][1]);
        p->pose_frame=(int)p->frame;p->motion_flags=0;
        if(s->type==8 && angle>=16384 && angle<27306){
            p->pose_frame=p->pose_frame2=p->frame;
            p->pose_motion2=animation_clip(p,PLAYER_ANIMATION_REFS[91][0],PLAYER_ANIMATION_REFS[91][1]);
            p->pose_blend=(float)(angle-16384)/10922.f;
        }
        float rate=p->speed<0x1.da12f6p-2f ? 1.f:p->speed/0x1.da12f6p-2f;
        int clock_clip=animation_clip(p,PLAYER_ANIMATION_REFS[s->type==9 ? 92:0][0],PLAYER_ANIMATION_REFS[s->type==9 ? 92:0][1]);
        animation_advance(&p->frame,rate,PLAYER_CLIP_FRAMES[p->character_id][clock_clip],0);
        if(source_player_foot_contact(s->type,before,(int)p->frame)){
            int program=source_player_skate_sound(p->surf,p->boost,p->sound_contact_override);
            if(program>=0)player_sound(p,PLAYER_SOUND_EFFECT,program);
        }
    } else if (s->type==5) {
        /* 03fea4: fast ramp stance selects reference14/61, advances its
           own looping clock, then samples the INTEGER frame. It must not
           retain the previous skate clip or its contact-effect flags. */
        p->motion=p->pose_motion=locomotion_clip(p,p->mirrored ? 61:14);
        animation_advance(&p->frame,s->rate,PLAYER_CLIP_FRAMES[p->character_id][p->motion],0);
        p->pose_frame=(int)p->frame;p->motion_flags=0;
    } else if (s->type==2) { /* 03fd9c: rear-car grip, pose BEFORE advancing. */
        p->motion=p->pose_motion=locomotion_clip(p,p->mirrored ? 62:17);
        p->pose_frame=(int)p->frame;p->motion_flags=0;
        animation_advance(&p->frame,s->rate,PLAYER_CLIP_FRAMES[p->character_id][p->motion],0);
    } else if (s->type==1) {
        float target=fminf(1.f,fabsf((float)p->pitch)/6371.f);
        if (!p->mirrored) {
            if (target>p->coast_blend) p->coast_blend+=.06f;
            if (target<p->coast_blend) p->coast_blend-=.06f;
            if (fabsf(p->coast_blend-target)<=.06f) p->coast_blend=target;
        }
        p->motion=p->pose_motion=locomotion_clip(p,p->mirrored ? 61 : 13);
        animation_advance(&p->frame,s->rate,PLAYER_CLIP_FRAMES[p->character_id][p->motion],0);
        p->pose_frame=p->mirrored ? (int)p->frame : p->frame;
        if (!p->mirrored) { p->pose_motion2=locomotion_clip(p,14);p->pose_frame2=p->frame;p->pose_blend=p->coast_blend; }
        p->motion_flags=0;
    } else if (s->type==0) {
        p->motion=p->pose_motion=first;p->motion_flags=0;
        animation_advance(&p->frame,s->rate,PLAYER_CLIP_FRAMES[p->character_id][first],0);
        p->pose_frame=(int)p->frame;
    } else {
        p->pose_motion=p->motion; p->pose_frame=p->frame;
        animation_advance(&p->frame,s->rate,PLAYER_CLIP_FRAMES[p->character_id][p->motion],0);
    }
    p->pose_entry_weight=p->entry_weight;
    animation_pose(p);
    animation_small_spray(p);
    /* 03f926 switches +560/+558 after rebuilding +594. The previous
       model supplies unkeyed bind TRS on the first transition tick. */
    p->spray_variant=p->spray_mode==2 || p->small_spray_active;
    /* 03f962..03faa6: persistent owners follow animation flags, including
       the separate small-spray overlay, not a guessed movement-state loop. */
    player_sound(p,PLAYER_SOUND_GRIND,(p->motion_flags&1)?source_player_grind_sound(p->surf):-1);
    player_sound(p,PLAYER_SOUND_SPRAY,(p->motion_flags&0x80)||p->small_spray_active?0:-1);
    if (p->entry_weight<1.f) p->entry_weight+=s->entry_blend;
    if(p->pose_overlays_blocked){p->overlay_motion=-1;return;}
    /* FUN_8c03ed98 +5d8/+5e0/+5e4: six-tick overlay fade, with the
       overlay clock advanced BEFORE sampling (unlike the primary graph). */
    if (p->overlay_ticks>0) { p->overlay_blend=fminf(1.f,p->overlay_blend+0x1.555556p-3f);p->overlay_ticks--; }
    else p->overlay_blend=fmaxf(0.f,p->overlay_blend-0x1.555556p-3f);
    if (p->overlay_blend>0.f && (unsigned)p->overlay_logical<PLAYER_REFERENCE_COUNT) {
        /* +5dc goes straight through 0207a8/0207f4, not 02077c's
           logical-motion graph. Fire reference 158 is PLAY clip 72. */
        const short *o=PLAYER_ANIMATION_REFS[p->overlay_logical];
        p->overlay_motion=animation_clip(p,o[0],o[1]);
        animation_advance(&p->overlay_frame,1.f,PLAYER_CLIP_FRAMES[p->character_id][p->overlay_motion],o[2]);
    } else p->overlay_motion=-1;
}
static void animation_tick(Player *p)
{
#ifdef PLAYER_ANIMATION_OBSERVER
    PLAYER_ANIMATION_OBSERVER(p,0);
#endif
    unsigned t=player_clock();animation_tick_impl(p);player_profile.animation_us+=player_clock()-t;
#ifdef PLAYER_ANIMATION_OBSERVER
    PLAYER_ANIMATION_OBSERVER(p,1);
#endif
}
void player_rival_wait(Player *p)
{set_motion(p,3);animation_tick(p);p->logic_tick++;}

static void animation_overlay(Player *p,int logical)
{
    if((unsigned)logical>=PLAYER_MOTION_COUNT)return;
    /* Contact callbacks resolve the mirrored logical motion through
       02077c before passing its animation reference to 03fae0. */
    const MotionSpec *motion=&MOTION_SPEC[logical][!!p->mirrored];
    int reference;
    for(reference=0;reference<PLAYER_REFERENCE_COUNT;reference++) {
        const short *candidate=PLAYER_ANIMATION_REFS[reference];
        if(candidate[0]==motion->clip && candidate[1]==motion->low_clip &&
           candidate[2]==motion->oneshot)break;
    }
    if(reference==PLAYER_REFERENCE_COUNT)return;
    const short *s=PLAYER_ANIMATION_REFS[reference];
    int clip=animation_clip(p,s[0],s[1]);
    p->overlay_logical=reference;p->overlay_mirror=!!p->mirrored;
    p->overlay_ticks=PLAYER_CLIP_FRAMES[p->character_id][clip];
    p->overlay_frame=p->overlay_blend=0.f;
}

/* FSCA's CPU ROM is not the correctly rounded libm sine. Even its two
   positive quadrants differ. The one-bit differences affect wall modes at
   exact speed/angle thresholds, so preserve the numeric table. */
static float bam_sin(int angle)
{
    unsigned a = (unsigned)angle & 65535;
    float s=SH4_SIN[a&32767];
    return a >= 32768 ? -s : s;
}
static float bam_cos(int angle) { return bam_sin(angle+16384); }
/* Shared PSP ROM access: enemy math must not reserve another 128 KiB table. */
float player_fsca_sin(int angle) { return bam_sin(angle); }
float player_recovery_flash(const Player *p)
{
    /* 019cfc: damage protection takes precedence. SHLL16 wraps before
       unsigned division by 30; phase uses actor +0x98, not pickup age. */
    if(p->recovery_flash_ticks<=0 || p->damage_invincible>0)return 0.f;
    unsigned angle=((uint32_t)p->logic_tick<<16)/30u;
    float sine=player_fsca_sin(angle);
    /* FCMP/GT + FNEG preserves FSCA's negative zero at half turns. */
    return (sine<0.f ? -sine:sine)*0.5f;
}
float player_camera_focal(int32_t angle,float width,float scale_x)
{
    /* Signed divide by two truncates toward zero before FSCA, rather than
       passing a half-angle in radians to libm. Keep the two divisions. */
    int half=angle/2;
    float tangent=bam_sin(half)/bam_cos(half);
    return (width/scale_x)/(tangent*2.f);
}
void player_camera_matrix(const float *pos,const float *f,int32_t roll,float *m)
{
    /* 27a7c8, including its FMAC sites and vertical-camera branch.
       Reusing lookAt + an invented up vector changes authored roll. */
    float sn=bam_sin(-(uint32_t)roll),cs=bam_cos(-(uint32_t)roll);
    float h2=fmaf(f[0],f[0],f[2]*f[2]);
    float inv=1.f/sqrtf(fmaf(f[1],f[1],h2));
    float pitch=f[1]*inv,h=sqrtf(h2)*inv,sy=0.f,cy=1.f;
    if(h2>0x1.0c6f7ap-20f){float r=1.f/sqrtf(h2);sy=-(f[0]*r);cy=f[2]*r;}
    float n[16]={0};
    n[0]=cs*cy-(sn*pitch)*sy;n[1]=fmaf(sy,cs*pitch,sn*cy);n[2]=-(h*sy);
    n[4]=-(sn*h);n[5]=cs*h;n[6]=pitch;
    n[8]=fmaf(cy,sn*pitch,cs*sy);n[9]=sn*sy-(cs*pitch)*cy;n[10]=h*cy;
    for(int j=0;j<3;j++)n[12+j]=(-(pos[0]*n[j])-pos[1]*n[4+j])-pos[2]*n[8+j];
    n[15]=1.f;
    /* Ninja negates its first two columns before publishing 32e10c.
       Converting Y/Z conventions cancels only the Y negation. */
    for(int r=0;r<4;r++)for(int c=0;c<4;c++)m[r*4+c]=n[c*4+r]*(r==0||r==2 ? -1.f:1.f);
}
static int dc_yaw(float yaw) { return (int)lroundf(yaw*BAM)-32768; }
/* FUN_8c103df0: binary search in the first quadrant, including its original
   32767 (not 32768) adjustment in quadrants II/III. */
static int bam_atan2(float y, float x)
{
    if (x == 0.f) return y < 0.f ? -16384 : 16384;
    if (y == 0.f) return x < 0.f ? 32768 : 0;
    /* 103e22 FIPR fv4,fv4 rounds once after the wide dot product.
       Separate rounded y*y and x*x can move the binary search by a BAM
       unit, including the staff camera's orbital tracking yaw. */
    float squared=(float)((double)y*y+(double)x*x);
    float s = fabsf(y) * (1.f/sqrtf(squared));
    int lo = 0, hi = 16384, mid;
    do {
        mid = lo+(hi-lo)/2;
        float a = bam_sin(mid);
        if (a == s) break;
        if (a > s) hi = mid; else lo = mid;
    } while (hi-lo != 1);
    if (x < 0.f) mid = 32767-mid;
    return y < 0.f ? -mid : mid;
}
int player_bam_atan2(float y,float x){return bam_atan2(y,x);}
int player_surface_motion(const float *current,const float *old,const float *point,float *delta,int *yaw)
{
    delta[0]=delta[1]=delta[2]=0.f;*yaw=0;
    if(!old)return 1;
    float u[3],v[3],w[3];
    for(int k=0;k<3;k++) {u[k]=current[3+k]-current[k];v[k]=current[6+k]-current[k];w[k]=point[k]-current[k];}
    /* 0b5b58 tries XY, XZ, then YZ, not the largest projected area.
       Preserve separate multiply/subtract/divide operations, including the
       strict >0.1 test and the current-point minus old-interpolated-point. */
    int a=0,b=1;float det=u[0]*v[1]-v[0]*u[1];
    if(!(fabsf(det)>.1f)) {b=2;det=u[0]*v[2]-v[0]*u[2];}
    if(!(fabsf(det)>.1f)) {a=1;det=u[1]*v[2]-v[1]*u[2];}
    if(!(fabsf(det)>.1f))return 0;
    float s=(w[a]*v[b]-v[a]*w[b])/det;
    float t=(u[a]*w[b]-w[a]*u[b])/det;
    for(int k=0;k<3;k++) {
        float x=(old[3+k]-old[k])*s;
        float y=(old[6+k]-old[k])*t;
        delta[k]=point[k]-((x+y)+old[k]);
    }
    /* 0b5cf4 uses the SUM of both edge vectors, not either edge alone. */
    float ox=(old[3]-old[0])+(old[6]-old[0]);
    float oz=(old[5]-old[2])+(old[8]-old[2]);
    float cx=u[0]+v[0],cz=u[2]+v[2];
    int before=bam_atan2(-ox,-oz),after=bam_atan2(-cx,-cz);
    *yaw=(int16_t)(after-before);return 1;
}
static int contact_pitch(float yaw, const float *n)
{
    int a = dc_yaw(yaw);
    float height=n[0]*bam_sin(a)+n[2]*bam_cos(a);
    if (n[1] == 0.f) return height == 0.f ? 0 : 16384;
    return bam_atan2(height/n[1],1.f);
}
/* Rx * Rz * Ry, as queued by FUN_8c053048. Movement on a slope is measured
   along this rotated unit vector, not in horizontal units plus a Y snap. */
static void ground_angles(const Player *p,int *rx,int *rz)
{
    *rx = -bam_atan2(-p->gn[2],p->gn[1]);
    *rz = bam_atan2(-p->gn[0],sqrtf(p->gn[1]*p->gn[1]+p->gn[2]*p->gn[2]));
    if (p->surf == 5 || p->surf == 31 || p->surf == 61) *rx = *rz = 0;
}
static void rotated_basis(int rx,int rz,int yaw,float *forward,float *up)
{
    float sx=bam_sin(rx),cx=bam_cos(rx),sz=bam_sin(rz),cz=bam_cos(rz);
    float x=bam_sin(yaw),z=bam_cos(yaw);
    forward[0]=cz*x;
#ifdef PLAYER_SOURCE_MOVEMENT
    /* 053048 builds Rx * Rz * Ry through Ninja matrix rotations. The
       first rotation rounds each coefficient; FTRV then rounds the two
       products together. Two separate binary32 multiplies lose a slope
       query ULP and can move a later rail departure by one whole tick. */
    forward[1]=(float)((double)(cx*sz)*x-(double)sx*z);
    forward[2]=(float)((double)(sx*sz)*x+(double)cx*z);
#else
    forward[1]=cx*sz*x-sx*z;forward[2]=sx*sz*x+cx*z;
#endif
    up[0]=-sz; up[1]=cx*cz; up[2]=sx*cz;
}
#ifdef PLAYER_SOURCE_MOVEMENT
static void source_air_translation(const float origin[3],int yaw,int pitch,float shift,float speed,float rise,float out[3])
{
    /* 0535e0 composes Ry(yaw) * Rx(-pitch), translates the local
       correction, then (0,rise,speed). Use the actual negative FSCA
       angle: its two positive quadrants are not interchangeable bits. */
    float sy=bam_sin(yaw),cy=bam_cos(yaw);
    float sp=-bam_sin(-pitch),cp=bam_cos(-pitch);
    float forward[3]={sy*cp,sp,cy*cp},up[3]={-sy*sp,cp,-cy*sp};
    float base[3]={source_fmac(cy,shift,origin[0]),origin[1],source_fmac(-sy,shift,origin[2])};
    for(int k=0;k<3;k++)out[k]=(float)((double)up[k]*rise+(double)forward[k]*speed+(double)base[k]);
}
#endif
static void ground_basis(const Player *p,float *forward,float *up)
{
    int rx,rz;ground_angles(p,&rx,&rz);
    if (p->ramp_active) { rx=p->ramp_rx;rz=p->ramp_rz; }
    rotated_basis(rx,rz,dc_yaw(p->yaw)+32768,forward,up);
}
void player_motion_angles(const Player *p,int32_t angles[3])
{
    int rx=0,rz=0;
    if(p->onground || p->grinding)ground_angles(p,&rx,&rz);
    if(p->ramp_active){rx=p->ramp_rx;rz=p->ramp_rz;}
    angles[0]=rx;angles[1]=dc_yaw(p->yaw);angles[2]=rz;
}
void player_small_spray_begin(Player *p,const float target[3])
{
    if(police_grip_count(&p->police_grips))return;
    int rx=0,rz=0;
    if(p->onground || p->grinding)ground_angles(p,&rx,&rz);
    if(p->ramp_active){rx=p->ramp_rx;rz=p->ramp_rz;}
    const float pos[3]={p->x,p->y,p->z};const int angles[3]={rx,dc_yaw(p->yaw),rz};float local[3];
    int ref=small_spray_target(pos,angles,target,local);
    if(ref<0)return;
    p->small_spray_active=1;p->small_spray_ref=ref;p->small_spray_frame=0;
    memcpy(p->small_spray_target,target,sizeof p->small_spray_target);
    /* 01abb8 -> 03fb0c(1): cancel an active secondary overlay immediately;
       a previously fading overlay (count <= 0) is not forcibly cleared. */
    if(p->overlay_ticks>0) {
        p->overlay_ticks=0;p->overlay_logical=0;p->overlay_frame=p->overlay_blend=0;p->overlay_motion=-1;
    }
}
void player_root_matrix(const Player *p,float *m)
{
    int rx=0,rz=0;
    if (p->onground || p->grinding) ground_angles(p,&rx,&rz);
    if (p->ramp_active) { rx=p->ramp_rx;rz=p->ramp_rz; }
    float forward[3],right[3],up[3],unused[3];
    int yaw=dc_yaw(p->yaw)+32768;
    if (p->visual_valid) { rx=p->visual_angles[0];yaw=p->visual_angles[1]+32768;rz=p->visual_angles[2]; }
    rotated_basis(rx,rz,yaw,forward,up);rotated_basis(rx,rz,yaw+16384,right,unused);
    for (int k=0;k<3;k++) { m[k*4]=right[k];m[k*4+1]=up[k];m[k*4+2]=forward[k]; }
    m[3]=p->x;m[7]=p->y;m[11]=p->z;m[12]=m[13]=m[14]=0.f;m[15]=1.f;
}
#include "source_trs.h"
#include "follow_camera.h"
#include "stage_portal.h"
void player_follow_input(const Player *p,FollowInput *in)
{
    memset(in,0,sizeof *in);in->pos[0]=p->x;in->pos[1]=p->y;in->pos[2]=p->z;
    in->ground_y=p->ground_y;in->reset=p->camera_reset;
    memcpy(in->translation,p->portal_translation,sizeof in->translation);
    int rx=0,rz=0;if(p->onground || p->grinding)ground_angles(p,&rx,&rz);
    if(p->ramp_active){rx=p->ramp_rx;rz=p->ramp_rz;}
    in->angle[0]=rx;in->angle[1]=dc_yaw(p->yaw);in->angle[2]=rz;
    /* +80 is not the movement state. Ordinary jumps retain mode 0;
       01c340 assigns mode 6 only on the high-speed trick branch. */
    in->mode=p->state==10 ? 8:p->state==4 ? 3:p->state==3 ? 4:p->state==2 ? (p->ramp_active ? 5:p->air_trick ? 6:0):0;
    in->ramp_yaw=p->ramp_axis;in->wall_side=p->wall_side;
    float m[16];player_root_matrix(p,m);
    for(int k=0;k<3;k++)in->body[k]=(float)((double)m[k*4+1]*10+m[k*4+3]);
    /* +820 is an explicit world relocation, not +82c surface carry.
       Credits' captured +820/+868/+870/+1f4 are zero. Do not substitute
       velocity or ground state for these separate source fields. */
}
void player_scene_matrix(const float *pos,const int32_t *a,const float *scale,float *m)
{
    source_trs(pos,a,scale,m);
}
void player_result_begin(Player *p,const float pos[3],const int32_t angles[3])
{
    /* The result handler copies JMDc's pose verbatim. In particular, it
       does not extract root yaw as the EVENT-to-gameplay handoff does. */
    p->state=10;p->sub=0;
    p->x=p->px=pos[0];p->y=p->py=p->ground_y=pos[1];p->z=p->pz=pos[2];
    p->yaw=(angles[1]+32768)*(6.283185307f/65536.f);
    memcpy(p->visual_angles,angles,sizeof p->visual_angles);p->visual_valid=1;
    p->pose_overlays_blocked=1;p->small_spray_active=p->spray_active=p->spray_mode=0;
    p->spray_camera.active=0;p->grinding=p->ramp_active=0;p->onground=1;
    p->speed=p->vy=0;p->queued_root_yaw=0;p->pending_pressed=0;
    memset(&p->car_pull,0,sizeof p->car_pull);
    memset(p->surface_motion,0,sizeof p->surface_motion);
    memset(p->air_surface_motion,0,sizeof p->air_surface_motion);
    set_motion(p,21); /* PLAY 173, original one-shot victory dance. */
    animation_tick(p);
}
void player_scene_handoff(Player *p,const float *pos,const int32_t *angles,int clip,float frame)
{
    /* 0cd788 -> 08b828: root CNK yaw joins authored actor angles before the
       original matrix/binary-angle extraction. Never substitute JMDc spawn. */
    int32_t a[3]={angles[0],angles[1],angles[2]};float scale[3]={1,1,1},m[16];
    if(p->pose_bank && p->pose_model>=0) {
        const Chars *c=p->pose_bank;const JgcChar *ch=&c->chars[p->pose_model];
        for(unsigned j=0;j<ch->nbones && j<64;j++)chars_sample_pose(c,ch,clip,frame,j,&p->pose[j]);
        if(ch->nbones)a[1]+=(short)p->pose[0].ang[1];
        p->pose_valid=1;memcpy(p->entry_pose,p->pose,sizeof p->pose);
    }
    player_scene_matrix(pos,a,scale,m);
    int yaw=bam_atan2(-m[2],-m[10]);
    p->x=p->px=pos[0];p->y=p->py=p->ground_y=pos[1];p->z=p->pz=pos[2];
    p->yaw=(yaw+32768)*(6.283185307f/65536.f);
    p->speed=p->vy=0;p->state=1;p->sub=0;p->onground=1;p->grinding=0;p->logic_tick=0;
    p->gn[0]=p->gn[2]=0;p->gn[1]=1;
    player_reset_controls(p);
    /* 08b828 -> 01ae70 -> 01aeb0 sets +90 at 01b0f6. The first
       resumed query must not accept collision history retained across
       an EVENT (notably the rotating final-stage platforms). */
    p->reset_query=1;
    p->logical_motion=0;p->motion_phase=0;p->motion_progress=0;
    p->motion=clip;p->frame=frame;p->pose_motion=clip;p->pose_frame=frame;
    p->pose_motion2=-1;p->overlay_motion=-1;p->pose_blend=0;
    player_cam_reset(p);
}

/* 0b26f0 takes the short path, with a specific half-turn tie direction. */
static int visual_approach(int current,int target,int step)
{
    int a=current&65535,b=target&65535;
    int delta=b-a;
    if (delta>=32768) delta-=65536;else if (delta<=-32768) delta+=65536;
    if (abs(delta)<step) return b;
    return a+(delta<0 ? -step : step);
}
static void visual_smooth(int *angles,const int *target)
{
    for (int i=0;i<3;i++) {
        int delta=(short)(target[i]-angles[i]);
        angles[i]=visual_approach(angles[i],target[i],i==1 ? 0x71c : abs(delta)/5);
    }
}
static void visual_tick(Player *p)
{
    int target[3]={0,dc_yaw(p->yaw),0};
    if (p->onground || p->grinding) ground_angles(p,&target[0],&target[2]);
    if (p->ramp_active) { target[0]=p->ramp_rx;target[2]=p->ramp_rz; }
    if (!p->visual_valid) { memcpy(p->visual_angles,target,sizeof target);p->visual_valid=1; }
    else visual_smooth(p->visual_angles,target);
}

#include "player_body.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_player_body.inc"
#else
#include "generated/source_data/player_body.inc"
#endif
#include "player_body_cache.inc"
#include "skatefx.inc"
int player_sound_points(const Player *p,float out[2][3])
{
    if(!p->pose_bank || !p->pose_valid || (unsigned)p->pose_model>=16)return 0;
    /* 0b71ec publishes section-5 collision-hierarchy anchors at +684.
       Use its bind defaults and FSCA/TRS, not the display skeleton. */
    unsigned mask=(1u<<SKATE_ANCHORS[p->pose_model][18].bone)|(1u<<SKATE_ANCHORS[p->pose_model][28].bone);
    float mats[20][16];if(player_body_matrices_masked(p,mats,mask)<0)return 0;
    const float *m[2],*v[2];
    for(int k=0;k<2;k++) {
        const SkateAnchor *a=&SKATE_ANCHORS[p->pose_model][k?28:18];m[k]=mats[a->bone];v[k]=a->pos;
    }
    source_point_batch(m,v,out,2);
    return 1;
}

/* 051d48 / 182cac: front, left and right police anchors. */
int player_grab_points(const Player *p,float out[3][3])
{
    if(!p || !p->pose_valid || (unsigned)p->pose_model>=16)return 0;
    unsigned mask=0;for(int k=23;k<26;k++)mask|=1u<<SKATE_ANCHORS[p->pose_model][k].bone;
    float matrices[20][16];if(player_body_matrices_masked(p,matrices,mask)<0)return 0;
    const float *m[3],*v[3];
    for(int k=0;k<3;k++) {
        const SkateAnchor *a=SKATE_ANCHORS[p->pose_model]+23+k;m[k]=matrices[a->bone];v[k]=a->pos;
    }
    source_point_batch(m,v,out,3);
    return 1;
}

static int shallow_contact(const ColTri *tri,const float *normal,const void *context)
{
    (void)normal;int *water=(int *)context;
    /* 055954 tests whether list 10db04 finds ANY surface 8, then copies
       the HEAD contact's point. The tunnel has a surface-7 overlay one
       unit above its surface-8 patch; testing only the head misses water. */
    if(tri->surf==8)*water=1;
    return 1;
}
static int contact_accept(const ColTri *tri, const float *n, const void *context)
{
    const Player *p = context; unsigned s = tri->surf;
    if (!surf_is_ground(s)) return 0;
    /* FUN_8c05249c / 05254c: ground, rail and airborne hit lists differ. */
    if (p->state != 2 && p->state != 3) {
        if (SURF_IS_RAIL(s)) return 0;
        if (p->surf == 10 && (s == 53 || s == 54)) return 0;
        if ((p->surf == 53 || p->surf == 54) && s == 10) return 0;
    }
    if (p->state == 3 && s == 12) return 0;
    /* FUN_8c055c1c: only types 55/56 have the 60-degree slope gate. */
    if ((s == 55 || s == 56) && bam_atan2(sqrtf(n[0]*n[0]+n[2]*n[2]),n[1]) >= 10922) return 0;
    return 1;
}
static float ground_probe_down(const Player *p, const CharTuning *t)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_player_c__shallow_0.inc"
#else

#include "generated/source_data/player_c__shallow_0.inc"

#endif

    for (unsigned i=0;i<sizeof shallow;i++) if (p->surf == shallow[i]) return t->shallow_down;
    return t->ground_down;
}
static float landing_speed(const CharTuning *t, float speed)
{
    /* FUN_8c056dcc really is asymmetric: the negative branch compares to the
       floor but has no store after that comparison. Do not 'correct' it. */
    if (speed > t->stumble_bounce) speed = fmaxf(t->stumble_bounce,speed*t->land_k);
    if (speed < -t->stumble_bounce) speed *= t->land_k;
    return speed;
}
static unsigned player_random(Player *p, unsigned modulus)
{
    return source_player_random(&p->rnd,p->speed,p->logic_tick,modulus);
}
static int trick_gesture(const Player *p)
{
    /* FUN_8c01a61c: a recent forward/back/forward gesture, NOT a test for
       rail state. Current forward run must be shorter than three samples. */
    int angle=dc_yaw(p->yaw),run=0;
    while (run<9 && p->input_mag[run]>.5f && abs((int16_t)(angle-p->input_yaw[run]))<10922) run++;
    if (run==0 || run>=3) return 0;
    for (int i=1;i<9;i++) if (p->input_mag[i]>.5f && abs((int16_t)(angle-p->input_yaw[i]))>p->tun->back_angle)
        for (int j=i;j<9;j++) if (p->input_mag[j]>.5f && abs((int16_t)(angle-p->input_yaw[j]))<10922) return 1;
    return 0;
}
static void consume_trick_gesture(Player *p)
{
    /* 01ba78 -> 01a520 fills history with the CURRENT input after jump
       selection. An old flick must not be reused after a quick re-catch. */
    for (int i=1;i<9;i++) {p->input_mag[i]=p->input_mag[0];p->input_yaw[i]=p->input_yaw[0];}
    /* The adapter's stop detector caches the same +1b8/+1bc samples. */
    p->stick_history[0]=p->stick_history[1]=p->input_mag[0];
}
static void trial_event(Player *p,int action)
{ if(p->trial_event)p->trial_event(p->trial_context,action); }

/* FUN_8c05651c (SH-4 8c05651c..8c056825): recover the rail axis from the
   collision triangle.  Ordinary rail patches select their shortest and
   longest edges, then join those edges' midpoints.  Types in 0x8c10d670
   select the two edges meeting at a near-right-angle vertex; joining their
   midpoints gives the same encoded centre axis.  The player is projected onto
   that infinite axis and the returned direction is the one nearest the
   current heading. */
static int rail_snap(const Collision *col, const ColHit *h, float yaw, float *dir, float *x, float *y, float *z, int *heading)
{
    const ColTri *current,*previous;float v[9];
    if(!col_contact(col,h->tri,&current,&previous))return 0;
    col_contact_vertices(current,v);
    int e0 = 0, e1 = 1, e2 = 0, e3 = 1;
    int directional = h->surf == 15 || h->surf == 20 || h->surf == 22 || h->surf == 58 || h->surf == 60;
    int found = 0;
    if (directional) {
        /* 0x3db27e52 = sin(5 degrees), exactly as loaded at 8c056640. */
        for (int i = 0; i < 3; i++) {
            int j = (i + 1) % 3, k = (i + 2) % 3;
            float ax = v[j*3] - v[i*3], ay = v[j*3+1] - v[i*3+1], az = v[j*3+2] - v[i*3+2];
            float bx = v[k*3] - v[i*3], by = v[k*3+1] - v[i*3+1], bz = v[k*3+2] - v[i*3+2];
#if PLAYER_SOURCE_MOVEMENT
            float av[3]={ax,ay,az},bv[3]={bx,by,bz};
            float al=sqrtf(probe_fipr(av,av)),bl=sqrtf(probe_fipr(bv,bv));
            if(al>1e-6f && bl>1e-6f)for(int k=0;k<3;k++){av[k]*=1.f/al;bv[k]*=1.f/bl;}
            float alignment=probe_fipr(av,bv);
#else
            float al = sqrtf(ax*ax + ay*ay + az*az), bl = sqrtf(bx*bx + by*by + bz*bz);
            float alignment=al>1e-6f && bl>1e-6f ? (ax/al)*(bx/bl) + (ay/al)*(by/bl) + (az/al)*(bz/bl):1.f;
#endif
            if (al > 1e-6f && bl > 1e-6f && fabsf(alignment) < 0x1.64fca4p-4f) {
                e0 = i; e1 = j; e2 = i; e3 = k; found = 1; break;
            }
        }
    }
    if (directional && !found) return 0; /* invalid source patch: never invent an axis */
    if (!directional) {
        float shortest = 10000.f, longest = 0.f;
        for (int i = 0; i < 3; i++) {
            int j = (i + 1) % 3;
            float dx = v[j*3] - v[i*3], dy = v[j*3+1] - v[i*3+1], dz = v[j*3+2] - v[i*3+2];
#if PLAYER_SOURCE_MOVEMENT
            float edge[3]={dx,dy,dz};float d2=sqrtf(probe_fipr(edge,edge));
#else
            float d2 = sqrtf(dx*dx + dy*dy + dz*dz);
#endif
            if (d2 < shortest) { shortest = d2; e0 = i; e1 = j; }
            if (d2 > longest) { longest = d2; e2 = i; e3 = j; }
        }
    }
    float a[3], b[3];
    for (int c = 0; c < 3; c++) { a[c] = (v[e0*3+c] + v[e1*3+c]) * 0.5f; b[c] = (v[e2*3+c] + v[e3*3+c]) * 0.5f; }
    float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
#if PLAYER_SOURCE_MOVEMENT
    /* 05651c uses FIPR normalization (2809b8), then Ninja's FMAC dot
       and projection (2723dc). Mixing these rounding rules moves a rail
       contact by one ULP and can change a later edge/landing decision. */
    float axis[3]={dx,dy,dz},l2=probe_ninja_dot(axis,axis);
#else
    float l2 = dx*dx + dy*dy + dz*dz;
#endif
    if (l2 < 1e-8f || dx*dx+dz*dz < 1e-8f) return 0;
#if PLAYER_SOURCE_MOVEMENT
    float inverse=1.f/sqrtf(probe_fipr(axis,axis));for(int k=0;k<3;k++)dir[k]=axis[k]*inverse;
#else
    float l = sqrtf(l2); dir[0] = dx/l; dir[1] = dy/l; dir[2] = dz/l;
#endif
    int angle = dc_yaw(yaw);
#if PLAYER_SOURCE_MOVEMENT
    float flat[3]={dx,0,dz},inv=1.f/sqrtf(probe_fipr(flat,flat));
    for(int k=0;k<3;k++)flat[k]*=inv;
    float input[3]={-bam_sin(angle),0,-bam_cos(angle)};
    /* The rounded normalized vector does not have an exactly unit dot. */
    float dot=probe_ninja_dot(input,flat)/probe_ninja_dot(flat,flat);
    float hx=source_fmac(flat[0],dot,0.f),hz=source_fmac(flat[2],dot,0.f);
#else
    float hl = sqrtf(dx*dx+dz*dz), nx=dx/hl, nz=dz/hl;
    /* FUN_8c2723dc uses FMAC, whose rounding residual can choose a direction
       at an exactly perpendicular diagonal contact. Preserve its order. */
    float dot = fmaf(-bam_sin(angle),nx,0.f);
    dot = fmaf(-bam_cos(angle),nz,dot);
    float hx = nx*dot, hz = nz*dot;
#endif
    int snapped = hz != 0.f ? bam_atan2(-hx,-hz) : hx < 0.f ? 16384 : hx > 0.f ? -16384 : angle-16384;
    if (heading) *heading = snapped;
    if (-dx*bam_sin(snapped)-dz*bam_cos(snapped) < 0.f) { dir[0] = -dir[0]; dir[1] = -dir[1]; dir[2] = -dir[2]; }
    if (x) {
#if PLAYER_SOURCE_MOVEMENT
        float relative[3];for(int k=0;k<3;k++)relative[k]=h->p[k]-a[k];
        float t=probe_ninja_dot(relative,axis)/l2;
        *x=source_fmac(t,dx,a[0]);*y=source_fmac(t,dy,a[1]);*z=source_fmac(t,dz,a[2]);
#else
        float t = ((h->p[0]-a[0])*dx + (h->p[1]-a[1])*dy + (h->p[2]-a[2])*dz) / l2;
        *x = a[0] + dx*t; *y = a[1] + dy*t; *z = a[2] + dz*t;
#endif
    }
    return 1;
}

/* Denki 0fe4d0 shares 05651c with the player. Its travel angle differs by
   half a turn from the player controller's facing convention. */
int player_wave_surface(const Collision *c,const ColHit *h,int yaw,float point[3],int *heading)
{
    float direction[3];int snapped;
    if(!rail_snap(c,h,yaw/BAM,direction,point,point+1,point+2,&snapped))return 0;
    *heading=(int16_t)(snapped-32768);return 1;
}

/* +0x174 is measured from the contact plane BEFORE rail heading/controls. */
static int movement_pitch(const Player *p)
{
    return p->pitch;
}
static int stair_surface(unsigned surf){return surf==5 || surf==31 || surf==61;}
static int ground_locomotion(const Player *p,int ordinary)
{
    /* 06c34c/06c54e: both stick-held and coasting paths use this test. */
    if(stair_surface(p->surf))return abs((int16_t)(p->ground_heading-dc_yaw(p->yaw)))<16384 ? 10:9;
    /* 06c048 selects the fast ramp stance before ordinary skate/dash.
       The same source surface list and strict speed threshold apply while
       coasting; this changes the pose, not the takeoff condition. */
    if((p->surf==9 || p->surf==10) && p->speed>p->tun->ramp_launch_speed)return 8;
    return ordinary;
}
/* FUN_8c020b00, shared tuning 0x8c17ffc8 +0x70..0x90. Slow rails
   decelerate to zero; slopes can start a stationary skater rolling backwards. */
static float rail_speed(float speed, int pitch)
{
    const float friction = 0x1.a8c536p-8f, slow = 0x1.2f684cp-6f;
    const float cutoff = 0x1.1c71c8p-1f, downhill = 0x1.7b425ep-5f;
    const float limit = 0x1.aaaaaap+1f;
    float dv;
    if (pitch < -546) dv = pitch < -6371 ? downhill : (float)(-pitch-546)*downhill/5825.f;
    else if (pitch > 546) {
        dv = -friction - (pitch > 6371 ? slow : (float)(pitch-546)*slow/5825.f);
        if (speed < cutoff) dv = -friction-slow;
    } else if (speed < 0.f) dv = speed <= -cutoff ? friction : slow;
    else if (speed > 0.f) dv = speed >= cutoff ? -friction : -slow;
    else dv = 0.f;
    float next = speed + dv;
    if ((speed > 0.f && next <= 0.f) || (speed < 0.f && next >= 0.f)) return 0.f;
    return fmaxf(-limit, fminf(limit, next));
}
/* FUN_8c06c8f0. No extra rail-friction pass belongs in the skating law. */
static float ground_speed_rate_scaled(const CharTuning *t, float speed, float mag, int dash, int boost, int pitch, unsigned surf,float *rate,float target_scale,int police_grab,int stumbling)
{
    /* 06c8f0 saturates the recorded stick before deriving target speed. */
    if(mag<.05f)mag=0.f;else if(mag>.9f)mag=1.f;
    int ramp = surf == 10 || surf == 9; /* list 0x8c10d95c */
    /* 06c8f0 selects the temporary stumble speeds while +5d4 is active,
       including a wall-reaction overlay that continues through landing. */
    float target = (police_grab ? (dash ? t->vmax_contact_dash:t->vmax_contact) :
        stumbling ? (dash ? t->vmax_stumble_dash:t->vmax_stumble) :
        (dash ? t->vmax_dash:t->vmax)*target_scale)*mag;
    if (pitch > t->uphill_dead) {
        if (ramp) target -= (float)(pitch-t->uphill_dead)*t->ramp_uphill_speed/(16384-t->uphill_dead);
        else if (pitch > t->uphill_limit) target = -t->uphill_speed;
        else target -= (float)(pitch-t->uphill_dead)*t->uphill_speed/(t->uphill_limit-t->uphill_dead);
    } else if (pitch < -t->downhill_dead) {
        if (ramp) target += (float)(-pitch-t->downhill_dead)*t->ramp_downhill_speed/(16384-t->downhill_dead);
        else if (-pitch > t->downhill_limit) target += t->downhill_speed;
        else target += (float)(-pitch-t->downhill_dead)*t->downhill_speed/(t->downhill_limit-t->downhill_dead);
    }
    float delta = (target-speed)*t->stop_speed/t->max_speed;
    if (rate) *rate=delta<0.f ? 1.f : 1.f+(target-speed)/t->vmax_dash;
    if (delta < 0.f && mag == 0.f && dash) delta *= t->dash_coast;
    if (delta > 0.f && boost) delta *= 3.f; /* shared +0xec */
    speed += delta;
    if (target == 0.f && fabsf(speed) < t->stop_speed) speed = 0.f;
    return fmaxf(-t->max_speed, fminf(t->max_speed, speed));
}
static float ground_speed_rate(const CharTuning *t,float speed,float mag,int dash,int boost,int pitch,unsigned surf,float *rate)
{ return ground_speed_rate_scaled(t,speed,mag,dash,boost,pitch,surf,rate,1.f,0,0); }
static float ground_speed(const CharTuning *t,float speed,float mag,int dash,int boost,int pitch,unsigned surf)
{
    return ground_speed_rate(t,speed,mag,dash,boost,pitch,surf,0);
}
static int turn_cap(const CharTuning *t, float speed, int dash, unsigned surf)
{
    float a, b;
    if (surf == 10) { a = dash ? t->ramp_turn_dash_a : t->ramp_turn_a; b = dash ? t->ramp_turn_dash_b : t->ramp_turn_b; }
    else { a = dash ? t->turn_dash_a : t->turn_a; b = dash ? t->turn_dash_b : t->turn_b; }
    return (int)((a + (1.f-fabsf(speed)/t->max_speed)*b)*1820.f); /* 8c06c308..312 */
}
static int ground_turn(const CharTuning *t, float speed, int dash, unsigned surf, int difference)
{
    int cap = turn_cap(t, speed, dash, surf);
    return difference > cap ? cap : difference < -cap ? -cap : difference;
}
static void start_brake(Player *p)
{
    /* FUN_8c06cbf8: capture a fixed decrement from entry speed; the first
       decrement is on the NEXT tick. Shared +0x14..0x30. */
    float ticks; int motion;
    if (p->speed <= 0.f) { ticks = 6.f; motion = 16; }
    else if (p->speed < 0x1.da12f6p-1f) { ticks = 15.f; motion = 13; }
    else if (p->speed < 0x1.da12f6p+0f) { ticks = 18.f; motion = 12; }
    else if (p->speed < 0x1.638e38p+1f) { ticks = 21.f; motion = 14; }
    else { ticks = 30.f; motion = 14; }
    p->braking = 1; p->brake_step = -p->speed/ticks; p->brake_released = 0;
    set_motion(p, motion);
    if(motion==14)player_sound(p,PLAYER_SOUND_EFFECT,12); /* 06cbf8 -> 05cf68 */
}
static void departure_carry(Player *p)
{
    /* 052c64 includes the independent landing slip, then clears it. */
    memcpy(p->air_surface_motion,p->surface_motion,sizeof p->surface_motion);
    p->air_surface_motion[0]+=bam_sin(p->slip_yaw)*-p->slip_speed;
    p->air_surface_motion[2]+=bam_cos(p->slip_yaw)*-p->slip_speed;
    p->slip_yaw=0;p->slip_speed=p->slip_decay=0;
}
static int takeoff_pitch(const Player *p,int contact)
{
    /* 01be54 (jump flag 0400), using 056ea4's +86c surface list.
       Keep the uphill component; the reverse-facing surface family uses
       the opposite sign. This is independent of the special ramp trick. */
    unsigned s=p->surf;
    int reverse=contact && (s==14 || (s>=46 && s<=52) || s==30 ||
        (s>=32 && s<=38) || (s>=57 && s<=60));
    int uphill=p->pitch>0 ? p->speed>=0.f : p->speed<0.f;
    return uphill!=reverse ? p->pitch:0;
}
static void start_jump(Player *p, const CharTuning *T)
{
    p->air_pitch=takeoff_pitch(p,1);
    departure_carry(p); /* 01bef8 flags 4000 -> 052c64 */
    int n = (int)(T->jump_ticks*T->rise_fraction); float e8 = T->jump_h / (float)(n * (n + 1) / 2);
    p->vy = e8 * n; p->rise_ticks = 0; p->state = 2; p->sub = 0; p->onground = 0; p->grinding = 0;
    p->jump_start_y = p->jump_peak_y = p->y; p->braking = 0;
    p->air_probe_valid = 0;
    p->rise_gravity=e8;
    int nd=(int)(T->jump_ticks*(1.f-T->rise_fraction));
    p->fall_gravity=T->jump_h/(float)(nd*(nd+1)/2);
    p->air_trick=p->trick_link=p->rail_uphill_jump=0;
    p->rail_detached=0;
    p->wall_used=0; /* 01bef8 clears +12c; wall departure explicitly sets it again. */
    p->ramp_active=0;
}
static void start_fall(Player *p, int sub, int motion)
{
    /* 056ea4 clears the surface classification on a missed ground/rail
       query. The old surface ID survives, but must not reverse the
       walk-off pitch as if that contact were still present. */
    p->air_pitch=takeoff_pitch(p,motion!=74);
    /* FUN_8c01bef8 kind 4 (walk-off, flags 0x402) / kind 8 (rail exit,
       flags 0x704): fall profile starts at zero vertical velocity. */
    /* A missed rail contact (kind4/flags402) uses the ordinary airborne
       query and can attach to the next rail. Only an explicit rail exit
       suppresses it; otherwise a small seam incorrectly disables all
       subsequent rail contacts until landing on ordinary ground. */
    p->rail_detached = p->state == 3 && motion!=74;
    if (p->rail_detached && motion==92) {
        /* Kind 8 flags 0x704: +100 queues +/-5 local X, +200 zeros
           forward speed. The offset is consumed by the NEXT move. */
        p->local_shift=p->rail_side ? 5.f : -5.f;p->speed=0.f;
        p->contact_protected=1;
    }
    p->state = 2; p->sub = sub; p->vy = 0.f; p->onground = p->grinding = 0;
    p->jump_held = 0; p->jump_peak_y = p->jump_start_y = p->y;
    p->jump_motion = 74; p->rise_ticks=sub==1 ? 18 : 0;
    p->braking = 0; set_motion(p, motion);
    p->air_probe_valid = 0;
    int n=(int)(p->tun->jump_ticks*(1.f-p->tun->rise_fraction));
    p->fall_gravity=p->tun->jump_h/(float)(n*(n+1)/2);
    /* 01bc20 only replaces fall gravity; rise gravity is not written. */
    p->air_trick=p->trick_link=p->rail_uphill_jump=0;
    p->wall_used=0;
    p->ramp_active=0;
    consume_trick_gesture(p);
}
static void trick_event(Player *p,int kind,int name)
{trick_hud_event(&p->trick_hud,&p->combo,&p->grind_dist,p->chain,kind,name);}
static void air_trick_event(Player *p,int index,int higher,int kind)
{trick_event(p,kind,TRICK_AIR_IDS[p->character_id][!!p->mirrored][index+(higher ? 2:0)]);}
static void ramp_jump(Player *p,int rx,int rz)
{
    p->air_pitch=0; /* kind 3 does not carry the ordinary takeoff pitch */
    departure_carry(p); /* kind 3 flags 4808 */
    /* 01bef8 flags 4808. No ordinary jump boost, minimum forward speed,
       button-shortened arc or stick takeoff turn applies to this mode. */
    float forward[3],up[3];rotated_basis(rx,rz,dc_yaw(p->yaw)+32768,forward,up);
    float vx=forward[0]*p->speed,vy=forward[1]*p->speed,vz=forward[2]*p->speed;
    int choice=player_random(p,2),motion=58+choice;
    air_trick_event(p,choice,1,2); /* 0208bc -> 05af58 */
    int clip=animation_clip(p,MOTION_SPEC[motion][!!p->mirrored].clip,MOTION_SPEC[motion][!!p->mirrored].low_clip);
    int frames=PLAYER_CLIP_FRAMES[p->character_id][clip];
    p->vy=fmaxf(vy,p->tun->ramp_min_vy);
    p->rise_gravity=p->fall_gravity=p->vy/(frames/2);
    /* 272518: project the horizontal velocity onto the launch plane
       through the origin, preserving its full (not flattened) normal. */
    const float *n=p->gn;
    float dot=fmaf(n[0],vx,0.f);dot=fmaf(n[2],vz,dot);
    float norm=fmaf(n[0],n[0],n[1]*n[1]);norm=fmaf(n[2],n[2],norm);
    float scale=-dot/norm,px=fmaf(scale,n[0],vx),pz=fmaf(scale,n[2],vz);
    int heading=bam_atan2(-vx,-vz),normal=bam_atan2(-n[0],-n[2]);
    int axis=(int16_t)(normal+16384);
    p->ramp_axis=abs((int16_t)(axis-heading))<16384 ? axis : (int16_t)(normal-16384);
    p->speed=sqrtf(px*px+pz*pz);
    player_voice(p,PLAYER_VOICE_TRICK); /* 01c1ec: after horizontal projection */
    int steepest=(n[0]==0.f && n[2]==0.f) ? dc_yaw(p->yaw) : normal;
    int target=(int16_t)(steepest+32768),diff=(int16_t)(dc_yaw(p->yaw)-target);
    p->ramp_target=target-diff/2;p->ramp_turn=(int)((float)abs(diff*2)/frames);
    p->ramp_rx=rx;p->ramp_rz=rz;p->ramp_active=1;
    p->state=2;p->sub=0;p->onground=p->grinding=0;p->braking=0;
    p->jump_start_y=p->jump_peak_y=p->y;p->jump_held=p->rise_ticks=0;
    p->wall_used=0;p->air_probe_valid=0;p->air_trick=1;p->trick_link=p->rail_uphill_jump=0;
    set_motion(p,motion);
    trial_event(p,TRIAL_JUMP); /* kind 3 flags 4808 -> 08e860 */
}
static void jump_boost(Player *p,float mag)
{
    /* 01ca38, shared by ordinary, wall and rail takeoffs. */
    const CharTuning *t=p->tun;
    p->speed=fmaxf(-t->jump_max_speed,fminf(t->jump_max_speed,p->speed*t->jump_boost));
    if (mag>.2f && p->speed<t->air_min_speed) p->speed=fminf(t->air_min_speed,p->speed+t->air_min_speed);
}
static int rail_takeoff_turn(int difference,float mag)
{
    /* 01cb60: kind-2 flag 1000 selects its own 45-degree turn,
       with the strict +/-170-degree gate and >.5 stick threshold. */
    if(mag<=.5f || abs(difference)>=30947)return 0;
    return difference>8192 ? 8192:difference< -8192 ? -8192:difference;
}
static void control_jump(Player *p, float mag)
{
    const CharTuning *t=p->tun;
    if (p->state==1 && p->surf==10 && p->pitch>t->ramp_launch_pitch && p->speed>t->ramp_launch_speed) {
        int rx,rz;ground_angles(p,&rx,&rz);ramp_jump(p,rx,rz);consume_trick_gesture(p);return;
    }
    int gesture=trick_gesture(p), old_motion=p->logical_motion,wall=p->state==4,side=p->wall_side;
    /* 01bef8 flag 1000 records an uphill rail departure independently
       of the trick pose. 056e40 consumes this flag on a later recatch. */
    int rail_uphill=p->state==3 && (p->speed>=0.f ? p->pitch>546:p->pitch< -546);
    float speed=fabsf(p->speed);
    int motion=speed>0x1.638e38p+0f ? 62 : speed>0x1.da12f6p-1f ? 65 : speed>0.f ? 68 : 71;
    for (int i=22;i<=42;i+=4) if (old_motion==i) motion=i+1;
    if (wall) motion=side ? 48 : 53; /* 01c490, flags 0x80 */
    start_jump(p,t);
    p->rail_uphill_jump=rail_uphill;
    if (speed>t->trick_speed && p->surf!=9 && p->surf!=10) {
        int choice=player_random(p,2);
        motion=(gesture ? 60 : 56)+choice;
        air_trick_event(p,choice,gesture,gesture ? 3:2); /* 020888 / 0208f0 */
        int frames=PLAYER_CLIP_FRAMES[p->character_id][player_anim(motion)], n=frames/2;
        p->rise_gravity=p->fall_gravity=t->jump_h/(float)(n*(n+1)/2);
        p->vy=p->rise_gravity*n;
        p->speed*=((float)t->jump_ticks/frames);
        p->air_trick=1; p->trick_link=gesture;
        p->jump_motion=62; /* fallback after trick animation completes */
        player_voice(p,PLAYER_VOICE_TRICK); /* 01c352, after clip-duration speed scaling */
    } else {
        if (gesture) {
            trick_event(p,4,0); /* 01c340 -> 05aff4: original 180 label */
            motion=wall ? (side ? 49 : 54) : 17;
            if (!wall) for (int i=22;i<=42;i+=4) if (old_motion==i) motion=i+2;
            p->trick_link=1;
        }
        p->jump_motion=wall ? 74 : motion;
        player_voice(p,PLAYER_VOICE_JUMP); /* 01c5de, before takeoff boost */
    }
    jump_boost(p,mag);
    set_motion(p,motion);
    consume_trick_gesture(p);
    trial_event(p,TRIAL_JUMP); /* kinds 0/1/2 all include flag 800 */
}
static int falling_motion(int rising)
{
    /* FUN_8c01cc8c case 0; non-trick grind jumps use the normal fall (63),
       not the NEXT grind trick's motion in the table. */
    switch (rising) {
    case 17: case 24: case 28: case 32: case 36: case 40: case 44:
    case 49: case 54: case 62: case 65: case 68: case 71: return rising+1;
    default: return 63;
    }
}
/* FUN_8c020924: grind entry (state 3). Random grind pose from table 0x8c10d440 (FUN_8c01ae30), stop counter 6, speed kept
   (no landing penalty on rails, FUN_8c056e40); a stopped player facing downhill gets 0.0926 */

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_player_c__grind_motions_0.inc"
#else

#include "generated/source_data/player_c__GRIND_MOTIONS_0.inc"

#endif

/* scoring (FUN_8c05b1b4 / FUN_8c05b568): points per trick event = 0x8c183170[combo] x multiplier (grind: 1, or 2 while the rail chain
   counter +0x1fc is set); combo bonus at the end (FUN_8c05b0ac): 0x8c1831c0[combo]; FUN_8c05b00c resets the combo (crash, fall) */
static void combo_bonus(Player *p) { trick_hud_finish(&p->trick_hud,&p->combo,&p->grind_dist);p->chain=0; }
static void combo_reset(Player *p) { trick_hud_reset(&p->trick_hud,&p->combo,&p->grind_dist);p->chain=0; }
void player_reposition(Player *p,const float position[3])
{
    /* 01b2c0 -> 01aeb0 / 06bd04(kind 1). Null angles preserve heading;
       kind 1 zeros speed. Do not reinitialize health, character or bindings.
       The host's fractional scheduler is not a Dreamcast player field. */
    float accumulator=p->tick_acc;
    player_reset_controls(p);p->tick_acc=accumulator;
    p->x=p->px=position[0];p->y=p->py=p->ground_y=position[1];p->z=p->pz=position[2];
    p->speed=p->vy=p->kb=p->push[0]=p->push[1]=0;
    p->logic_tick=0;p->pitch=p->stair_pitch=p->ground_heading=0;p->surf=0;p->gn[0]=p->gn[2]=0;p->gn[1]=1;
    p->state=1;p->sub=0;p->onground=1;p->grinding=0;p->reset_query=1;
    p->rail_side=p->rail_detached=p->stop_ticks=p->air_trick=p->trick_link=0;
    p->grind_dist=p->skate_lean=p->coast_blend=0;
    p->jump_start_y=p->jump_peak_y=p->y;
    combo_bonus(p); /* 06cd20 -> 05b0ac, not a health loss or score reset */
    player_cam_reset(p);
}
static void start_grind(Player *p)
{
    p->ramp_active=0;
    if (p->speed == 0.f && p->pitch < 0) p->speed = 0x1.7b425ep-4f;
    unsigned pose = player_random(p,6);
    p->grind_pose=pose;
    p->state = 3; p->sub = 0; p->grinding = 1; p->onground = 1; p->stop_ticks = 6; p->grind_dist = 0.f; p->vy = 0.f;
    p->grind_voice_ticks=0;player_sound(p,PLAYER_SOUND_EFFECT,9); /* 020924 */
    p->chain = p->trick_link ? 5 : 0;   /* +0x164, not the previous chain countdown */
    set_motion(p, GRIND_MOTIONS[pose]);
}

#define BODY_HITS 96
static int wall_surface(const ColBodyHit *h)
{
    /* FUN_8c055be0: +4d8 (upper probes) and +4dc (foot) have different lists. */
    unsigned s=h->surf;
    return s==1 || s==4 || s==18 || s==55 || s==56 ||
           (h->probe!=2 && (s==2 || s==27));
}
static int body_surface(const ColBodyHit *h,int filter)
{
    unsigned s=h->surf;
    if (filter==0) return wall_surface(h);
    if (filter==2) return s==4;
    if (filter==3 && (s==4 || s==10)) return 0; /* list 0x8c10dadc */
    return s==1 || s==2 || s==4 || s==11 || s==18 || s==27 || s==10 || s==55 || s==56;
}
static void body_translate(Player *p,const float *delta)
{
    p->x+=delta[0];p->y+=delta[1];p->z+=delta[2];
    for (int i=0;i<3;i++) for (int k=0;k<3;k++) p->body_centres[i][k]+=delta[k];
    for (int k=0;k<3;k++) p->air_probe[k]+=delta[k];
}
static int body_separate(Player *p,const ColBodyHit *hits,int n,int filter,float extra,int *headon)
{
    /* FUN_8c055000: merge normals within 20 degrees, retaining maximum
       depth. Resolve each group ONCE, not once per overlapping triangle. */
    float normals[BODY_HITS][2],depth[BODY_HITS];int count=0;
    int yaw=dc_yaw(p->yaw)+(p->speed<0.f ? 32768 : 0);
    *headon=0;
    /* FUN_8c055558 resolves surface 11 vertically before the XZ groups. */
    float vertical[2]={0.f,0.f};
    for (int i=0;i<n;i++) if (hits[i].surf==11 && body_surface(hits+i,filter) && fabsf(hits[i].n[1])>.1f) {
        int side=hits[i].n[1]<0.f;if (vertical[side]<hits[i].depth) vertical[side]=hits[i].depth;
    }
    float delta_y[3]={0.f,vertical[0]-vertical[1],0.f};body_translate(p,delta_y);
    for (int i=0;i<n;i++) {
        const ColBodyHit *h=hits+i;if (!body_surface(h,filter) || h->surf==11) continue;
        float x=h->n[0],z=h->n[2];
#if PLAYER_SOURCE_MOVEMENT
        /* 27a788/2809b8: FIPR length, then one reciprocal and multiply.
           Direct division changes setup replay contacts by a final ULP. */
        float horizontal[3]={x,0.f,z};
        float length=sqrtf(probe_fipr(horizontal,horizontal));
#else
        float length=sqrtf(x*x+z*z);
#endif
        if (length<=.1f) continue;
#if PLAYER_SOURCE_MOVEMENT
        float inverse=1.f/length;x*=inverse;z*=inverse;
#else
        x/=length;z/=length;
#endif
        if (-x*bam_sin(yaw)-z*bam_cos(yaw)<-bam_cos(8192)) *headon=1;
        int j=0;for (;j<count;j++) if (normals[j][0]*x+normals[j][1]*z>0.939700007f) break;
        if (j==count) { normals[j][0]=x;normals[j][1]=z;depth[j]=h->depth;count++; }
        else if (depth[j]<h->depth) depth[j]=h->depth;
    }
    for (int i=0;i<count;i++) {
        float delta[3]={normals[i][0]*(depth[i]+extra),0.f,normals[i][1]*(depth[i]+extra)};body_translate(p,delta);
    }
    /* 055000 publishes +5b4 for the following ground controller. The
       response classifier also consumes this flag; do not discard it. */
    if(*headon)p->contact_headon=1;
    return count;
}
static void ground_contact_momentum(Player *p);
static int wall_tangent(const ColBodyHit *h,const float *motion,const Collision *col)
{
    /* 0569c8 -> 272518 projects an absolute contact + queued motion,
       then subtracts the projected point. Relative-vector projection loses
       the original rounding at large coordinates and changes rail entries. */
    const ColTri *tri,*previous;const float *origin=h->p,*n=h->n;
    if(col && col_contact(col,h->tri,&tri,&previous))origin=tri->v;
    float point[3]={h->p[0]+motion[0],h->p[1],h->p[2]+motion[2]};
    float offset=(-(n[0]*origin[0])-n[1]*origin[1])-n[2]*origin[2];
    float t=-(probe_ninja_dot(n,point)+offset)/probe_ninja_dot(n,n);
    float x=source_fmac(t,n[0],point[0]),z=source_fmac(t,n[2],point[2]);
    return bam_atan2(h->p[0]-x,h->p[2]-z);
}
static int wall_mode(Player *p,const ColBodyHit *hits,int n,const float *motion,const Collision *col)
{
    /* FUN_8c055cb8. Motion is the queued world-space displacement (+d0),
       not a normalized direction: zero and backwards speeds matter. */
    p->contact_speed=-1.f;
    const ColBodyHit *h=NULL;int headon;
    if (p->obstacle_cooldown<1) for (int i=0;i<n;i++) if (hits[i].probe==2 && hits[i].surf==27) {
        ground_contact_momentum(p);
        body_separate(p,hits,n,0,0.f,&headon);p->obstacle_cooldown=30;
        int face=bam_atan2(-hits[i].n[0],-hits[i].n[2]);
        return abs((int16_t)(face-dc_yaw(p->yaw)))<16384 ? 17 : 14;
    }
    for (int i=0;i<n;i++) if (wall_surface(hits+i)) { h=hits+i;break; }
    if (!h) return 8;
    p->contact_surface=h->surf;
    ground_contact_momentum(p);
    p->contact_speed=0x1.284bdap+1f;
    int tangent=wall_tangent(h,motion,col),yaw=dc_yaw(p->yaw);
    int diff=(int16_t)(tangent-yaw),ad=abs(diff);
    int groups=body_separate(p,hits,n,0,0.f,&headon);
    if ((ad<8192 && (groups==1 || !headon)) || p->surf==5 || p->surf==31 || p->surf==61) {
        p->yaw=wrap_pi((tangent+32768)/BAM);return 9;
    }
    if (fabsf(p->speed)<=0x1.284bdap+1f) { p->contact_speed=0x1.da12f6p-1f;return 9; }
    if (ad<10922) {
        p->yaw=wrap_pi((tangent+32768)/BAM);
        return p->speed<0.f ? (diff<=0 ? 11 : 10) : (diff<=0 ? 10 : 11);
    }
    int face=bam_atan2(-h->n[0],-h->n[2]);
    int frontal=abs((int16_t)(face-yaw))<16384;
    p->yaw=wrap_pi((face+(frontal ? 0 : 32768)+32768)/BAM);
    return (frontal ? 15 : 12)+(fabsf(p->speed)>=0x1.638e38p+1f);
}
static void wall_plane(Player *p,const ColBodyHit *h)
{
    memcpy(p->wall_origin,h->p,12);memcpy(p->wall_normal,h->n,12);
}
static float momentum_dot(const float a[3],const float b[3])
{ return (float)((double)a[0]*b[0]+(double)a[1]*b[1]+(double)a[2]*b[2]); }
static int contact_pitch_at(const Collision *col,float yaw,const ColHit *h)
{
    /* 056864 samples the plane one unit behind the world contact, then
       measures its actual rounded XZ distance. The normal-only ratio
       loses those world-coordinate roundings and changes speed/RNG bits. */
    const ColTri *tri,*previous;
    if(!col_contact(col,h->tri,&tri,&previous) || h->n[1]==0.f)return contact_pitch(yaw,h->n);
    int angle=dc_yaw(yaw);const float *n=h->n,*v=tri->v;
    float x=h->p[0]-bam_sin(angle),z=h->p[2]-bam_cos(angle);
    float offset=(-(n[0]*v[0])-n[1]*v[1])-n[2]*v[2];
    float dot=source_fmac(n[0],x,n[1]*h->p[1]);dot=source_fmac(n[2],z,dot);
    float t=-(dot+offset)/n[1],y=source_fmac(t,1.f,h->p[1]);
    float dx=x-h->p[0],dz=z-h->p[2];
    return bam_atan2(y-h->p[1],sqrtf(dx*dx+dz*dz));
}
static float momentum_normalize(float v[3])
{
    float squared=momentum_dot(v,v),length=sqrtf(squared);
    if(squared!=0){float reciprocal=1.f/sqrtf(squared);for(int k=0;k<3;k++)v[k]*=reciprocal;}
    return length;
}
static void ground_contact_momentum(Player *p)
{
    /* 056b0c: ground body contacts and airborne rail attachment consume
       the independent slide, using the tilted forward vector. */
    float f[3],up[3],v[3];ground_basis(p,f,up);
    for(int k=0;k<3;k++)v[k]=f[k]*p->speed;
    v[0]+=bam_sin(p->slip_yaw)*-p->slip_speed;
    v[2]+=bam_cos(p->slip_yaw)*-p->slip_speed;
    float length=momentum_normalize(v);
    p->speed=momentum_dot(v,f)<0 ? -length:length;
    if(p->speed>p->tun->max_speed)p->speed=p->tun->max_speed;
    if(p->speed< -p->tun->max_speed)p->speed=-p->tun->max_speed;
    p->slip_yaw=0;p->slip_speed=p->slip_decay=0;
}
static void landing_momentum(Player *p)
{
    /* 0548dc -> 052d40, BEFORE adopting the landing triangle's yaw.
       Convert departure velocity to a new moving surface's relative speed
       or a separately decaying slip vector. This is not ground friction. */
    int angle=dc_yaw(p->yaw)+32768;
    float forward[3]={bam_sin(angle),0,bam_cos(angle)},velocity[3],surface[3];
    for(int k=0;k<3;k++)velocity[k]=(float)((double)forward[k]*p->speed)+p->air_surface_motion[k];
    float carry_length=sqrtf(momentum_dot(p->air_surface_motion,p->air_surface_motion));
    float length=momentum_normalize(velocity);memcpy(surface,p->surface_motion,12);
    float surface_length=momentum_normalize(surface);p->slip_mode=0;
    if(surface_length>0x1.da12f6p-1f) {
        p->slip_yaw=0;p->slip_speed=p->slip_decay=0;
        if(length<0x1.da12f6p-2f)p->speed=0;
        else {
            float dot=momentum_dot(surface,velocity);
            if(dot>.5f)p->speed=length>surface_length ? length-surface_length:0;
            else {p->speed=0;if(dot>=-.5f)p->slip_mode=8;}
        }
    } else if(carry_length>0) {
        float dot=momentum_dot(velocity,forward);
        p->slip_yaw=bam_atan2(-velocity[0],-velocity[2]);
        if(dot>.5f || dot<-.5f) {
            p->slip_speed=length*(dot>.5f ? 1.f-dot:1.f+dot);
            p->slip_decay=p->slip_speed/15.f;p->speed=length*dot;
        } else {
            p->slip_speed=length;p->slip_decay=length/30.f;p->speed=0;p->slip_mode=9;
        }
    } else {p->slip_yaw=0;p->slip_speed=p->slip_decay=0;}
    memset(p->air_surface_motion,0,sizeof p->air_surface_motion);
}
static void air_contact_momentum(Player *p)
{
    /* 055ff8 -> 056ca0: a body contact consumes the departure vector.
       Add ALL three carry components to rotated forward velocity, choose
       the sign using its dot with forward, then clamp and clear +838.
       Keeping carry alive after contact applies the platform impulse on
       every subsequent airborne tick (not what the original does). */
    int angle=dc_yaw(p->yaw)+32768;
    float forward[3]={bam_sin(angle),0,bam_cos(angle)},velocity[3];
    for(int k=0;k<3;k++)velocity[k]=(float)((double)forward[k]*p->speed)+p->air_surface_motion[k];
    float length=sqrtf((float)((double)velocity[0]*velocity[0]+(double)velocity[1]*velocity[1]+(double)velocity[2]*velocity[2]));
    float dot=(float)((double)velocity[0]*forward[0]+(double)velocity[1]*forward[1]+(double)velocity[2]*forward[2]);
    p->speed=dot<0 ? -length:length;
    if(p->speed>p->tun->max_speed)p->speed=p->tun->max_speed;
    if(p->speed< -p->tun->max_speed)p->speed=-p->tun->max_speed;
    memset(p->air_surface_motion,0,sizeof p->air_surface_motion);
}
static int air_wall_mode(Player *p,const ColBodyHit *hits,int n,const float *motion,const Collision *col)
{
    /* FUN_8c055ff8; airborne modes 0..7 are NOT ground modes 8..17. */
    const ColBodyHit *h=NULL;int ignored;
    for (int i=0;i<n;i++) if (body_surface(hits+i,1)) { h=hits+i;break; }
    if (!h) return 0;
    p->contact_surface=h->surf;
    int yaw=dc_yaw(p->yaw);
    air_contact_momentum(p);
    int tangent=wall_tangent(h,motion,col),diff=(int16_t)(tangent-yaw),ad=abs(diff);
    float away=(p->wall_origin[0]-p->x)*p->wall_normal[0]+(p->wall_origin[2]-p->z)*p->wall_normal[2];
    if (h->surf==4 && p->y>p->jump_start_y && (!p->wall_used || away<0.f) && ad<14563) {
        body_separate(p,hits,n,1,0.f,&ignored);p->yaw=wrap_pi((tangent+32768)/BAM);
        int side=(int16_t)(bam_atan2(h->n[0],h->n[2])-tangent)>0;
        p->wall_side=p->speed<0.f ? !side : side;
        /* 055ff8 publishes the accepted wall height (+bc -> +fc) before
           returning mode 18. The following camera keeps this height after
           wall departure, rather than the previous rail's ground height. */
        p->ground_y=p->y;wall_plane(p,h);return 18;
    }
    if (ad<8192) {
        if (body_separate(p,hits,n,1,0.f,&ignored)==1) p->yaw=wrap_pi((tangent+32768)/BAM);
        return 1;
    }
    if (fabsf(p->speed)<3.0863888263702393f) {
        if (body_separate(p,hits,n,1,0.f,&ignored)) { p->gn[0]=p->gn[2]=0.f;p->gn[1]=1.f; }
        return 1;
    }
    if (ad<10922) {
        p->yaw=wrap_pi((tangent+32768)/BAM);body_separate(p,hits,n,1,0.f,&ignored);
        return p->speed<0.f ? (diff<=0 ? 3 : 2) : (diff<=0 ? 2 : 3);
    }
    body_separate(p,hits,n,1,0.f,&ignored);
    int face=bam_atan2(-h->n[0],-h->n[2]);
    int frontal=abs((int16_t)(face-yaw))<16384;
    p->yaw=wrap_pi((face+(frontal ? 0 : 32768)+32768)/BAM);
    return (frontal ? 6 : 4)+(fabsf(p->speed)>=3.7037036418914795f);
}
static int body_sweep_impl(Player *p,const Collision *col,int airborne,const float *old_pos,
                      const float *new_pos,const float *up,ColBodyHit *hits)
{
    int count=0;
    for (int probe=0;probe<3;probe++) {
        float next[3],old[3];
        float height=p->state==4 ? p->tun->body_wall[probe] : p->state==3 ? p->tun->body_rail[probe] : airborne ? p->tun->body_air[probe] : p->tun->body_ground[probe];
        for (int k=0;k<3;k++) {
            /* 0535e0 transforms the body probes in the same takeoff
               frame as the landing ray. Flattening them on a pitched
               jump changes wall penetration and the following rail hit. */
            float offset=airborne && !p->ramp_active && !p->air_pitch ?
                (k==1 ? height : 0.f) : height*up[k];
            next[k]=new_pos[k]+offset;
            old[k]=p->body_valid ? p->body_centres[probe][k] : old_pos[k]+offset;
        }
        float radius=p->state==4 && probe<2 ? p->tun->wall_radius : p->tun->body_radius;
        int n=col_body(col,old,old,next,radius,p->state==4 ? 2 : airborne ? 3 : 2,hits+count,BODY_HITS-count);
        for (int i=0;i<n;i++) {
            hits[count+i].probe=probe;
            if(p->surface_contact)p->surface_contact(p->surface_contact_context,hits[count+i].tri);
        }
        count+=n;memcpy(p->body_centres[probe],next,sizeof next);
    }
    p->body_valid=1;
    /* FUN_8c0b5e7c: +4d8 combines the first two probes in ascending depth;
       +4dc is a separate sorted foot list. Equal-depth order is stable. */
    for (int i=1;i<count;i++) {
        ColBodyHit h=hits[i];int j=i;
        while (j>0 && ((hits[j-1].probe==2 && h.probe!=2) ||
            ((hits[j-1].probe==2)==(h.probe==2) && hits[j-1].depth>h.depth))) {
            hits[j]=hits[j-1];j--;
        }
        hits[j]=h;
    }
    return count;
}
static int body_sweep(Player *p,const Collision *col,int airborne,const float *old_pos,
                      const float *new_pos,const float *up,ColBodyHit *hits)
{unsigned t=player_clock();int n=body_sweep_impl(p,col,airborne,old_pos,new_pos,up,hits);player_profile.body_us+=player_clock()-t;return n;}

static void recovery_brake(Player *p)
{
    /* FUN_8c06c000. Reaching zero does NOT cause a recovery jump. */
    float before=p->speed;p->speed+=p->kb;
    if ((before>0.f && p->speed<=0.f) || (before<0.f && p->speed>=0.f)) p->speed=p->kb=0.f;
}
static void air_landing(Player *p,int mode,float impact_vy)
{
    /* 01bc94 -> 06bd04 -> table 8c1b4b80. Keep the landing animation
       protected from the next idle/skate selection, not from player input. */
    if (p->sub==4 && p->rise_ticks<=0) {
        /* 03d2f8: severe fall is a full recovery, not a normal landing. */
        p->state=6;p->sub=0;p->kb=-p->speed/18.f;p->rail_detached=1;
        p->vy=impact_vy; /* ground recovery does not consume or clear +e4 */
        recovery_brake(p);combo_reset(p);
        p->health-=p->tun->fall_damage;if (p->health<0) p->health=0;
        player_voice(p,PLAYER_VOICE_DAMAGE); /* 03d2f8 -> 042bac(...,1) */
        set_motion(p,77);
        player_sound(p,PLAYER_SOUND_EFFECT,7); /* 01bc94 -> 05ce50 */
    } else {
        /* Native setup supplies 05cdf8's external surface selector. */
#ifdef PLAYER_LANDING_SOUND
        PLAYER_LANDING_SOUND(p);
#endif
        float drop=p->jump_peak_y-p->y;
        int motion=p->sub==4 ? 75 : mode==10 ? 96 : mode==11 ? 97 :
            drop<5.f ? 0 : drop<20.f ? 70 : drop<40.f ? 67 : 64;
        int ramp_landing=p->ramp_active && (p->surf==9 || p->surf==10);
        if (ramp_landing) { p->speed=fabsf(impact_vy);motion=75; }
        else if(p->slip_mode==8)motion=75; /* 01bd8e -> 06ce44 */
        else if(p->slip_mode==9)motion=78; /* 01bd8e -> 06ce6c */
        p->state=1;p->vy=0.f;p->braking=p->brake_released=0;p->boost=p->boost_cool=0;
        p->brake_forced=0;
        if(!ramp_landing && p->slip_mode==9) {
            p->braking=p->brake_forced=1;p->brake_step=-p->speed/30.f; /* shared 17ffc8+34 */
        }
        p->rail_detached=0;p->contact_protected=motion!=0 && motion!=78;set_motion(p,motion);combo_bonus(p);
    }
    p->ramp_active=0;p->grinding=0;
}
static void wall_recovery(Player *p,int mode)
{
    /* Ground dispatch 0x8c1b4bd4 -> 03c778/03c82c/03c8e8/03c974/
       03ca24/03cb00. Separate from an enemy attack's fixed push state. */
    if(mode!=14 && mode!=17)player_voice(p,PLAYER_VOICE_COLLISION); /* 06d3d4/400/4dc/508 */
#ifdef PLAYER_COLLISION_SOUND
    if(mode!=14 && mode!=17)PLAYER_COLLISION_SOUND(p,mode==13 || mode==16);
#endif
    p->grinding=0;p->braking=0;p->rail_detached=1;p->ramp_active=0;combo_reset(p);
    int motion;
    if (mode==12 || mode==13) {
        p->kb=p->speed/18.f;p->speed=-p->speed;
        motion=mode==13 || p->surf==10 ? 83 : 79;
    } else if (mode==15 || mode==16) {
        p->kb=-fabsf(p->speed)/18.f;p->speed=fabsf(p->speed);motion=82;
    } else { p->kb=-p->speed/18.f;motion=mode==14 ? 80 : 81; }
    if (mode==13 || mode==16) { p->health-=p->tun->wall_damage;if (p->health<0) p->health=0; }
    p->state=6;p->sub=0;recovery_brake(p);
    if(mode==13 || mode==16)player_voice(p,PLAYER_VOICE_DAMAGE); /* 042bac after recovery's speed update */
    if(mode==14 || mode==17)player_voice(p,PLAYER_VOICE_COLLISION); /* 06d42c/534 calls AFTER recovery */
    set_motion(p,motion); /* adapter's pose-heading handoff follows voice RNG */
}
static void recovery_fall(Player *p)
{
    /* FUN_8c03d5ac -> 01bc20. This is only a LOST-GROUND transition.
       The source ignores its height argument and uses char +c0, with an
       initial vertical velocity of zero and half the recovery duration. */
    int flags=MOTION_SPEC[p->logical_motion][!!p->mirrored].flags;
    int n=p->tun->recovery_ticks>>1;
    p->sub=1;p->vy=0.f;p->onground=p->grinding=0;
    p->fall_gravity=p->tun->jump_h/(n*(n+1)/2.f);p->air_probe_valid=0;
    set_motion(p,(flags&0x100) ? 85 : 88);
}
static void start_wall_ride(Player *p)
{
    /* FUN_8c01d740: horizontal, constant-speed ride, 15 countdown ticks. */
    player_sound(p,PLAYER_SOUND_EFFECT,18); /* 01d740 -> 05ce44 */
    /* The source writes +80=3 (motion type), NOT +204 (air substate). */
    p->state=4;p->vy=0.f;p->rise_ticks=15;
    p->ramp_active=0;
    p->onground=p->grinding=0;p->grind_dist=0.f;
    p->overlay_ticks=0;p->overlay_blend=0.f;
    set_motion(p,p->wall_side ? 46 : 51);
}
static int wall_ride_mode(Player *p,const ColBodyHit *hits,int n,const float *motion,const Collision *col)
{
    /* FUN_8c0563b8: tight inside-corner gate and a different body radius.
       A wall ride never attaches to an arbitrary 'vertical enough' patch. */
    const ColBodyHit *h=NULL;int ignored;
    for (int i=0;i<n;i++) if (body_surface(hits+i,1)) { h=hits+i;break; }
    if (h && h->surf==4) {
        int tangent=wall_tangent(h,motion,col),diff=(int16_t)(tangent-dc_yaw(p->yaw));
        if ((!p->wall_side && diff>=-910 && diff<5461) || (p->wall_side && diff<=910 && diff>-5461)) {
            body_separate(p,hits,n,2,p->tun->wall_distance-p->tun->wall_radius,&ignored);
            if (body_separate(p,hits,n,3,0.f,&ignored)==0) {
                p->yaw=wrap_pi((tangent+32768)/BAM);wall_plane(p,h);return 18;
            }
            return 0;
        }
    }
    if (h) body_separate(p,hits,n,1,0.f,&ignored);
    return 0;
}
static void leave_wall(Player *p,int jump,int collision,float mag,float input_yaw)
{
    /* FUN_8c01bef8 kinds 1/5/7, flags 0x80: remember departure plane and
       queue the source three-unit lateral offset. */
    int side=p->wall_side;
    float origin[3]={p->x,p->y,p->z};
    if (jump) {
        control_jump(p,mag);p->jump_held=1;
        /* FUN_8c01cbe8: only turn away from the wall, up to 45 degrees. */
        int turn=(int16_t)(dc_yaw(input_yaw)-dc_yaw(p->yaw));
        /* Here turn is target-old; 01a8a8 supplies the opposite sign
           and 01cbe8 subtracts it. Controller bytes use camera-angle. */
        if ((!side && turn<0) || (side && turn>0)) turn=0;
        if (mag>.5f && abs(turn)<30947) p->yaw=wrap_pi(p->yaw+fmaxf(-8192.f,fminf(8192.f,turn))/BAM);
    } else {
        start_fall(p,collision ? 2 : 1,collision ? 92 : side ? 47 : 52);
        p->contact_protected=collision;
    }
    p->wall_used=1;memcpy(p->wall_origin,origin,sizeof origin);
    p->local_shift+=side ? -3.f : 3.f; /* +844 is consumed by the NEXT move */
}
static void wall_ride_control(Player *p,int jump,float mag,float input_yaw)
{
    /* FUN_8c01d88c, contact mode 18 ONLY. Jump precedes distance/countdown;
       otherwise the final tick still contributes distance before release. */
    if (jump) { leave_wall(p,1,0,mag,input_yaw);return; }
    p->grind_dist+=fabsf(p->speed);
    if (p->grind_dist>10.f) {
        trick_event(p,6,0); /* 05afe8: no rail-chain multiplier/decrement */
    }
    if (--p->rise_ticks<0) leave_wall(p,0,0,mag,input_yaw);
}
static void damage_clocks(Player *p)
{
    if(p->stumble_ticks>0)p->stumble_ticks--;
    if(p->recovery_flash_ticks>0)p->recovery_flash_ticks--;
    /* 03d478/03d504 retain the damage grace period until recovery ends. */
    if(p->vehicle_recovery && p->state==6 && p->health>0)p->damage_invincible=120;
    else p->vehicle_recovery=0;
    if(p->burn_ticks>0)p->burn_ticks--;
    if(p->damage_invincible>0) {
        if(--p->damage_invincible==0 && police_grip_count(&p->police_grips))p->police_grab_damage_clock=60;
    } else if(police_grip_count(&p->police_grips) && p->police_grab_damage_clock>0 && --p->police_grab_damage_clock==0) {
        /* 0198f8 -> 042bac: the character's +24 damage and 120 ticks of
           protection; this is not the bullet overlay reaction. */
        p->health-=p->tun->grab_damage;if(p->health<0)p->health=0;
        trick_hud_reset(&p->trick_hud,&p->combo,&p->grind_dist);
        if(p->health>0)p->damage_invincible=120;
        player_voice(p,PLAYER_VOICE_DAMAGE);
    } else if(!police_grip_count(&p->police_grips))p->police_grab_damage_clock=0;
}

static void player_water_begin(Player *p)
{
    /* 03d3a0. Water is independent of enemy-hit invulnerability. The
       nine-word payload is selected by the original Kogane area table. */
    WaterRecovery *w=&p->water;w->region=w->pending-1;w->pending=0;
    w->phase=1;w->countdown=30;w->alpha=0;w->fade_step=0x1.111112p-5f;
    w->splash=w->splash_started=1;w->splash_frame=0;
    w->splash_position[0]=p->px;w->splash_position[1]=KOGANE_WATER_HEIGHT[w->region==1 ? 1:0];w->splash_position[2]=p->pz;
    combo_reset(p);p->health-=p->tun->water_damage;
    if(p->health<=0){p->health=0;w->phase=4;w->fade_step=0;}
    else p->damage_invincible=120;
    p->state=7;p->sub=0;p->grinding=0;p->onground=0;
    p->small_spray_active=0;p->overlay_ticks=0;p->overlay_blend=0;
    set_motion(p,90);
}
static void player_water_step(Player *p)
{
    WaterRecovery *w=&p->water;
    const WaterReturn *r=KOGANE_WATER_RETURN+w->region;
    p->px=p->x;p->py=p->y;p->pz=p->z;
    if(w->phase==1) {
        /* 03d8c8: exactly 31 constant-displacement updates. There is no
           floor/body query while sinking or during the get-up animation. */
        p->x+=r->delta[0];p->y+=r->delta[1];p->z+=r->delta[2];
        if(--w->countdown<0) {
            WaterRecovery saved=*w;
            player_reposition(p,r->position);*w=saved;
            p->yaw=wrap_pi((r->angles[1]+32768)/BAM);player_cam_reset(p);
            p->state=7;p->sub=1;p->reset_query=0;
            set_motion(p,91);w->phase=2;w->alpha=1.f;w->fade_step=-0x1.111112p-5f;
        }
    } else if(w->phase==2) {
        /* 03d9e4 -> 05cff4 -> bank16 program40, then 03da04. */
        w->recover_started=1;w->phase=3;
    } else if(w->phase==3 && p->motion_done) {
        w->phase=0;p->state=1;p->sub=0;set_motion(p,MOT_IDLE);
    }
}

/* one 30 Hz game tick */
static void player_portal_translate(Player *p)
{
    /* 01a8e8: shift cached contact points along with position. Keep speed,
       animation, jump/grind state and control history through the passage. */
    const float *d=p->portal_translation;
    if(d[0]==0.f && d[1]==0.f && d[2]==0.f)return;
    p->x+=d[0];p->y+=d[1];p->z+=d[2];
    p->px+=d[0];p->py+=d[1];p->pz+=d[2];
    p->ground_y+=d[1];p->jump_start_y+=d[1];p->jump_peak_y+=d[1];
    for(int k=0;k<3;k++) {
        p->air_probe[k]+=d[k];p->wall_origin[k]+=d[k];p->car_pull.reference[k]+=d[k];
        for(int i=0;i<3;i++)p->body_centres[i][k]+=d[k];
    }
    p->cam_ex+=d[0];p->cam_ez+=d[2];
}
static void player_tick_impl(Player *p, const Collision *col, float ax, float ay, int a_held, int a_press, int dash, int dash_press, float cam_yaw)
{
    memset(p->portal_translation,0,sizeof p->portal_translation);
    if(!p->reset_query && p->state!=10)memcpy(p->portal_translation,p->portal_pending,sizeof p->portal_translation);
    memset(p->portal_pending,0,sizeof p->portal_pending);
    p->water.splash_started=p->water.recover_started=0;
    if(p->state==10) {
        /* 06d8cc holds the completed pose. Inputs and collision must not
           replace the celebration with a skate, jump or fall animation. */
        if(!p->motion_done)animation_tick(p);
        if(p->camera_tick)p->cam_yaw=p->camera_tick(p->camera_context);
        effects_tick(p);if(p->recovery_flash_ticks>0)p->recovery_flash_ticks--;p->logic_tick++;return;
    }
    p->dash_held=!!dash;
    p->contact_headon=0; /* 0198f8 clears +5b4 before contact resolution. */
    p->score_add+=trick_hud_tick(&p->trick_hud,player_fsca_sin);
    p->practice_event=0; /* 0feb64, before the movement handler */
    int handler_state=p->state,entered_fall_grace=p->sub==4,fall_threshold_check=0;
    if(p->water.phase){player_water_step(p);goto tick_finish;}
    int reset_query=p->reset_query;p->reset_query=0;
    /* 055954 consumes the previous 056efc mask-8 ray. Water is an effect
       contact, never a body obstacle or a replacement for the solid floor. */
    p->sound_contact_override=!reset_query && p->shallow_pending;
    p->shallow_pending=0;
    if(p->fire_pending && !reset_query)player_fire_hit(p);
    p->fire_pending=0;
    const CharTuning *T = p->tun ? p->tun : &BEAT_TUNING;
    float mag = sqrtf(ax * ax + ay * ay); if (mag > 1.f) mag = 1.f;
    for (int i=8;i>0;i--) { p->input_mag[i]=p->input_mag[i-1]; p->input_yaw[i]=p->input_yaw[i-1]; }
    p->input_mag[0]=mag; p->input_yaw[0]=dc_yaw(cam_yaw+atan2f(ax,-ay));
    int released_stick = mag <= 0.05f && (p->stick_history[0] > 0.5f || p->stick_history[1] > 0.5f);
    p->stick_history[1] = p->stick_history[0]; p->stick_history[0] = mag;
    /* 06c048 compares 0277a0 with float 0x3d4ccccd at 06c0bc.
       Nonzero samples <= .05 are released input, not another steering
       tick. The original tutorial includes a 10/255 release sample. */
    int stick = mag > 0.05f;
    if(stick)p->brake_contact=0; /* 06c048 clears +21c on held input. */
    /* Boost and cooldown belong to the ground callback (06bdf0). Other
       movement handlers retain them, including the airborne portion of a
       boosted jump; process the counters after the ground control below. */
    p->px = p->x; p->py = p->y; p->pz = p->z;
    /* order per tick (trace16): move with the current yaw / speed, camera, then control (turn, accelerate, jump) */
    /* horizontal step (speed is per tick) with the values the tick started with (ground / air states) */
    float forward[3], up[3];
    float facing=p->yaw;p->yaw-=p->queued_root_yaw/BAM;
    int travel_yaw=dc_yaw(p->yaw)+32768;
    ground_basis(p,forward,up);p->yaw=facing;p->queued_root_yaw=0;
    int air_query = p->state == 2 || (!p->onground && !p->grinding);
    /* 0535e0 queues Ry(travel yaw) * Rx(-208). Uphill takeoffs use
       this frame for BOTH velocity and probes, not a world-vertical hop.
       Ramp tricks have their own axis/tilt path. */
    int air_pitch=air_query && !p->ramp_active ? p->air_pitch:0;
    float air_sin=0.f,air_cos=1.f;
    if(air_pitch) {
#ifdef PLAYER_SOURCE_MOVEMENT
        air_sin=-bam_sin(-air_pitch);air_cos=bam_cos(-air_pitch);
#else
        air_sin=bam_sin(air_pitch);air_cos=bam_cos(air_pitch);
#endif
        int a=travel_yaw;
        up[0]=-bam_sin(a)*air_sin;up[1]=air_cos;up[2]=-bam_cos(a)*air_sin;
    }
    /* 053048/0538fc move the entire ground/rail query origin by the last
       contact's +82c vector. 0535e0 instead uses the departure copy +838. */
    const float *moving=air_query ? p->air_surface_motion:p->surface_motion;
    p->x+=moving[0];p->y+=moving[1];p->z+=moving[2];
    if(!air_query && p->state!=3) {
        p->x+=bam_sin(p->slip_yaw)*-p->slip_speed;
        p->z+=bam_cos(p->slip_yaw)*-p->slip_speed;
        if(p->slip_speed>0)p->slip_speed-=p->slip_decay;
        if(p->slip_speed<0)p->slip_speed=0;
    }
    int recovering=p->state==6,wall_riding=p->state==4,finished_motion=p->motion_done;
    int landing=0,grind_entry=0,rail_landing=0;float impact_vy=p->vy;
    if (p->contact_protected && finished_motion) p->contact_protected=0;
    if (p->obstacle_cooldown>0) p->obstacle_cooldown--;
#ifdef PLAYER_SOURCE_MOVEMENT
    /* 053048 translates the rotated query frame, first by the local
       correction and then by speed. FTRV rounds each matrix translation
       once; rounding product and sum separately changes slope contacts. */
    int matrix_ground=!air_query && !p->ramp_active && p->state!=7 && p->state!=9;
    if(matrix_ground && p->local_shift!=0.f) {
        float right[3],unused[3];int rx,rz;ground_angles(p,&rx,&rz);
        rotated_basis(rx,rz,travel_yaw+16384,right,unused);
        p->x=source_fmac(right[0],p->local_shift,p->x);
        p->y=source_fmac(right[1],p->local_shift,p->y);
        p->z=source_fmac(right[2],p->local_shift,p->z);p->local_shift=0.f;
    }
#endif
    float probe_a[3];
    float old_pos[3] = {p->x,p->y,p->z};
    for (int k=0;k<3;k++) probe_a[k] = air_query ?
        (p->air_probe_valid ? p->air_probe[k] : old_pos[k]+(air_pitch ? up[k]*T->air_up:(k==1 ? T->air_up : 0.f))) :
        old_pos[k]+(p->state==3 ? (k==1 ? T->rail_up:0.f):up[k]*T->ground_up);
#ifdef PLAYER_SOURCE_MOVEMENT
    if(!air_query && p->state!=3)for(int k=0;k<3;k++)
        probe_a[k]=source_fmac(up[k],T->ground_up,old_pos[k]);
#endif
    /* Air queries, like ground queries, were queued before 03ed98 moved
       outgoing pose yaw into the actor heading. */
    float dx = bam_sin(travel_yaw) * p->speed, dz = bam_cos(travel_yaw) * p->speed;
    float air_dy=p->vy;
    if(air_pitch) {
        float horizontal=air_cos*p->speed-air_sin*p->vy;
        dx=bam_sin(travel_yaw)*horizontal;
        dz=bam_cos(travel_yaw)*horizontal;
        air_dy=air_sin*p->speed+air_cos*p->vy;
    }
    if (!air_query) { dx=forward[0]*p->speed; dz=forward[2]*p->speed; p->y+=forward[1]*p->speed; }
#ifdef PLAYER_SOURCE_MOVEMENT
    if(matrix_ground)p->y=source_fmac(forward[1],p->speed,old_pos[1]);
#endif
    if (p->ramp_active) { dx=-bam_sin(p->ramp_axis)*p->speed;dz=-bam_cos(p->ramp_axis)*p->speed; }
#ifdef PLAYER_SOURCE_MOVEMENT
    float source_air_shift=p->local_shift;
#endif
    if (p->local_shift!=0.f) {
        int a=travel_yaw;
        dx+=bam_cos(a)*p->local_shift;dz-=bam_sin(a)*p->local_shift;p->local_shift=0.f;
    }
    if (p->state == 9) {
        dx=sinf(p->yaw)*p->speed;dz=cosf(p->yaw)*p->speed;
        if(p->police_knockdown==1){float prev=p->speed;p->speed+=p->kb;if(prev*p->speed<=0)p->speed=p->kb=0;}
        else if(!p->police_knockdown)p->speed*=.9f;
    }
    if (p->state == 7) {
        dx=dz=0.f;
        if (p->sub==0) {
            dx=p->push[0];dz=p->push[1];
            if (--p->stun<0) { p->sub=1;p->speed=0.f;set_motion(p,91); }
        } else if (p->motion_done) { p->state=1;set_motion(p,MOT_IDLE); }
        /* Legacy NPC push adapter. Authored water recovery uses the
           separate 03d8c8 path above, including the safe-position restore. */
    }
#ifdef PLAYER_SOURCE_MOVEMENT
    if(matrix_ground){p->x=source_fmac(forward[0],p->speed,p->x);p->z=source_fmac(forward[2],p->speed,p->z);}
    else
#endif
    {p->x += dx;p->z += dz;}
    /* Consume the already queued airborne displacement. Its velocity and
       phase are advanced only if contact dispatch selects air control. */
    if (p->state == 2) {
        p->y+=air_dy;
        /* 01baa8 updates the held-button latch before contact dispatch. */
        p->jump_held=p->jump_held && a_held;
    }
    else if (recovering && p->sub==1) { p->y+=air_dy;p->vy=fmaxf(-T->fall_max,p->vy-p->fall_gravity); }
    else if (!wall_riding && !p->onground && !p->grinding) { p->y += air_dy; p->vy -= 0.1988f; }
#ifdef PLAYER_SOURCE_MOVEMENT
    if(handler_state==2 && !p->ramp_active){
        float next[3];source_air_translation(old_pos,travel_yaw,air_pitch,source_air_shift,p->speed,impact_vy,next);
        p->x=next[0];p->y=next[1];p->z=next[2];
    }
#endif
    if(p->water.pending && !reset_query && (handler_state==2 || (recovering && p->sub==1))) {
        player_water_begin(p);goto tick_finish;
    }
    p->water.pending=0;
    /* FUN_8c053048 / 0535e0: first sweep the previous upper point to the
       new upper point, THEN test upper-to-lower. Airborne lower = feet,
       not five units below them. Ground probe depth depends on the previous
       surface. No global n.y cutoff and no ground-state rail magnet. The
       special surface-12 fan, lateral slip and moving
       object transforms still need their own source-backed integration. */
    float probe_b[3],probe_c[3],new_pos[3]={p->x,p->y,p->z};
    ColBodyHit body[BODY_HITS];
    int body_count=reset_query ? 0:body_sweep(p,col,air_query,old_pos,new_pos,up,body);
    if(p->car_pull.active && handler_state==1)body_count=0; /* prior +4ec suppresses reaction dispatch */
    for (int k=0;k<3;k++) {
        probe_b[k]=new_pos[k]+(air_query ? (p->ramp_active || air_pitch ? up[k]*T->air_up : (k==1 ? T->air_up : 0.f)) : up[k]*T->ground_up);
        probe_c[k]=new_pos[k]+(air_query ? 0.f : up[k]*ground_probe_down(p,T));
#ifdef PLAYER_SOURCE_MOVEMENT
        if(air_query && handler_state==2 && !p->ramp_active)
            probe_b[k]=source_fmac(air_pitch ? up[k]:(k==1 ? 1.f:0.f),T->air_up,new_pos[k]);
        if(!air_query){
            probe_b[k]=source_fmac(up[k],T->ground_up,new_pos[k]);
            probe_c[k]=source_fmac(up[k],ground_probe_down(p,T),new_pos[k]);
        }
#endif
        if (wall_riding) {
            probe_b[k]=new_pos[k]+(k==1 ? T->wall_up : 0.f);
            probe_c[k]=new_pos[k]+(k==1 ? T->wall_down : 0.f);
        }
    }
    if (air_query) { memcpy(p->air_probe,probe_b,sizeof probe_b); p->air_probe_valid=1; }
    ColHit g;
    unsigned probe_mask = air_query && !p->rail_detached ? 0x85 : 0x81;
    if (p->state==3) {
        /* FUN_8c0526bc -> 0538fc uses character +1f4, mask 5.
           Reusing the ground mask discarded rail-only (bit 4) triangles
           on the very next tick after attaching. */
        probe_mask=5;
        /* 0538fc adds the rail-query offsets in world axes after the
           movement transform. Tilting the ray with the previous rail's
           normal selects the wrong contact at sloping rail junctions. */
        for (int k=0;k<3;k++) { probe_b[k]=new_pos[k]+(k==1 ? T->rail_up:0.f);probe_c[k]=new_pos[k]+(k==1 ? T->rail_down:0.f); }
    }
    if (wall_riding) probe_mask=0x81;
    unsigned ground_started=player_clock();
    int hit = !reset_query && ((!wall_riding && col_probe_filtered(col,probe_a,probe_b,probe_mask,contact_accept,p,&g)) ||
              col_probe_filtered(col,probe_b,probe_c,probe_mask,contact_accept,p,&g));
    player_profile.ground_us+=player_clock()-ground_started;
#ifdef PLAYER_QUERY_TRACE
    PLAYER_QUERY_TRACE(p,col,probe_a,probe_b,probe_c,hit,&g);
#endif
    int snapped_yaw=0,lost_ground_mode=0;
    ColHit pitch_contact;if(hit)pitch_contact=g; /* 056864 precedes 05651c's rail projection. */
    if (hit && SURF_IS_RAIL(g.surf)) hit=rail_snap(col,&g,p->yaw,p->rail_dir,&g.p[0],&g.p[1],&g.p[2],&snapped_yaw);
    if (reset_query) {
        /* 0545ac returns ground mode 8 and 051f0c returns no body mode
           while +90 is set. It does NOT publish a new +b8 position:
           053048 registers the next sweep only after the ground control
           callback. Moving here spent the pre-initialization speed once
           before the first query existed (visible in every staff spawn). */
        p->x=p->px;p->y=p->py;p->z=p->pz;
        p->onground=1;
    } else if (hit) {
        if(p->surface_contact)p->surface_contact(p->surface_contact_context,g.tri);
        int was_air = !p->onground && !p->grinding;
        p->pitch=contact_pitch_at(col,p->yaw,&pitch_contact);
        p->stair_pitch=stair_surface(g.surf) ? p->pitch:0;
        if(stair_surface(g.surf))p->pitch=0; /* 0545ac: stairs keep the actor upright */
        p->ground_heading=(g.n[0]!=0.f || g.n[2]!=0.f) ? bam_atan2(-g.n[0],-g.n[2]):dc_yaw(p->yaw);
        p->x=g.p[0]; p->y=p->ground_y=g.p[1]; p->z=g.p[2]; p->vy = 0; p->onground = 1; p->surf = g.surf; p->gn[0] = g.n[0]; p->gn[1] = g.n[1]; p->gn[2] = g.n[2];
        p->air_probe_valid=0;
        p->rail_detached=0;
        float motion[3]={0,0,0};int moving_yaw=0;
        const ColTri *current,*previous;
        int contact_kind=col_contact(col,g.tri,&current,&previous);
#ifdef PLAYER_STATIC_SURFACE_CARRY
        /* Original registered node meshes keep both vertex buffers even
           when stationary. Their barycentric roundtrip can leave a small
           carry that must survive a recorded jump. World-grid triangles
           have no previous buffer. Setup opts in with original strip order. */
        if(contact_kind==1 && current->src){previous=current;contact_kind=2;}
#endif
        if(contact_kind==2) {
            float now[9],before[9];col_contact_vertices(current,now);
            if(previous)col_contact_vertices(previous,before);
            player_surface_motion(now,previous ? before:NULL,g.p,motion,&moving_yaw);
        } else if(col->surface_motion)col->surface_motion(col->surface_context,g.tri,g.p,motion,&moving_yaw);
        if(g.surf==61)memset(motion,0,sizeof motion); /* 0545ac / 10dbe8 */
        memcpy(p->surface_motion,motion,sizeof motion);
        if(air_query)landing_momentum(p);
        if(!SURF_IS_RAIL(g.surf) && moving_yaw)p->yaw=wrap_pi(p->yaw+moving_yaw/BAM);
        if (SURF_IS_RAIL(g.surf)) {
            if(air_query) {
                ground_contact_momentum(p); /* 054a12 before rail heading */
                if(p->rail_uphill_jump && (p->speed>=0.f ? p->pitch>546:p->pitch< -546))
                    p->speed=landing_speed(T,p->speed); /* 056e40 */
            }
            if (p->state!=3) p->rail_side=(int16_t)(dc_yaw(p->yaw)-snapped_yaw)<0;
            p->yaw=wrap_pi((snapped_yaw+32768)/BAM);
            if (p->state != 3 && p->state != 9 && !recovering) { start_grind(p);grind_entry=1;p->body_valid=0; } /* ground mode 19 */
        } else {
            if (p->state == 3) {
                /* 020c6c -> 06bd04(4) installs the ground callback and
                   06cd94 protects motion 70. Ground steering/acceleration
                   starts on the next callback, not this rail-handler tick. */
                p->state=1;p->grinding=0;rail_landing=1;p->contact_protected=1;
                p->boost=p->boost_cool=0; /* 06bd04 resets both ground counters. */
                set_motion(p,MOT_LAND);combo_bonus(p);
            }
            else if (p->state == 2 || wall_riding) { landing=1;p->state=1;p->speed=landing_speed(T,p->speed); }
            else if (recovering && p->sub==1) p->speed=landing_speed(T,p->speed);
            else if (was_air && p->state == 1) { p->speed = landing_speed(T,p->speed); set_motion(p, MOT_LAND); }
        }
    } else {
        if (recovering && p->sub==0) recovery_fall(p);
        if(!air_query)departure_carry(p); /* 052c64 on lost ground */
        if(!air_query && handler_state==1) {
            /* 0547f4..05481e: the missed GROUND query still resolves
               055ff8 against its already queued body lists, after 052c64
               publishes departure carry. Do not postpone this until an
               airborne query, or dispatch the airborne reaction graph:
               06bdf0 maps results 0..7 to the same 06cecc departure. */
            const float motion[3]={dx,0,dz};
            lost_ground_mode=air_wall_mode(p,body,body_count,motion,col);
        }
        /* The original rail-query fall uses the ordinary
           fall profile (+204=1, captured Gum clock 447). Sub 2 is the side exit;
           using it here doubled release gravity and broke Gum's next jump. */
        if (p->state == 3) { start_fall(p, 1, 74); combo_reset(p); }
        if(p->state==1 && lost_ground_mode==18)start_wall_ride(p);
        else if (p->state==1 && p->surf==10) {
            /* 06cecc: a missed ramp query first resets the tilt and forces
               a 15-tick brake, then repeats the ordinary ground query. */
            p->surf=6;p->pitch=0;p->gn[0]=p->gn[2]=0.f;p->gn[1]=1.f;
            p->braking=p->brake_released=1;p->brake_step=-p->speed/15.f;set_motion(p,12);
        } else { if (p->state==1) start_fall(p,1,74);p->onground=0; }
        if (!p->stage_fall_recovery && p->y < -900.f) { p->y = 600.f; p->vy = 0; }
    }
    float queued_motion[3]={dx,0.f,dz};
    p->contact_mode=!reset_query && !hit && handler_state==1 ? lost_ground_mode:8;p->contact_speed=-1.f;
    int block_controls=rail_landing || (!reset_query && !hit && p->state==1); /* new callbacks start next tick */
    if (!hit && wall_riding) {
        p->contact_mode=wall_ride_mode(p,body,body_count,queued_motion,col);block_controls=1;
        if (p->contact_mode==18) wall_ride_control(p,a_press,mag,cam_yaw+atan2f(ax,-ay));
        else leave_wall(p,0,0,mag,cam_yaw+atan2f(ax,-ay));
    } else if (!hit && air_query) {
        if(p->ramp_active) {
            /* 053ff8 queues a separate mask-0x10 vertical guidance ray.
               On a missed landing ray, 0548dc projects only X/Z onto
               that patch and updates +254; this is not a rail landing. */
            const float top[3]={new_pos[0],10000.f,new_pos[2]};
            const float bottom[3]={new_pos[0],-10000.f,new_pos[2]};
            ColHit guide;float direction[3],point[3];int heading;
            if(col_probe(col,top,bottom,0x10,&guide) &&
               rail_snap(col,&guide,(p->ramp_axis+32768)/BAM,direction,point,point+1,point+2,&heading)) {
                p->x=point[0];p->z=point[2];p->ramp_axis=heading;
            }
        }
        p->contact_mode=air_wall_mode(p,body,body_count,queued_motion,col);
        int mode=p->contact_mode;
        if (p->ramp_active && mode>=1 && mode<=7) { p->ramp_active=0;p->speed=0.f; } /* 01d24c..01d610 */
        if (recovering) {
            if (mode>=4 && mode<=7) {
                int flags=MOTION_SPEC[p->logical_motion][!!p->mirrored].flags;
                p->speed=-p->speed;p->kb=-p->kb;set_motion(p,(flags&0x100) ? 87 : 84);
            }
        } else if (mode==18) { start_wall_ride(p);block_controls=1; }
        else if (mode==2 || mode==3) {
            p->speed*=.3f;player_voice(p,PLAYER_VOICE_COLLISION);
#ifdef PLAYER_COLLISION_SOUND
            PLAYER_COLLISION_SOUND(p,0);
#endif
            combo_reset(p);animation_overlay(p,mode==2 ? 93 : 94);
            p->stumble_ticks=p->overlay_ticks;block_controls=1;
        } else if (mode>=4 && mode<=7) {
            if(mode==5 || mode==7)player_voice(p,PLAYER_VOICE_COLLISION); /* 01d4f4/5e8 before full recovery */
            combo_reset(p);p->speed*=mode<6 ? -.3f : (p->speed<0.f && mode==7 ? -.3f : .3f);
            if(mode==4 || mode==6)player_voice(p,PLAYER_VOICE_COLLISION); /* 01d430/550 after bounce */
            if (mode==5 || mode==7) {
                p->state=6;p->kb=-p->speed/18.f;p->rail_detached=1;recovery_fall(p);
                p->health-=T->wall_damage;if (p->health<0) p->health=0;
                player_voice(p,PLAYER_VOICE_DAMAGE);
                set_motion(p,mode==5 ? 84 : 87);
            } else { p->vy=0.f;p->sub=2;p->rise_ticks=0;set_motion(p,92);p->contact_protected=1; }
            block_controls=1;
        }
    }
    if (hit && (p->state==1 || recovering)) {
        p->contact_mode=wall_mode(p,body,body_count,queued_motion,col);
        if (landing && p->contact_mode>=8 && p->contact_mode<=11) {
            air_landing(p,p->contact_mode,impact_vy);block_controls=1;
        } else if (recovering) {
            block_controls=1;
            if (p->sub==1) {
                int flags=MOTION_SPEC[p->logical_motion][!!p->mirrored].flags;
                p->sub=0;p->kb=-p->speed/18.f;set_motion(p,(flags&0x100) ? 86 : 89);
            }
            else if (finished_motion) {
                /* 03d620/03d6e8 return through 06bd04. A fresh dash after
                   recovery must not inherit the pre-impact cooldown. */
                p->state=1;p->boost=p->boost_cool=0;p->vy=0.f;
                p->idle_ticks=p->brake_contact=0;set_motion(p,MOT_IDLE);
            }
            else if (p->surf==10) {
                /* 03d620/03d6e8: turn towards the steepest ramp heading. */
                int target=bam_atan2(-p->gn[0],-p->gn[2])+(p->speed<0.f ? 32768 : 0);
                int diff=(int16_t)(target-dc_yaw(p->yaw));
                p->yaw=wrap_pi(p->yaw+fmaxf(-910.f,fminf(910.f,diff))/BAM);
            } else if (p->contact_mode==8) recovery_brake(p);
            else p->speed=p->kb=0.f;
        } else if (p->contact_mode==10 || p->contact_mode==11) {
            p->speed*=.5f;block_controls=1;
            player_voice(p,PLAYER_VOICE_COLLISION); /* 06d244/300 */
#ifdef PLAYER_COLLISION_SOUND
            PLAYER_COLLISION_SOUND(p,0);
#endif
            animation_overlay(p,p->contact_mode==10 ? 96 : 97);
        } else if (p->contact_mode>=12) {
            block_controls=1;
            if ((p->contact_mode==14 || p->contact_mode==17) && p->speed<0x1.284bdap+1f) {
                p->speed*=.5f;p->contact_protected=1;set_motion(p,95);
                player_voice(p,PLAYER_VOICE_COLLISION); /* 06d42c/534 */
            } else wall_recovery(p,p->contact_mode);
        }
    }
    /* 01baa8 dispatches contact before 01cc8c. Side impacts, bounces and
       wall attachment skip that control callback, retaining vertical speed
       and timers for the next queued sweep. */
    if(handler_state==2 && !hit && !block_controls && p->state==2) {
        float e8 = p->rise_gravity, ec = p->fall_gravity;
        if (p->sub == 0) {
            p->rise_ticks++;
            if (p->rise_ticks >= T->jump_min && !p->jump_held && !p->air_trick) p->vy = 0.f;
            else p->vy -= e8;
            /* Natural apex keeps its small negative velocity; only an early
               button release zeroes it (FUN_8c01cc8c case 0). */
            if (p->vy <= 0.f) {
                p->sub=2;
                if (!p->air_trick) {
                    /* 01cd7c..01ceae updates BOTH +200 and logical motion. */
                    p->jump_motion=falling_motion(p->jump_motion);set_motion(p,p->jump_motion);
                }
            }
        } else {
            if(p->sub==2)p->air_pitch=0; /* 01cf34: clear on the first descent control tick */
            p->vy -= ec*((p->sub == 2 && !p->jump_held && !p->air_trick) ? T->fall_release : 1.f);
            if (p->sub==1 || p->sub==3) {
                /* 01cc8c cases 1/3: the departure tilt expires when +f0
                   reaches zero. Keeping it rotates gravity sideways and
                   turns forward speed into an endless climb at 90 degrees. */
                if (p->rise_ticks>0 && --p->rise_ticks==0) p->air_pitch=0;
                fall_threshold_check=1;
            }
            if(p->sub==2)fall_threshold_check=2;
        }
        if (p->vy < -T->fall_max) p->vy = -T->fall_max;
    }
    /* 01baa8 resolves 05251c before choosing 01bc94 (landing) or
       01cc8c (air control). A proposed step below a nearby floor must
       not start severe-fall grace before the landing is accepted. */
    if(!hit && !block_controls && p->state==2) {
        if(fall_threshold_check==1 && p->jump_peak_y-p->y>56.f) {
            p->sub=4;p->rise_ticks=18;set_motion(p,76);
        } else if(fall_threshold_check==2 &&
                  (p->ramp_active ? p->y<p->jump_start_y : p->jump_peak_y-p->y>17.f)) {
            int n=(int)(T->jump_ticks*T->rise_fraction);
            int nd=(int)(T->jump_ticks*(1.f-T->rise_fraction));
            p->sub=3;p->rise_ticks=0;
            p->rise_gravity=T->jump_h/(float)(n*(n+1)/2);
            p->fall_gravity=T->jump_h/(float)(nd*(nd+1)/2);
            p->vy=fmaxf(-T->fall_max,-p->rise_gravity*n);set_motion(p,74);
        }
        /* 01cc8c's peak and descent tests use +bc after 05251c resolves
           body/floor contact. A ceiling-rejected proposed rise must never
           inflate the apex and start the falling animation a tick early. */
        if(handler_state==2 && p->y>p->jump_peak_y)p->jump_peak_y=p->y;
    }
    player_portal_translate(p);
#ifdef PLAYER_CONTACT_SPRAY
    /* Private native replay proof: 06bdf0 checks 01abb8 after contact
       publication and before dispatching the ground controller. */
    if(PLAYER_CONTACT_SPRAY(p,handler_state))goto tick_finish;
#endif
    /* camera object tick (trace17): eye on a 43-unit leash at +18 (30 right after the spawn), dragged along the eye->skater line only
       when taut; the camera yaw is the direction of (eye - skater). Runs after the move and before the turn (the DC turn uses cam_i). */
    if(!p->camera_tick) { float R = p->state == 9 ? 12.f : 43.f; if (!p->cam_init) player_cam_reset(p);
      float ex = p->cam_ex - p->x, ez = p->cam_ez - p->z, d = sqrtf(ex * ex + ez * ez);
      if (d > R) { p->cam_ex = p->x + ex / d * R; p->cam_ez = p->z + ez / d * R; }
      else if (d < 1e-3f) { p->cam_ex = p->x - sinf(p->yaw) * R; p->cam_ez = p->z - cosf(p->yaw) * R; }
      p->cam_yaw = atan2f(p->x - p->cam_ex, p->z - p->cam_ez); cam_yaw = p->cam_yaw; }
    if(p->state==9 && p->police_knockdown) {
        /* 042c24 -> 042cbc -> 042e74: fall, crawl/struggle, get up.
           CROSS edges reduce the source recovery counter faster. */
        if(p->police_knockdown==1 && p->motion_done){p->police_knockdown=2;p->speed=0;set_motion(p,99);}
        else if(p->police_knockdown==2) {
            p->police_recovery-=a_press ? T->grab_mash:T->grab_recover;
            if(mag>.5f){p->speed=.462962955f;p->yaw=wrap_pi(p->yaw+ground_turn(T,p->speed,0,p->surf,(short)(p->input_yaw[0]-dc_yaw(p->yaw)))/BAM);set_motion(p,100);}
            else {p->speed=0;set_motion(p,99);}
            if(p->police_recovery<=0){p->police_knockdown=3;set_motion(p,101);}
        } else if(p->police_knockdown==3 && p->motion_done){p->police_knockdown=0;p->contact_protected=1;p->state=1;set_motion(p,MOT_IDLE);}
        stick=0;a_press=0;
    }
    if (p->state == 9 && !p->police_knockdown && p->stun > 0) { if (--p->stun == 0) { p->state = 1; set_motion(p, MOT_IDLE); } stick = 0; a_press = 0; }
    if (p->state == 7) { stick = 0; a_press = 0; }

    if(handler_state!=1 && p->state==1){p->ground_ticks=0;p->turn_delay=0;} /* 06bd04 */
    /* 06bdf0 -> 06cecc only initializes a walk-off. Air steering belongs
       to the next airborne-handler tick, as it does after a button jump. */
    int airborne_at_control = handler_state == 2;
    if (p->state == 1 && !p->grinding && p->onground && !block_controls) {
        if(stick)p->idle_ticks=0; /* 06c466: held ground input resets +5c4. */
        /* Only mode 8 dispatches 06cf70's fresh boost. Granting it before
           collision gave mode 9 one boosted tick before its cancellation. */
        if(handler_state==1 && p->contact_mode==8 && dash_press && !a_press &&
           !p->mirrored && mag>.9f && p->boost_cool==0 && !stair_surface(p->surf))
            p->boost=p->boost_cool=60;
        /* 06bdf0: countdown precedes the controller, including a jump
           on this tick; it does not run in the airborne handler. */
        if(p->turn_delay>0 && --p->turn_delay==0)trick_event(p,5,0);
        /* 06cf70's first operation. Even skating/braking/jumping counts;
           leaving the target volume does not reset this player counter. */
        if(++p->ground_ticks==6)trial_event(p,TRIAL_GROUND_SIX);
        /* 06c048 subtracts the recorded 027788 angle from camera+c4,
           then 0b27cc/0b26f0 operate on INTEGER BAM headings. A radians
           subtraction followed by truncation loses one unit on many
           ordinary turns, accumulating a different recorded trajectory. */
        int target_bam=dc_yaw(cam_yaw+(stick ? atan2f(ax,-ay):0.f));
        int difference=(int16_t)(target_bam-dc_yaw(p->yaw));
        int behind = mag > 0.1f && abs(difference) > T->back_angle;
        int pulling=0,far_grip=0;
        if(p->car_pull_query && !a_press) {
            if(!p->car_pull.valid){p->car_pull.heading=dc_yaw(p->yaw);p->car_pull.valid=1;}
            int yaw=dc_yaw(p->yaw);const float pos[3]={p->x,p->y,p->z};
            pulling=car_pull_step(&p->car_pull,pos,&yaw,&p->speed,p->surface_motion,
                                  p->logical_motion,bam_atan2,&far_grip);
            p->yaw=wrap_pi((yaw+32768)/BAM);
            behind=mag>.1f && abs((int16_t)(target_bam-yaw))>T->back_angle;
        } else p->car_pull.active=0;
        /* FUN_8c06cf70 takes the jump branch BEFORE ordinary steering or
           acceleration. Jumping must not perform a bonus skate tick. */
        if (a_press) {
            control_jump(p,mag); p->jump_held = a_held;
            /* FUN_8c01cad8: normal jump kind 0 enables a capped 30-degree
               takeoff turn only within 120 degrees of the current heading. */
            if (!p->ramp_active && mag > 0.5f && abs(difference)<21845) {
                int turn=difference>5461 ? 5461:difference< -5461 ? -5461:difference;
                p->yaw=wrap_pi((dc_yaw(p->yaw)+turn+32768)/BAM);
            }
        } else if(pulling) {
            if(behind){p->speed*=.5f;p->car_pull.released=1;p->car_pull.active=0;}
            else set_motion(p,5); /* original holding-the-car pose */
        } else if (p->braking && (behind || !stick || (p->brake_forced && !finished_motion))) {
            float prev = p->speed;
            p->speed += p->brake_step; /* FUN_8c06c000 */
            if ((prev > 0.f && p->speed <= 0.f) || (prev < 0.f && p->speed >= 0.f)) p->speed = p->brake_step = 0.f;
            /* 06c048 waits for +5bc from the preceding animation tick,
               not merely for speed to reach zero. */
            if (finished_motion) { p->braking=0;set_motion(p,MOT_IDLE); }
        } else if(p->braking) {
            /* 06c048 release branch does NOT also steer/accelerate. */
            p->braking=p->brake_forced=0;p->brake_released=1;p->turn_lock=0;
            if((p->logical_motion==12 || p->logical_motion==14) && trick_gesture(p)){
                trial_event(p,TRIAL_GROUND_TRICK); /* 08e818 spatial type-6 trial */
                set_motion(p,15);p->turn_delay=3;p->turn_lock=1;p->practice_event=14;
            }
        } else {
        if (stick) {
            if (behind && !p->brake_released) {
                start_brake(p);
            } else {
                if (!behind) p->brake_released = 0;
                /* FUN_8c06c048 -> FUN_8c0b26f0: full remaining difference,
                   capped in integer BAM. Confirmed by 256 Flycast cases;
                   the old half-angle fit to asynchronous trace16 was wrong. */
                int dyb = ground_turn(T, p->speed, dash, p->surf, difference);
                p->yaw = wrap_pi((dc_yaw(p->yaw)+dyb+32768)/BAM);
                /* FUN_8c01a490 receives the pre-turn difference. */
                float lean_target=fminf(1.f,(float)abs(difference)/T->back_angle);
                if (difference< -910) p->skate_lean=fminf(lean_target,p->skate_lean+0x1.9a029p-4f);
                else if (difference>910) p->skate_lean=fmaxf(-lean_target,p->skate_lean-0x1.9a029p-4f);
                else if (p->skate_lean>0.f) p->skate_lean=fmaxf(0.f,p->skate_lean-0x1.9a029p-4f);
                else p->skate_lean=fminf(0.f,p->skate_lean+0x1.9a029p-4f);
                p->speed = ground_speed_rate_scaled(T, p->speed, mag, dash, p->boost, movement_pitch(p), p->surf,&p->skate_rate,far_grip ? 2.f:1.f,police_grip_count(&p->police_grips),p->stumble_ticks>0);
                if (!p->contact_protected && !p->turn_lock) set_motion(p,ground_locomotion(p,dash && !p->contact_headon ? MOT_DASH : MOT_SKATE));
            }
        } else {
            p->brake_released = 0;
            if (!p->brake_contact && (p->contact_headon || (released_stick && p->speed < 0x1.da12f6p-1f))) {
                /* FUN_8c01a860 / 8c06c4d6: release near walking speed
                   or a head-on contact starts the same six-tick stop.
                   +21c prevents restarting it until input is held. */
                p->braking = 1; p->brake_step = -p->speed/6.f;p->brake_contact=p->contact_headon;set_motion(p, 11);
            } else {
                p->speed = ground_speed_rate_scaled(T, p->speed, 0.f, dash, p->boost, movement_pitch(p), p->surf,&p->skate_rate,far_grip ? 2.f:1.f,police_grip_count(&p->police_grips),p->stumble_ticks>0);
                int motion=ground_locomotion(p,MOT_COAST);
                if(p->speed==0.f) {
                    /* 06c530: stationary time pauses under a police grip.
                       Delay values belong to the external movement record. */
                    if(!police_grip_count(&p->police_grips))p->idle_ticks++;
                    motion=MOT_IDLE;
                    if(p->idle_thresholds[0] && p->idle_ticks>=p->idle_thresholds[0])motion=2;
                    if(p->idle_thresholds[1] && p->idle_ticks>=p->idle_thresholds[1])motion=3;
                }
                if (!p->contact_protected && !p->turn_lock) set_motion(p,motion);
            }
        }
        }
        if (!a_press && p->contact_speed>0.f)
            p->speed=fmaxf(-p->contact_speed,fminf(p->contact_speed,p->speed));
        /* 06c67e: shared +68/+6c cap ascending and descending stairs.
           Both original values are 0x3f6d097b. Apply after ordinary control
           and the contact-speed clamp, before sampling the stair pose. */
        if(!a_press && !pulling && stair_surface(p->surf))
            p->speed=fmaxf(-0x1.da12f6p-1f,fminf(0x1.da12f6p-1f,p->speed));
        /* 06c048 tail: a released stick aligns the skates with a slope.
           All ordinary branches, including braking, reach this tail after the
           speed clamps. +21c blocks contact stops, while released-stick
           braking still turns. Jump/car handlers bypass 06c048 entirely. */
        if(!a_press && !pulling && !p->brake_contact && !reset_query && p->surf!=5 && p->surf!=31 && p->surf!=61) {
            int yaw=dc_yaw(p->yaw),target=bam_atan2(-p->gn[0],-p->gn[2]),limit=0;
            if(p->gn[0]==0.f && p->gn[2]==0.f)target=yaw;
            if(p->surf==10)limit=p->speed<T->ramp_align_speed ? T->ramp_align_slow:T->ramp_align_fast;
            else if(mag<.5f) {
                int slope=bam_atan2(sqrtf(p->gn[0]*p->gn[0]+p->gn[2]*p->gn[2]),p->gn[1]);
                if(slope>T->uphill_dead || slope< -T->downhill_dead) {
                    limit=abs(slope)<6371 ? abs(slope)/(dash ? 9:5):(dash ? 91:910);
                    if(abs((int16_t)(yaw-target))>24576)target+=32768;
                }
            }
            int d=(int16_t)(target-yaw);if(d>limit)d=limit;if(d< -limit)d=-limit;
            if(d)p->yaw=wrap_pi((yaw+d+32768)/BAM);
        }
    }
    if (p->state == 3 && !grind_entry) {
        /* FUN_8c020c84: rail heading is supplied by contact, A jumps off,
           otherwise slope/friction; after seven zero-speed ticks, fall. */
        p->grind_dist += fabsf(p->speed);
        if (p->grind_dist > 35.f) {   /* shared tuning +0x9c = 35 (NOT zero); FUN_8c020c84 */
            trick_event(p,p->rail_side ? 0:1,TRICK_GRIND_IDS[p->character_id][!!p->mirrored][p->grind_pose]);
            if(p->chain>0)p->chain--; /* 020c84, outside the popup producer */
        }
        player_voice(p,PLAYER_VOICE_GRIND); /* 020c84, before jump/friction */
        if (a_press) {
            /* Kind 2 is flags 0x5c31 at 0x8c10a648. Its 1000 branch
               calls 01cb60 after the boost; 0040 is the separate ground
               turn. Omitting 01cb60 made sideways rail transfers fall short. */
            control_jump(p,mag); p->jump_held = a_held;
            int difference=(int16_t)(dc_yaw(cam_yaw+atan2f(ax,-ay))-dc_yaw(p->yaw));
            int turn=rail_takeoff_turn(difference,mag);
            p->yaw=wrap_pi((dc_yaw(p->yaw)+turn+32768)/BAM);
        } else {
            p->speed = rail_speed(p->speed, movement_pitch(p));
            if (p->speed == 0.f) { combo_bonus(p);p->grind_dist=0.f; if (--p->stop_ticks < 0) start_fall(p, 2, 92); }
        }
    }
    /* 01cc8c can select a new animation before steering, but 03ed98
       transfers its outgoing root yaw only after that controller returns.
       set_motion records that transfer immediately; temporarily recover
       the controller's heading, then retain its turn plus the transfer. */
    int air_handoff=airborne_at_control && p->state==2 && !block_controls ? p->queued_root_yaw:0;
    if(air_handoff)p->yaw=wrap_pi((dc_yaw(p->yaw)-air_handoff+32768)/BAM);
    if (airborne_at_control && p->state==2 && !block_controls && p->ramp_active) {
        p->yaw=wrap_pi((visual_approach(dc_yaw(p->yaw),p->ramp_target,p->ramp_turn)+32768)/BAM);
    }
    else if (airborne_at_control && p->state == 2 && !block_controls && mag > 0.5f) { /* FUN_8c01cc8c */
        int d=(int16_t)(dc_yaw(cam_yaw+atan2f(ax,-ay))-dc_yaw(p->yaw));
        if (abs(d) < T->air_back_angle) {
            if (p->speed < T->air_min_speed) p->speed = fminf(T->air_min_speed, p->speed+T->air_min_speed);
            if (d > T->air_dead) p->yaw = wrap_pi(p->yaw + T->air_turn/BAM);
            if (d < -T->air_dead) p->yaw = wrap_pi(p->yaw - T->air_turn/BAM);
        } else p->speed = fmaxf(0.f, p->speed-T->air_brake);
    }
    if(air_handoff)p->yaw=wrap_pi((dc_yaw(p->yaw)+air_handoff+32768)/BAM);
    /* 01bc94 tests the existing +f0 grace before any airborne control
       countdown. A contact on its last tick still receives a soft landing. */
    if(handler_state==2 && !hit && !block_controls && entered_fall_grace && p->state==2 && p->sub==4 && p->rise_ticks>0)p->rise_ticks--;
    /* 06bdf0 checks the accepted ground/body mode AFTER the control
       callback. A wall-slide mode 9 consumes this tick's acceleration,
       then cancels the boost before pose evaluation and the next tick. */
    if(handler_state==1){
        if(!dash || mag<=.9f || p->contact_mode!=8 || stair_surface(p->surf))p->boost=0;
        else if(p->boost>0)p->boost--;
        if(p->boost_cool>0)p->boost_cool--;
    }
    /* Dispatch by the handler we ENTERED, not the resulting pose/state.
       A rail jump still returns through 020a38 -> 08e62c. 08e6c4 is the
       WALL-RIDE handler's distance check, not an ordinary airborne check. */
    if(handler_state==3 && !block_controls)trial_event(p,TRIAL_GRIND);
    if(handler_state==4 && (p->contact_mode==18 || !block_controls))trial_event(p,TRIAL_WALL);
    if(p->car_pull_query) {
        if(p->state==1 && p->onground) {
            /* 052940: query from the retained grip anchor (or ordinary
               player position), with this tick's queued surface motion. */
            if(!p->car_pull.active){p->car_pull.heading=dc_yaw(p->yaw);p->car_pull.reference[0]=p->x;p->car_pull.reference[1]=p->y;p->car_pull.reference[2]=p->z;}
            float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
            const float zero[3]={0},one[3]={1,1,1};int32_t angles[3],axis[3]={0};player_motion_angles(p,angles);angles[1]=p->car_pull.heading;
            for(int r=0;r<3;r++)matrix[r*4+3]=p->car_pull.reference[r]+p->surface_motion[r];
            for(int i=0;i<3;i++){int k=i==0 ? 0:i==1 ? 2:1;axis[k]=angles[k];source_trs_apply(zero,axis,one,matrix,bam_sin);axis[k]=0;}
            float point[3];for(int r=0;r<3;r++)point[r]=(float)((double)matrix[r*4+2]*-10.f+matrix[r*4+3]);
            p->car_pull_query(p->car_pull_context,point,&p->car_pull.contact);
        } else {p->car_pull.active=0;p->car_pull.contact.handle=0;p->car_pull.valid=0;}
    }
tick_finish:
    animation_tick(p);
    if(p->world_stage>=1 && p->world_stage<=3 && !p->reset_query && !p->water.phase) {
        const float from[3]={p->px,p->py,p->pz},to[3]={p->x,p->y,p->z};
        stage_portal_query(p->world_stage,from,to,p->portal_pending);
    }
    if(p->water_enabled && !p->water.phase && (p->state==2 || (p->state==6 && p->sub==1))) {
        float position[3]={p->x,p->y,p->z};
        p->water.pending=kogane_water_query(p->water_area,p->water_sector,position);
    }
    kogane_water_effects_tick(&p->water);
    if(p->turn_lock && p->motion_done)p->turn_lock=0; /* 0198f8, after 03ed98 */
    unsigned visual_started=player_clock();visual_tick(p);effects_tick(p);
    player_profile.effects_us+=player_clock()-visual_started;
    if(!p->water.phase && p->state!=7 && p->state!=10 && p->state!=11) {
        /* 056efc: original vertical extent +5.1 / -2, type/mask 8.
           Keep the point for next tick's splash and skate sound. */
        float a[3]={p->x,p->y+5.1f,p->z},b[3]={p->x,p->y-2.f,p->z};ColHit wet;int water=0;
        p->shallow_pending=col_probe_filtered(col,a,b,8,shallow_contact,&water,&wet) && water;
        if(p->shallow_pending)memcpy(p->shallow_point,wet.p,sizeof p->shallow_point);
    }
    if(p->sound_spatial){unsigned t=player_clock();p->sound_spatial(p->sound_context);player_profile.audio_us+=player_clock()-t;}
    trial_event(p,TRIAL_FAIL_VOLUME); /* 0198f8 after its state/pose update */
    damage_clocks(p);
    p->logic_tick++; /* FUN_8c0198f8 advances +0x98 AFTER its state handler */
    /* The recovered 01dc40 task observes the completed 0198f8 actor,
       including its new yaw/pose/clock. Its heading feeds the NEXT input
       update. Running it before steering gave staff replay a one-tick
       lookahead absent from the captured original callback sequence. */
    if(p->camera_tick){unsigned phase=hang_mark(HANG_PLAYER_CAMERA),t=player_clock();
        p->cam_yaw=p->camera_tick(p->camera_context);player_profile.camera_us+=player_clock()-t;hang_mark(phase);}
}
static void player_tick(Player *p,const Collision *col,float ax,float ay,int held,int press,int dash,int dash_press,float camera)
{unsigned t=player_clock();player_tick_impl(p,col,ax,ay,held,press,dash,dash_press,camera);player_profile.total_us+=player_clock()-t;player_profile.ticks++;}


static int player_overlay_hit(Player *p,int logical,int amount)
{
    if(!p || p->health<=0 || p->damage_invincible>0 || p->reset_query)return 0;
    /* +6c stays the current movement handler. State 7 from 042b60 is
       transient source dispatch state, NOT the generic knockdown below. */
    FinalDamage d={.health=p->health,.combo=p->combo,.grind_distance=p->grind_dist,
                   .invincible=p->damage_invincible};
    unsigned flags=(unsigned)p->logical_motion<PLAYER_MOTION_COUNT ? MOTION_SPEC[p->logical_motion][!!p->mirrored].flags:0;
    enemy_overlay_damage(&d,logical,amount,120,0,flags,0);
    if(p->combo && !d.combo)trick_hud_clear_labels(&p->trick_hud);
    p->health=d.health;p->combo=d.combo;p->grind_dist=d.grind_distance;
    p->damage_invincible=d.invincible;p->burn_ticks=d.burn;
    p->overlay_logical=d.overlay;p->overlay_ticks=d.overlay_ticks;
    p->stumble_ticks=d.overlay_ticks;
    p->overlay_frame=d.overlay_frame;p->overlay_blend=d.overlay_blend;if(logical==158)p->fire_hits++;else p->bullet_hits++;
    return 1;
}
int player_fire_hit(Player *p)
{return p ? player_overlay_hit(p,158,(p->tun ? p->tun:&BEAT_TUNING)->fire_damage):0;}
int player_receive_bullet(Player *p)
{return player_bullet_vulnerable(p) ? player_overlay_hit(p,157,(p->tun ? p->tun:&BEAT_TUNING)->bullet_damage):0;}
int player_bullet_vulnerable(const Player *p)
{
    if(!p || p->health<=0 || p->damage_invincible || p->reset_query || p->vehicle_recovery)return 0;
    /* Port state 7 is the separate 03d8c8 push/get-up adapter. */
    if(p->state==7 || p->state==9 || p->burn_ticks)return 0;
    return enemy_bullet_vulnerable(p->grinding ? 3:p->state,p->air_trick,p->boost);
}
static int player_impact_hit(Player *p,int mode,int yaw,int explosion)
{
    if(!p || (unsigned)mode>=12 || p->health<=0 || p->damage_invincible || p->reset_query ||
       p->state==7 || p->state==9 || p->state==10)return 0;
    const CharTuning *t=p->tun ? p->tun:&BEAT_TUNING;
    /* 17ece0: an explosion always selects front/back airborne response.
       Both impact families use recovery height +f4, not jump height +c0. */
    VehicleResponse r=vehicle_response(explosion?(mode<6?0:6):mode,p->state==2 || !p->onground,
        p->speed,t->damage_jump_h,t->recovery_ticks);
    if(!r.motion)return 0;
    /* Accepted vehicle body contact: 051f0c -> 04315c. Explosion effects
       have their own producer; do not synthesize a second burst here. */
    if(explosion!=1){p->effects.impact=(SkateBurst){.active=1};effect_impact_position(p);}
    p->health-=explosion==2?t->taban_damage:explosion?t->explosion_damage:t->vehicle_damage;if(p->health<0)p->health=0;
    p->damage_invincible=p->health>0 ? 120:0;p->vehicle_recovery=1;p->vehicle_hits++;
    /* 042bac -> 05b00c clears the current trick, not the accumulated chain. */
    trick_hud_reset(&p->trick_hud,&p->combo,&p->grind_dist);
    p->ramp_active=p->grinding=p->braking=p->boost=0;p->rail_detached=1;
    memset(&p->car_pull,0,sizeof p->car_pull);
    p->yaw=wrap_pi(yaw/BAM);p->speed=r.speed;p->kb=r.brake;p->state=6;
    p->sub=r.airborne;p->vy=r.velocity;p->fall_gravity=r.gravity;p->air_probe_valid=p->body_valid=0;
    if(r.airborne)p->onground=0;
    p->overlay_ticks=0;p->overlay_blend=0;player_voice(p,PLAYER_VOICE_DAMAGE);
    set_motion(p,r.motion);player_voice(p,PLAYER_VOICE_COLLISION);
    return 1;
}
int player_receive_vehicle(Player *p,int mode,int yaw){return player_impact_hit(p,mode,yaw,0);}
int player_receive_explosion(Player *p,int mode,int yaw){return player_impact_hit(p,mode,yaw,1);}
/* Attribute 27 -> 0424f4/042564, tables 17ec80/17ecb0: the same
   front/back airborne response, own +34 damage, and the hit burst. */
int player_receive_taban(Player *p,int mode,int yaw){return player_impact_hit(p,mode,yaw,2);}
int player_police_receive(Player *p)
{
    int count=police_grip_count(&p->police_grips);if(!count || p->health<=0)return 0;
    if(!p->damage_invincible && !p->police_grab_damage_clock)p->police_grab_damage_clock=60;
    int interrupted=p->spray_active || p->small_spray_active;
    p->small_spray_active=0;
    /* 06bdf0 -> 0426a4: three attached officers topple the player. */
    if(count==3 && !p->police_knockdown && p->state!=9) {
        p->police_knockdown=1;p->police_recovery=p->tun->grab_stun_ticks;
        p->state=9;p->sub=0;p->speed*=.5f;p->kb=-p->speed/18.f;
        p->grinding=p->ramp_active=p->braking=p->boost=0;
        memset(&p->car_pull,0,sizeof p->car_pull);p->damage_invincible=120;
        set_motion(p,MOT_STUMBLE);interrupted=1;
    }
    return interrupted;
}

void player_hit(Player *p, float dx, float dz)
{
    if (p->state == 9 || p->state == 7 || p->state==10) return;
    p->ramp_active=0;
    p->health -= 5; if (p->health < 0) p->health = 0; combo_reset(p);
    p->state = 7; p->sub=0; p->stun = 30; p->grinding = 0; p->push[0] = dx * 0.5f; p->push[1] = dz * 0.5f; p->speed = 0.f; p->vy = 0.f; set_motion(p, 0x5a);
}
void player_update_axes(Player *p, const Collision *col, float ax, float ay, unsigned buttons, unsigned pressed, float cam_yaw, float dt)
{
    int a_held = (buttons & PSP_CTRL_CROSS) != 0;
    p->pending_pressed |= pressed;
    p->tick_acc += dt; if (p->tick_acc > 4 * TICK) p->tick_acc = 4 * TICK;
    while (p->tick_acc >= TICK) {
        unsigned phase=hang_mark(HANG_PLAYER_WORLD);
        if(p->before_tick && p->before_tick(p->before_tick_context)) {p->tick_acc=0;p->speed=p->vy=0;hang_mark(phase);return;}
        unsigned edges = p->pending_pressed; p->pending_pressed = 0;
        /* 0198f8 -> 01b754: the spray edge also recentres the NEXT camera
           update, even without an eligible tag. Keep this tick's heading. */
        if(edges&GAME_SPRAY)p->camera_reset=1;
        hang_mark(HANG_PLAYER_SIM);
        player_tick(p, col, ax, ay, a_held, (edges & PSP_CTRL_CROSS) != 0,
                    (buttons & GAME_BOOST) != 0, (edges & GAME_BOOST) != 0,
                    p->camera_tick ? p->cam_yaw:cam_yaw);
        hang_mark(HANG_PLAYER_CONTACTS);
        if(p->after_tick)p->after_tick(p->after_tick_context);
        p->tick_acc -= TICK;
        hang_mark(phase);
    }
}
static void player_map_direction_keys(SceCtrlData *pad, int graffiti)
{
    /* PSP nubs can rest beyond the old 19-unit radial dead zone. Treat a
       36-unit circle as neutral for both movement and graffiti, preserving
       both axes outside it so small steering corrections still work.
       Clear resting drift before the digital fallback, including its
       unused axis. Work on the caller's copy, never the raw pad sample. */
    int x=(int)pad->Lx-128,y=(int)pad->Ly-128;
    if (x*x+y*y>36*36) return;
    pad->Lx=pad->Ly=128;
    /* Keep the requested skating key swap. Graffiti uses screen directions
       so the physical input follows the displayed arrow. */
    if (pad->Buttons & PSP_CTRL_LEFT) pad->Lx=graffiti ? 0:255;
    else if (pad->Buttons & PSP_CTRL_RIGHT) pad->Lx=graffiti ? 255:0;
    if (pad->Buttons & PSP_CTRL_UP) pad->Ly=0;
    else if (pad->Buttons & PSP_CTRL_DOWN) pad->Ly=255;
}
void player_input_axes(const SceCtrlData *pad, float *ax, float *ay, int graffiti)
{
    SceCtrlData mapped=*pad;
    player_map_direction_keys(&mapped, graffiti);
    /* PLATFORM: reverse the physical nub's X only while skating. Graffiti
       arrows use physical screen directions for every character/pattern.
       Map the D-pad separately so its skating swap is not reversed again.
       Negate AFTER normalization: byte-wise 255-x moves neutral off 128,
       while 256-x cannot represent both endpoints without clipping. */
    *ax = (mapped.Lx - 128) / (!graffiti && mapped.Lx >= 128 ? 127.f : 128.f);
    *ay = (mapped.Ly - 128) / (!graffiti && mapped.Ly >= 128 ? 127.f : 128.f);
    if (!graffiti && mapped.Lx == pad->Lx) *ax = -*ax;
}
void player_update(Player *p, const Collision *col, const SceCtrlData *pad, unsigned pressed, float cam_yaw, float dt)
{
    float ax, ay;
    player_input_axes(pad, &ax, &ay, 0);
    /* FUN_8c027c74 stores 8-bit direction/magnitude; FUN_8c027788/7a0
       expands them again. Preserve that resolution at the platform boundary. */
    float mag = fminf(1.f, sqrtf(ax*ax+ay*ay));
    mag = (int)(mag*255.f) * (1.f/255.f);
    float angle = (int16_t)((int)(atan2f(ax,-ay)*BAM) & 0xff00) / BAM;
    ax = sinf(angle)*mag; ay = -cosf(angle)*mag;
    player_update_axes(p, col, ax, ay, pad->Buttons, pressed, cam_yaw, dt);
}

/* Source spray logical-motion dispatch. The host binds this only when the
   target has an audited original camera subvolume. Cue/HUD task lifetime is
   owned by graffiti_game, not by an invented pose-duration timer. */
static SprayTracks spray_tracks;
static unsigned char *spray_track_image;
int player_load_spray_cameras(void)
{
    FILE *f=fopen("spraycam.bin","rb");
    if(!f)return -1;
    int ok=0;long size=0;unsigned char *data=NULL;SprayTracks tracks={0};
    if(!fseek(f,0,SEEK_END) && (size=ftell(f))>16 && size<=65536) {
        rewind(f);data=malloc(size);
        if(data && fread(data,1,size,f)==(unsigned)size)ok=spray_tracks_init(&tracks,data,size);
    }
    fclose(f);
    if(!ok){free(data);return -2;}
    free(spray_track_image);spray_track_image=data;spray_tracks=tracks;return 0;
}
static int spray_subvolume(Player *p,const Spot *s)
{
    if(!s->camera_valid)return 0;
    const float position[3]={p->x,p->y,p->z};
    const void *volumes=s->source ? s->source->volumes:s->camera_volume;
    unsigned count=s->source ? s->source->nvolume:1;
    return spray_camera_subject(position,volumes,count,s->camera_candidates,s->camera_ncandidate,p->spray_subject)+1;
}
void player_spray_actor(void *context,Graffiti *g,Spot *s,int action,int cue)
{
    Player *p=context;
    if(action==SPRAY_ACT_SOUND) {
        if(cue>=SPRAY_SOUND_SYSTEM && cue<SPRAY_SOUND_SYSTEM+16)player_sound(p,PLAYER_SOUND_SYSTEM,cue-SPRAY_SOUND_SYSTEM);
        else if(cue==SPRAY_SOUND_SMALL){player_sound(p,PLAYER_SOUND_VOICE,0);player_sound(p,PLAYER_SOUND_SYSTEM,13);}
        else if(cue==SPRAY_SOUND_EMPTY)player_sound(p,PLAYER_SOUND_SYSTEM,11);
        else player_voice(p,cue==SPRAY_SOUND_FINAL?PLAYER_VOICE_TRICK:PLAYER_VOICE_JUMP);
        return;
    }
    if(action==SPRAY_ACT_SMALL) {
        const float target[3]={s->x,s->y,s->z};player_small_spray_begin(p,s->source ? s->source->center:target);return;
    }
    if(action==SPRAY_ACT_ENTER) {
        if(s->source)player_scene_matrix(s->source->origin,s->source->rotation,s->source->scale,s->camera_matrix);
        /* 03aedc does not run 01aeb0's actor-reset seed store. Keep the
           live motion/voice RNG while retiring platform control state. */
        unsigned seed=p->rnd;player_reset_controls(p);p->rnd=seed;p->spray_active=1;
        p->spray_cursor=0;p->spray_used=0;p->state=5;p->sub=0;
        p->speed=p->vy=p->kb=0;p->grinding=0;
        p->visual_valid=1;memset(p->visual_angles,0,sizeof p->visual_angles);
        p->yaw=32768.f/BAM;
        for(unsigned j=0;j<24;j++)p->spray_shuffle[j]=j;
        for(unsigned j=0;j<24;j++) {
            unsigned k=player_random(p,24),v=p->spray_shuffle[k];
            p->spray_shuffle[k]=p->spray_shuffle[j];p->spray_shuffle[j]=v;
        }
        if(p->before_tick && p->before_tick(p->before_tick_context)) {
            g->mg_active=0;p->spray_active=p->spray_camera.active=0;p->spray_mode=p->spray_variant=0;return;
        }
        p->spray_subvolume=spray_subvolume(p,s);
#ifdef PLAYER_CONTACT_SPRAY_DEFER_FINISH
        if(PLAYER_CONTACT_SPRAY_DEFER_FINISH(p))return;
#endif
        p->score_add+=trick_hud_tick(&p->trick_hud,player_fsca_sin);
        animation_tick(p);effects_tick(p);if(p->sound_spatial)p->sound_spatial(p->sound_context);damage_clocks(p);p->logic_tick++;
        if(p->after_tick)p->after_tick(p->after_tick_context);
        if(p->camera_tick)p->cam_yaw=p->camera_tick(p->camera_context);
        return;
    }
    if(action==SPRAY_ACT_PRE) {
        g->reveal_step=p->spray_reveal_step;
        if(p->before_tick && p->before_tick(p->before_tick_context)) {
            g->mg_active=0;p->spray_active=p->spray_camera.active=0;p->spray_mode=p->spray_variant=0;return;
        }
        g->actor_damage=p->fire_pending && p->health>0 && !p->damage_invincible && !p->reset_query;
        if(!g->actor_damage)p->fire_pending=0;
        return;
    }
    if(action==SPRAY_ACT_DAMAGE) {
        p->score_add+=trick_hud_tick(&p->trick_hud,player_fsca_sin);
        player_fire_hit(p);p->fire_pending=0;
        /* 042b14 returns zero; 03c0f0 -> 06bd04(7) restores the
           ordinary handler AFTER the burn, never a generic knockdown. */
        p->state=1;p->sub=0;p->speed=p->vy=0;p->boost=p->boost_cool=0;
        p->ground_ticks=0;p->coast_blend=0;combo_reset(p);set_motion(p,1);
        animation_tick(p);visual_tick(p);effects_tick(p);if(p->sound_spatial)p->sound_spatial(p->sound_context);damage_clocks(p);p->logic_tick++;
        if(p->after_tick)p->after_tick(p->after_tick_context);
        return;
    }
    if(action==SPRAY_ACT_SETUP) {
        if(s->stroke>=s->nstrokes)return;
        const SprayAnchor *a=s->anchors+s->stroke;
        p->x=a->position[0];p->y=a->position[1];p->z=a->position[2];
        memcpy(p->spray_matrix,s->camera_matrix,64);
        float cx=s->source ? s->source->center[0]:s->x,cz=s->source ? s->source->center[2]:s->z;
        int yaw=a->radial ? bam_atan2(p->x-cx,p->z-cz):bam_atan2(s->facing[0],s->facing[2]);
        p->yaw=(yaw+32768)/BAM;p->visual_angles[0]=p->visual_angles[2]=0;p->visual_angles[1]=yaw;
        for(int j=0;j<3;j++){p->body_centres[j][0]=p->x;p->body_centres[j][1]=p->y+14.f;p->body_centres[j][2]=p->z;}p->body_valid=1;
        unsigned mask=s->style_mask&0xffffff,chosen=24;
        for(int pass=0;pass<2 && chosen==24;pass++)for(int j=0;j<24;j++) {
            p->spray_cursor=(p->spray_cursor+1)%24;
            unsigned style=p->spray_shuffle[p->spray_cursor],bit=1u<<style;
            /* 03b3a2 uses TST bit,bit; MOVT, not NOT bit. Since bit is
               nonzero, the original unused-style pass never accepts it.
               Retain the full 24-step pass and its fallback cursor. */
            if((mask&bit) && pass){chosen=style;p->spray_used|=bit;break;}
        }
        /* Only 17 style tables exist in this revision of the executable. */
        if(chosen>=17){g->mg_active=0;p->spray_active=p->spray_camera.active=0;p->spray_mode=p->spray_variant=0;return;}
        p->spray_style=chosen;
        SprayCamera *camera=&p->spray_camera;
        if(!camera->active || camera->style!=(int)chosen) {
            if(p->spray_subvolume) {
                const unsigned char *v=s->source ? s->source->volumes+(p->spray_subvolume-1)*176:s->camera_volume;
                unsigned char volume[176];memcpy(volume,v,sizeof volume);
                memcpy(volume+156,p->spray_subject,12);spray_camera_begin(camera,chosen,volume);
            }
            else {*camera=(SprayCamera){.active=1,.style=chosen};}
        }
        camera->authored=!!p->spray_subvolume;
        p->spray_mode=2;
        set_motion(p,0x80+player_random(p,4));p->motion_done=0;return;
    }
    if(action==SPRAY_ACT_IDLE) {
        if(p->motion_done)set_motion(p,0x7c+player_random(p,4));
        return;
    }
    if(action==SPRAY_ACT_CUE) {
        if((unsigned)cue>=11 || (unsigned)p->spray_style>=17)return;
        unsigned mask=SPRAY_MOTION_MASK[cue],indices[11],n=0;
        for(unsigned j=0;j<11;j++)if(mask&(1u<<j))indices[n++]=j;
        if(n)set_motion(p,SPRAY_STYLE_MOTION[p->spray_style][indices[player_random(p,n)]]);
        return;
    }
    if(action==SPRAY_ACT_MISS){set_motion(p,player_random(p,2) ? 0x84:0x85);return;}
    if(action==SPRAY_ACT_EXIT || action==SPRAY_ACT_INTERRUPT) {
        p->spray_active=p->spray_camera.active=0;p->spray_mode=p->spray_variant=0;p->tick_acc=0;p->pending_pressed=0;
        /* Damage may already have installed a fall/hit handler. A grip
           alone does not: 03b0fc -> 03c0f0 must leave spray control too. */
        if(action==SPRAY_ACT_INTERRUPT && p->state!=5){player_cam_reset(p);return;}
        /* 03bfa8 -> 06bd04(7) -> 06ce1c: return to control mode,
           motion 1 and cleared combo/boost/coast state; not walk-off. */
        p->state=1;p->sub=0;p->speed=p->vy=0;p->boost=p->boost_cool=0;
        p->ground_ticks=0;p->coast_blend=0;combo_reset(p);set_motion(p,1);
        if(cue)control_jump(p,0.f);
        player_cam_reset(p);return;
    }
    if(action==SPRAY_ACT_FINISH) {
        /* Leaving 03b0fc still completes this 0198f8 actor tick. EXIT
           alone also serves out-of-tick reset/teardown and must not do it. */
        p->score_add+=trick_hud_tick(&p->trick_hud,player_fsca_sin);
        animation_tick(p);visual_tick(p);effects_tick(p);
        if(p->sound_spatial)p->sound_spatial(p->sound_context);
        damage_clocks(p);p->logic_tick++;
        if(p->after_tick)p->after_tick(p->after_tick_context);
        if(p->camera_tick)p->cam_yaw=p->camera_tick(p->camera_context);
        return;
    }
    if(action==SPRAY_ACT_TICK && p->spray_active) {
        p->score_add+=trick_hud_tick(&p->trick_hud,player_fsca_sin);
        animation_tick(p);effects_tick(p);if(p->sound_spatial)p->sound_spatial(p->sound_context);
        /* 051d48 transforms the constant (0,10,0) by the visual root.
           It is not a head bone or the normal follow camera's +14 offset. */
        float matrix[16],focus[3];player_root_matrix(p,matrix);
        for(int j=0;j<3;j++)focus[j]=matrix[j*4+1]*10.f+matrix[j*4+3];
        SprayCamera *camera=&p->spray_camera;
        if(camera->authored)spray_camera_tick(camera,focus);
        else {
            float matrix[16];memcpy(matrix,p->spray_matrix,64);
            matrix[3]=p->x;matrix[7]=p->y;matrix[11]=p->z;
            if(!spray_tracks_sample(&spray_tracks,p->spray_style,camera->frame,matrix,camera->eye,camera->forward,
                                   &camera->roll,&camera->fov,&camera->frame))camera->active=0;
        }
        p->spray_subvolume=spray_subvolume(p,s);
        damage_clocks(p);p->logic_tick++;
        if(p->after_tick)p->after_tick(p->after_tick_context);
    }
}

/* Renderer diagnostic ONLY: the same teleport + original entry calls used
   by flycast_rail_trace.lua / flycast_wallride_trace.lua. No forced effects. */
void player_trace_fixture(Player *p,int wall)
{
    if (wall) {
        player_init(p,1093.134521f+6.f*.864423f,62.475628f,1502.664144f+6.f*.502765f);
        p->yaw=atan2f(-.502765f-.4322115f,.864423f-.2513825f);
    } else {
        player_init(p,1176.f,13.325f,1376.f);
        p->yaw=(-5651.f+32768.f)/BAM;
    }
    p->speed=1.2f;p->surf=6;
    if (wall) control_jump(p,0.f);else start_fall(p,1,74);
    player_cam_reset(p);
}
