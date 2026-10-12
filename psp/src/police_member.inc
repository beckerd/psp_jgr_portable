/* Ordinary Keikan: 076310/077638, state table 1b77a0 and animation
   table 1b77e0. World contacts use the port's collision adapter; controller
   distances, turning ramps, grab slots and animation clocks are source data. */
static unsigned police_next_token;

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_police_member_inc__police_clip_frames_0.inc"
#else

#include "generated/source_data/police_member_inc__POLICE_CLIP_FRAMES_0.inc"

#endif

int gang_police_motion(const GangMember *m)
{
    if(m->police.fire) {
        if(m->police.fire==25 && (m->anim==13 || m->anim==19))return 17;
        if(m->police.fire==25 && m->anim==14)return 18;
        if(m->police.fire==25 && m->anim==15)return 3;
        if((m->anim==16 || m->anim==17) && m->police.slot==0)return FIRE_FRONT_GRAB_MOTION;
        int clip=FIRE_MOTION[(unsigned)m->anim<24?m->anim:0];return clip<0?15:clip;
    }
    if(m->police.kairiki) {int clip=KAIRIKI_MOTION[(unsigned)m->anim<22?m->anim:0];return clip<0?10:clip;}
    if(m->police.bomb) {
        if((m->anim==16 || m->anim==17) && m->police.slot==0)return BOMB_FRONT_GRAB_MOTION;
        int clip=BOMB_MOTION[(unsigned)m->anim<22?m->anim:0];return clip<0?0:clip;
    }
    if(m->police.killer) {
        if((m->anim==16 || m->anim==17) && m->police.slot==0)return KILLER_FRONT_GRAB_MOTION;
        return KILLER_MOTION[(unsigned)m->anim<20?m->anim:0];
    }
    if(m->police.riot) {
        int pose=m->secondary_anim>=20 && m->secondary_anim<=22?m->secondary_anim:m->anim;
        if(pose>=20 && pose<=22)return m->speed!=0?RIOT_MOTION[1]:RIOT_MOTION[pose];
        if((m->anim==16 || m->anim==17) && m->police.slot==0)return RIOT_FRONT_GRAB_MOTION;
        return (unsigned)m->anim<23?RIOT_MOTION[m->anim]:RIOT_MOTION[0];
    }
    if(m->police.para) {
        int clip=(unsigned)m->anim<25 ? PARA_MOTION[m->anim]:21;
        if((m->anim==16 || m->anim==17) && m->police.slot==0)clip=27;
        return clip>=0?clip:21;
    }
    if(m->police.dog) {
        static const signed char slots[21]={6,13,-1,-1,7,8,-1,9,13,0,-1,-1,-1,-1,-1,-1,11,11,9,0,10};
        int slot=(unsigned)m->anim<21?slots[m->anim]:6;
        if(m->anim==1 && m->police.blend<=0 && m->speed>1.388888955f)slot=4;
        if((m->anim==16 || m->anim==17) && m->police.slot==0)slot=12;
        return slot>=0?DOG_MOTIONS[slot]:5;
    }
    
#if JGR_PORTABLE_ASSETS
#include "source_data_police_clips.inc"
#else

#include "generated/source_data/police_clips.inc"

#endif

    if(m->anim==16 || m->anim==17)return m->police.slot==0 ? 38:37;
    return (unsigned)m->anim<20 ? clips[m->anim]:27;
}
int gang_police_gait_motion(const GangMember *m)
{return m->police.fire?FIRE_GAIT_MOTION:m->police.kairiki?KAIRIKI_GAIT_MOTION:m->police.bomb?BOMB_GAIT_MOTION:m->police.killer?KILLER_GAIT_MOTION:m->police.riot?RIOT_GAIT_MOTION:m->police.para?PARA_GAIT_MOTION:10;}
static float police_motion_end(const GangMember *m)
{int clip=gang_police_motion(m);return (m->police.fire?FIRE_FRAMES[clip]:m->police.kairiki?KAIRIKI_FRAMES[clip]:m->police.bomb?BOMB_FRAMES[clip]:m->police.killer?KILLER_FRAMES[clip]:POLICE_CLIP_FRAMES[clip])-1.f;}
static int police_bam(float yaw){return (int)lrintf(yaw*10430.378350470453f)&65535;}
static float police_rad(int bam){return (bam&65535)*0.000095873799242853f;}
static int police_approach(int current,int target,int limit)
{int d=(short)(target-current);if(d>limit)d=limit;if(d< -limit)d= -limit;return (current+d)&65535;}
static void police_anim(GangMember *m,int anim)
{
    if(m->anim==anim)return;
    if(m->police.riot && m->anim==1)m->police.walk_frame=m->frame;
    m->police.old_anim=m->anim;m->police.old_motion=gang_police_motion(m);m->police.old_frame=m->frame;
    m->anim=anim;m->frame=anim==18 && !m->police.dog ? 11.f:0.f;
    m->police.blend=(anim==0 || anim==1 || anim==16) ? .8f:0.f;
}
#include "dog_member.inc"

static void police_animation_tick(GangMember *m,int changed)
{
    if(m->police.dog){dog_animation_tick(m,changed);return;}
    /* 076764/0768e8: each member owns its gait, even when offscreen. */
    int initial=m->anim;
    if(m->anim==0 && m->speed!=0.f)police_anim(m,1);
    else if(m->anim==1 && m->speed==0.f)police_anim(m,0);
    if(changed || initial!=m->anim)return;
    if(m->police.blend>0.f) {
        m->police.blend=fmaxf(0.f,m->police.blend-.2f);
        if(m->anim==16){if(m->police.blend==0)police_anim(m,17);return;}
    }
    if(m->anim==1) {
        float target=fminf(m->speed/1.388888955f,1.f);
        float d=target-m->police.gait;if(d>.1f)d=.1f;if(d<-.1f)d=-.1f;m->police.gait+=d;
        m->frame+=.7f+.3f*m->police.gait;
    } else m->frame+=m->anim==4 ? .5f:1.f;
    float end=police_motion_end(m);
    if(m->frame>=end) {
        switch(m->anim) {
        case 5:police_anim(m,6);break;
        case 18:police_anim(m,12);break;case 19:police_anim(m,15);break;
        case 10:case 11:police_anim(m,12);break;
        case 13:case 14:police_anim(m,15);break;
        case 12:case 15:police_anim(m,0);break;
        case 4:case 7:case 9:m->frame=end;break;
        default:if(end>0)m->frame-=end;break;
        }
    }
}
/* 074f30 (verified against original SH-4 for the no-wall branch). Returns
   remaining formation distance; never snaps a distant officer to a slot. */
static float police_follow(Gang *g,GangMember *m,const float *target,const float *player)
{
    const float *p=g->par;float dx=target[0]-m->pos[0],dz=target[2]-m->pos[2],d=sqrtf(dx*dx+dz*dz);
    m->flags&=~0x40;
    if(d<p[10]) {
        if(d<m->police.near) {
            m->police.near=p[13];d=fmaxf(d-1.f,0.f);m->speed=fminf(g->speed,d);
            dx=player[0]-m->pos[0];dz=player[2]-m->pos[2];
        } else {
            m->police.near=p[12];
            if(m->speed>g->speed)m->speed=fmaxf(0.f,m->speed-p[7]);
            else if(m->speed==0.f){dx=player[0]-m->pos[0];dz=player[2]-m->pos[2];}
        }
    } else if(!(m->flags&0x80)) {
        m->police.near=p[12];float limit=p[g->flags&2 ? 4:3];if(d-p[9]>limit+p[5])limit+=p[5];
        if(m->speed<limit)m->speed=fminf(limit,m->speed+p[6]);else m->speed=fmaxf(0.f,m->speed-p[7]);
    }
    if(d<p[9])m->flags|=0x40;
    int yaw=police_bam(m->yaw),heading=yaw;
    if(m->police.turn_wait>0){m->police.turn_wait--;heading=m->aim;}
    else {
        int wanted=police_bam(atan2f(dx,dz)),delta=abs((short)(wanted-yaw));
        if(delta<m->turn_step)m->turn_step=delta;
        else {m->turn_step+= (int)p[22];if(m->turn_step>delta)m->turn_step=delta;if(m->turn_step>(int)p[21])m->turn_step=(int)p[21];}
        heading=police_approach(yaw,wanted,m->turn_step);
    }
    m->aim=heading;float a=police_rad(heading);
    m->pos[0]+=sinf(a)*m->speed;m->pos[2]+=cosf(a)*m->speed;
    m->pos[1]+=m->police.vy;
    /* 0746f4 publishes the travel angle after collision integration. */
    m->yaw=a;return d;
}
static void police_release(Gang *g,GangMember *m)
{
    /* 0785a0: offsets at 1b7120, front angle 1b7158; slot ownership is
       retired before falling/getting up. */
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_police_member_inc__off_0.inc"
#else

#include "generated/source_data/police_member_inc__off_0.inc"

#endif

    int front=m->police.slot==0;const float *v=m->police.dog ? DOG_RELEASE+(front?0:3):off[front?0:1];
    float a=m->yaw+police_rad(32768+(front?(m->police.dog?0x111b:0x2239):0)),c=cosf(a),s=sinf(a);
    m->pos[0]+=c*v[0]+s*v[2];m->pos[1]+=v[1];m->pos[2]+=-s*v[0]+c*v[2];
    if(g->combat)police_grip_release(g->combat->grips,m->police.slot,m->police.token);
    m->police.slot=-1;m->state=front?18:17;m->flags=(m->flags|0x1000000)&~0x4000006;
    m->police.vy=.1f;m->visual_yaw=m->yaw;m->speed=0;
}
static int police_try_grab(Gang *g,GangMember *m)
{
    GangCombat *c=g->combat;if(!c || !c->grip_eligible)return 0;
    int slot=police_grip_claim(c->grips,m->police.token);if(slot<0)return 0;
    police_anim(m,16);m->police.slot=slot;m->police.jumps=10;m->police.distance=0;
    m->police.player_state=c->player_state;m->police.vy=m->speed=m->pitch=0.f;
    m->state=16;m->flags=(m->flags|0x4000000)&~6;
    /* +178 offsets initialize the pending position, not current display
       position. The next state-16 tick blends from the contact position. */
    return 1;
}
static void police_grab_tick(Gang *g,GangMember *m)
{
    GangCombat *c=g->combat;
    if(!c || !police_grip_touch(c->grips,m->police.slot,m->police.token) || !c->grip_anchors_valid) {police_release(g,m);return;}
    int (*obstacle)(void *,const float *,const float *,float *)=m->police.dog?c->dog_obstacle:c->police_obstacle;
    float before[3];memcpy(before,m->pos,12);memcpy(m->pos,c->grip_anchors[m->police.slot],12);
    if(m->anim==16) {for(int j=0;j<3;j++)m->pos[j]=(1.f-m->police.blend)*m->pos[j]+m->police.blend*before[j];return;}
    float d[3];for(int j=0;j<3;j++)d[j]=m->pos[j]-before[j];
    m->police.distance+=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    if(d[0]!=0 || d[2]!=0)m->yaw=police_rad(police_approach(police_bam(m->yaw),police_bam(atan2f(d[0],d[2])),910));
    if(m->police.player_state!=2 && c->player_state==2)m->police.jumps--;
    m->police.player_state=c->player_state;
    float normal[3];int wall=obstacle && obstacle(c->context,before,m->pos,normal);
    if(m->police.distance>200.f || !c->grip_eligible || (m->flags&0x10) || m->police.jumps<1 || wall)police_release(g,m);
}
static int police_contact_class(const GangMember *m,const SourceActorContact *c)
{
    /* 078f98: two signed dot products, not distance-only grabbing. */
    float d[3],v[3];for(int j=0;j<3;j++){d[j]=c->other_center[j]-m->pos[j];v[j]=c->other_center[j]-c->other_previous[j];}
    float facing=d[0]*sinf(m->yaw)+d[2]*cosf(m->yaw),moving=d[0]*v[0]+d[1]*v[1]+d[2]*v[2];
    return facing<0 ? (moving<0?3:1):(moving<0?0:2);
}
static void police_contact_tick(Gang *g,GangMember *m)
{
    GangCombat *c=g->combat;if(!c || !c->police_contact)return;
    switch(c->player_state){case 6:case 8:case 9:case 10:case 11:return;}
    SourceActorContact hit;if(!c->police_contact(c->context,m,&hit))return;
    if(g->flags&1)g->flags|=0x1000;
    int cls=police_contact_class(m,&hit),body;
    switch(hit.other_flags){case 0:case 1:case 2:case 4:case 5:case 6:case 12:case 13:case 15:case 16:body=1;break;default:body=0;}
    if((body && m->speed!=0 && cls!=3) || (!body && cls==2 && m->speed!=0)){police_try_grab(g,m);return;}
    if(g->kind==2 || cls>1 || (body && m->speed!=0))return; /* 078cec/e34: dogs do not stumble */
    /* 06de28 (weight .5): separation plus the recipient's velocity,
       then 078cec/078e34 normalize and choose front/back stumble/fall. */
    float x=hit.center[0]-hit.other_center[0],z=hit.center[2]-hit.other_center[2];
    if(x==0 && z==0)for(int j=0;j<3;j++)m->police.recoil[j]=hit.previous[j]-hit.center[j];
    else {
        float k=(hit.speed*.5f+hit.other_speed*.5f)/sqrtf(x*x+z*z);
        m->police.recoil[0]=((hit.center[0]-hit.previous[0])+x*k)*.7f;
        m->police.recoil[1]=0;
        m->police.recoil[2]=((hit.center[2]-hit.previous[2])+z*k)*.7f;
    }
    float n=0;for(int j=0;j<3;j++)n+=m->police.recoil[j]*m->police.recoil[j];
    if(n>0){n=1.f/sqrtf(n);for(int j=0;j<3;j++)m->police.recoil[j]*=n;}
    int hard=hit.other_speed-hit.speed>=1.85185194f;
    m->speed=hard?1.85185194f:.92592597f;m->police.vy=m->police.recoil[1]*m->speed;
    int turn=police_bam(atan2f(m->police.recoil[0],m->police.recoil[2]));
    m->yaw=police_rad(police_approach(police_bam(m->yaw),turn+(cls==0?32768:0),910));
    m->visual_yaw=m->yaw;
    m->state=cls==0 ? (hard?14:13):(hard?11:10);police_anim(m,m->state);m->flags|=0x1000000;
}
/* 073404..0737b4, Keikan attack record 1b72c4: initial delay 300,
   spacing 120, hold 90, maximum range 200. Group chase is not a command
   for every member to dive; only ready formation members receive bit 2. */
static void police_attack_tick(Gang *g,float distance,int off_graph,int disabled)
{
    GangCombat *c=g->combat;
    const unsigned *bomb=g->kind==23?BOMB_ATTACK_B:g->kind==5?BOMB_ATTACK_A:NULL;
    const void **owner=c ? (g->kind==2?&c->dog_attacking_group:&c->police_attacking_group):NULL;
    switch(g->fire_state) {
    case 0:g->fire_state=1;g->fire_delay=g->kind==2?DOG_ATTACK[2]:g->type==3?KILLER_ATTACK[2]:bomb?bomb[2]:300;g->fire_cursor=0;break;
    case 1:
        if(!(g->flags&2))break;
        if(owner && *owner && *owner!=g){g->flags&=~2;break;}
        if(owner)*owner=g;
        g->fire_state=2;
        {int ready=0;for(int i=0;i<g->nmem;i++)if(!(g->m[i].flags&0x20))ready|=g->m[i].flags&0x40;
         if(!ready){g->fire_delay=1;g->fire_cursor=g->nmem;}
         else {
             if(g->flags&0x800)g->fire_delay=1;
             if(g->kind==2 && c && c->sound)c->sound(c->context,(int)0xffff0007u,g->pos);
         }}
        break;
    case 2:
        if(distance>200 || off_graph || disabled || (g->flags&0x4000))g->fire_cursor=g->nmem;
        else if(!(g->flags&0x10) && --g->fire_delay<1) {
            g->fire_delay=(g->flags&0x800)?1:(g->kind==2?DOG_ATTACK[0]:g->type==3?KILLER_ATTACK[0]:bomb?bomb[0]:120);
            while(g->fire_cursor<g->nmem) {
                GangMember *m=g->m+g->fire_cursor++;
                if(!(m->flags&0x40) || (m->flags&0x20))continue;
                m->flags|=2;
                if(c && c->police_aim)c->police_aim(c->context,(int)g->par[20],m->police.recoil);
                else memcpy(m->police.recoil,g->target,12);
                break;
            }
        }
        if((g->flags&0x10) || g->fire_cursor>=g->nmem){g->flags|=8;g->speed=0;g->fire_state=3;g->fire_delay=0;}
        break;
    case 3:{
        if(g->fire_delay==0 && !(g->flags&0x10)) {
            int active=0;for(int i=0;i<g->nmem;i++)if(!(g->m[i].flags&0x20))active|=g->m[i].flags&2;
            if(!active){g->fire_state=0;g->flags&=~10;}
        }
        if(++g->fire_delay>(g->kind==2?DOG_ATTACK[1]:g->type==3?KILLER_ATTACK[1]:bomb?bomb[1]:90) || (g->flags&0x10)) {
            for(int i=0;i<g->nmem;i++) {
                GangMember *m=g->m+i;
                if(m->flags&2){m->flags|=0x80000000u;m->flags&=~2;}
                m->flags&=~4;
            }
            g->fire_state=4;g->fire_delay=0;
        }
        break;}
    case 4:{
        int active=0;if(g->fire_delay++<300)for(int i=0;i<g->nmem;i++)if(!(g->m[i].flags&0x20))active|=g->m[i].flags&0x80000000u;
        if(!active){g->fire_state=0;g->flags&=~14;}
        break;}
    }
}
#include "para_member.inc"
#include "riot_member.inc"
#include "burning_member.inc"

static void police_member_tick(Gang *g,GangMember *m,const float *player,float (*ground)(void *,float,float,float),void *ctx)
{
    if(m->flags&0x20)return;
    if(m->police.riot)m->visual_yaw=m->yaw;
    if(g->kind==2 && m->state<16)m->visual_yaw=m->yaw;
    int (*obstacle)(void *,const float *,const float *,float *)=g->combat?(m->police.dog?g->combat->dog_obstacle:g->combat->police_obstacle):NULL;
    int old_anim=m->anim;float old[3];memcpy(old,m->pos,12);memcpy(m->police.previous,old,12);
    if(!m->police.token){if(++police_next_token==0)++police_next_token;m->police.token=police_next_token;m->police.slot=-1;m->police.near=g->par[12];m->police.grounded=!m->police.para;}
    if((g->kind==2 || g->kind==4 || m->police.fire || m->police.kairiki) && !(g->flags&0x100))m->flags&=~0x100;
    if(!(g->flags&1))m->flags&=~1;
    if(g->flags&0x10)m->flags|=0x10;
    if(g->kind!=2 && (g->flags&0x10000) && m->state==3){m->state=9;police_anim(m,m->speed?1:2);}
    /* 0946d0 / 071260 rotate the authored dog formation by owner yaw
       plus 0x8000, both at creation and for subsequent pursuit targets. */
    float sign=(g->kind==2 || g->kind==4 || m->police.fire || m->police.kairiki || m->police.killer || m->police.bomb)?-1.f:1.f;
    float target[3]={g->pos[0]+sign*(m->off[0]*g->dir[2]+m->off[2]*g->dir[0]),g->pos[1],g->pos[2]+sign*(-m->off[0]*g->dir[0]+m->off[2]*g->dir[2])};
    int private_done=0;
    if(m->police.fire)private_done=fire_private_tick(g,m,target,player);
    else if(m->police.kairiki)private_done=kairiki_private_tick(g,m,target,player);
    else if(m->police.para)private_done=para_private_tick(g,m,target,player);
    else if(m->police.riot)private_done=riot_private_tick(g,m,target,player);
    else if(m->police.bomb) {
        if(m->state==19)private_done=1;
        else if(m->state==20) {
            police_contact_tick(g,m);
            if(!(m->flags&0x2000)){m->state=3;police_anim(m,0);}
            private_done=1;
        }
    }
    if(!private_done)switch(m->state) {
    case 0:
        m->state=1;m->anim=8;m->frame=0;memcpy(m->pos,target,12);
        if(g->kind==2) {
            memcpy(m->police.pace_origin,m->pos,12);m->police.pace_yaw=police_bam(atan2f(g->dir[0],g->dir[2]));
            m->police.head=16384;m->yaw=police_rad(m->police.pace_yaw+16384);m->speed=.648148179f;
        }
        break;
    case 1:
        if(m->flags&0x10){m->state=3;police_anim(m,1);}
        else if(!(m->flags&1)){m->state=2;police_anim(m,9);}
        else if(g->kind==2)dog_pace(m,g->par);else police_contact_tick(g,m);
        break;
    case 2:if(m->frame>=(g->kind==2?17.f:g->kind==4?28.f:(m->police.killer || m->police.bomb || m->police.kairiki)?police_motion_end(m):20.f)){m->state=3;m->flags&=~0x100;police_anim(m,0);}break;
    case 3:
        police_follow(g,m,target,player);
        if(m->speed==0 && m->police.vy==0){if(++m->timer>30){m->flags|=0x80;m->timer=0;}}else m->timer=0;
        if(m->flags&2){m->state=4;m->flags&=~0x8000000;}
        else if(m->police.bomb && (m->flags&0x2000)){m->state=20;police_anim(m,21);m->flags&=~0x8000000;m->speed=0;}
        else {
            police_contact_tick(g,m);
            if(m->police.bomb) {
                float x=player[0]-m->pos[0],y=player[1]-m->pos[1],z=player[2]-m->pos[2],d=x*x+y*y+z*z;
                if(d<400.f || ((m->flags&0x8000000) && d<625.f))m->flags|=0x8000000;else m->flags&=~0x8000000;
            }
        }
        break;
    case 4:
        if(g->kind==2 && (g->flags&0x800)) {
            for(int j=0;j<3;j+=2)m->police.recoil[j]+=(g->combat && g->combat->random15 ? g->combat->random15(g->combat->context):16384)*(40.f/32768.f)-20.f;
        }
        m->yaw=police_rad(police_approach(police_bam(m->yaw),police_bam(atan2f(m->police.recoil[0]-m->pos[0],m->police.recoil[2]-m->pos[2])),0x2aaa));
        m->speed=g->par[19]/g->par[20];
        if(g->kind==2 && (g->flags&0x800)) {
            float x=m->police.recoil[0]-m->pos[0],z=m->police.recoil[2]-m->pos[2];m->speed=sqrtf(x*x+z*z)/g->par[20];
        }
        m->police.vy=(g->kind==2?m->police.vy:0.f)+g->par[17]*g->par[20]*.5f;
        m->flags|=0x82;m->police.grounded=0;m->state=5;police_anim(m,4);
        if(g->kind!=23 && g->combat && g->combat->sound)g->combat->sound(g->combat->context,g->kind==2?(int)0xffff000bu:m->police.killer?0x30003:m->police.bomb?(g->kind==5?0x50001:-1):4,m->pos);
        /* 077cdc advances 075328 on the entry tick too. */
        /* fall through */
    case 5:{
        m->pos[0]+=sinf(m->yaw)*m->speed;m->pos[2]+=cosf(m->yaw)*m->speed;m->pos[1]+=m->police.vy;
        m->police.vy=fmaxf(-g->par[18],m->police.vy-g->par[17]);
        SourceActorContact contact;
        if(g->combat && g->combat->police_contact && g->combat->police_contact(g->combat->context,m,&contact))m->flags|=4;
        if(m->police.bomb && g->kind==5) {
            if(m->police.grounded) {
                float x=player[0]-m->pos[0],y=player[1]-m->pos[1],z=player[2]-m->pos[2];
                if((m->flags&4) || x*x+y*y+z*z<=225.f) {
                    m->state=19;police_anim(m,20);m->speed=0;m->police.vy=0;
                    if(g->combat && g->combat->explosion)g->combat->explosion(g->combat->context,m->pos,0);
                    break;
                }
            }
        } else if((m->flags&4) && police_try_grab(g,m))break;
        if(m->police.grounded){m->state=g->kind==2?19:6;police_anim(m,5);m->speed=0;if(g->kind!=2)m->flags|=0x2000000;
            if(g->kind!=23 && g->combat && g->combat->sound)g->combat->sound(g->combat->context,g->kind==2?(int)0xffff000bu:m->police.killer?0x30009:m->police.bomb?(g->kind==5?0x50002:-1):13,m->pos);}
        break;}
    case 6:m->pos[1]+=m->police.vy;if(m->flags&0x80000000u){m->state=7;police_anim(m,7);}break;
    case 7:if(m->frame>=((m->police.killer || m->police.bomb)?police_motion_end(m):POLICE_CLIP_FRAMES[31]-1)){m->state=8;m->timer=0;m->flags&=~0x3000082;police_anim(m,1);}break;
    case 8:if(police_follow(g,m,target,player)<=g->par[10] || ++m->timer>300){m->state=3;m->flags&=~0x80000002;m->timer=0;}break;
    case 9:if(m->speed!=0)police_follow(g,m,target,player);m->speed=fmaxf(0,m->speed-g->par[7]);
        if(!(g->flags&0x10000) || (m->flags&0x10)){m->state=3;police_anim(m,0);}else if(!m->police.kairiki)police_contact_tick(g,m);break;
    case 10:case 11:case 13:case 14:
        for(int j=0;j<3;j++)m->pos[j]+=m->police.recoil[j]*m->speed;
        m->speed=fmaxf(0,m->speed-.09259259f);
        if(m->anim==12)m->state=12;else if(m->anim==15)m->state=15;break;
    case 12:case 15:
        m->pos[1]+=m->police.vy;if(m->anim==0){m->state=3;m->flags&=~0x1000000;m->speed=0;}break;
    case 16:police_grab_tick(g,m);break;
    case 19: /* Dog 094ff0: land, then watch or get up. */
        if(m->frame==DOG_CLIP_FRAMES[gang_police_motion(m)]-1.f) {
            if(m->flags&4){m->state=20;police_anim(m,20);}
            else {m->state=21;police_anim(m,7);m->flags&=~2;}
        }
        m->police.vy=.01f;break;
    case 20:
        if(m->flags&0x80000000u){m->state=21;police_anim(m,7);}m->police.vy=.01f;break;
    case 21:
        m->flags&=~0x3000000;
        if(m->frame==DOG_CLIP_FRAMES[gang_police_motion(m)]-1.f){m->state=8;police_anim(m,1);m->timer=0;}
        m->police.vy=.01f;break;
    case 17:
        if(g->kind==2){dog_release_fall(m,18);break;}
        m->pos[1]+=m->police.vy;m->yaw=m->visual_yaw;
        if(m->anim==16 || m->anim==17)police_anim(m,18);else if(m->anim!=18)m->state=12;break;
    case 18:
        if(g->kind==2){dog_release_fall(m,19);break;}
        m->pos[1]+=m->police.vy;
        if(m->anim==16 || m->anim==17){police_anim(m,19);m->yaw+=police_rad(0x2239);}else if(m->anim!=19){m->state=15;m->yaw=m->visual_yaw;}break;
    }
    if(!(m->police.bomb && m->anim==20) && m->state!=16 && !(m->police.para && (m->flags&0x100))) {
        if(m->police.slot>=0){if(g->combat)police_grip_release(g->combat->grips,m->police.slot,m->police.token);m->police.slot=-1;}
        if(obstacle && m->speed!=0) {
            float normal[3];if(obstacle(g->combat->context,old,m->pos,normal)) {
                m->pos[0]=old[0];m->pos[2]=old[2];m->flags|=0x200;
                float d=sinf(m->yaw)*normal[2]-cosf(m->yaw)*normal[0];
                m->aim=police_bam(atan2f(d<0?-normal[2]:normal[2],d<0?normal[0]:-normal[0]));m->police.turn_wait=15;
                if(m->state==5)m->speed=0;
            }
        }
        int moved=memcmp(old,m->pos,12)!=0;
        if((g->combat && g->combat->police_floor) || ground) {
            if(moved || !m->police.grounded || (m->flags&0x80)) {
                float y=0;int hit;
                if(g->combat && g->combat->police_floor)
                    hit=g->combat->police_floor(g->combat->context,old,m->pos,m->flags,&y);
                else {y=ground(ctx,m->pos[0],fmaxf(old[1],m->pos[1]),m->pos[2]);hit=m->pos[1]<=y;}
                if(hit && m->police.vy<=0){m->pos[1]=y;m->police.vy=0;m->police.grounded=1;m->flags&=~0x80;}
                else {m->police.grounded=0;m->flags|=0x80;if(m->state!=5)m->police.vy=fmaxf(-g->par[18],m->police.vy-g->par[17]);}
            }
        }
    }
    if(g->kind==2)m->police.gait=(m->pos[0]-old[0])*(m->pos[2]-old[2]);
    /* 077638 follows +260 until completion, independently of the animation
       gate. The sound task recalculates listener falloff in 0e6ee0. */
    if(m->police.fire && m->gun.audio_token && g->combat && g->combat->voice_sound)
        m->gun.audio_token=g->combat->voice_sound(g->combat->context,m->gun.audio_token,-1,m->pos);
    if(m->police.fire || m->police.kairiki) {
        if(!(m->flags&0x120)) {
            int current=m->anim;
            if(m->police.fire)fire_animation_tick(g,m,m->secondary_anim!=current);
            else kairiki_animation_tick(g,m,m->secondary_anim!=current);
            m->secondary_anim=current;
        }
    } else if(m->police.riot) {
        if(!(m->flags&0x120)) {
            int current=m->anim;riot_animation_tick(g,m,m->secondary_anim!=current);m->secondary_anim=current;
        }
    } else if(m->police.para) {
        /* 077638 dispatches the main state before 076530's animation. */
        if(!(m->flags&0x120)) {
            int current=m->anim;
            para_animation_tick(m,m->secondary_anim!=current);m->secondary_anim=current;
        }
    } else if(m->police.bomb && m->anim==20) {
        if(old_anim==20 && ++m->frame>=BOMB_BLAST_FRAMES-1.f) {
            m->flags|=0x20;int live=0;
            for(int i=0;i<g->nmem;i++)live|=!(g->m[i].flags&0x20);
            if(!live)g->flags|=0x20;
        }
    } else police_animation_tick(m,m->anim!=old_anim);
    if(g->kind==23 && !(m->flags&0x120) && m->anim!=20) {
        GangCombat *c=g->combat;
        enemy_hand_flames_tick(&m->police.flames,c?c->random15:NULL,c?c->context:NULL);
        if(m->police.flames.ready==2 && c && c->burning_hands)c->burning_hands(c->context,g,m);
    }
}
/* 0757cc: first nearby nonzero contact separates by half the overlap
   inside four units. Pairwise broadphase costs no character posing. */
static void police_separate(Gang *g)
{
    for(int i=0;i<g->nmem;i++) {
        GangMember *a=g->m+i;if(a->state<3 || a->state>=10 || (a->flags&0x20))continue;
        for(int j=0;j<g->nmem;j++)if(i!=j) {
            GangMember *b=g->m+j;if(b->flags&0x20)continue;
            float x=a->pos[0]-b->pos[0],y=a->pos[1]-b->pos[1],z=a->pos[2]-b->pos[2];
            if(fabsf(x)>=4 || fabsf(y)>=4 || fabsf(z)>=4)continue;
            float d=sqrtf(x*x+y*y+z*z);if(d>0 && d<4){float k=(4-d)*.5f/d;a->pos[0]+=x*k;a->pos[1]+=y*k;a->pos[2]+=z*k;break;}
        }
    }
}
