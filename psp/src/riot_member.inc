/* Moving Kidoutai (type 4): 0798f4, 079df0/079e10 and 0739e4.
   Type 1 is the separate tackle squad; type 27 is an anchored launcher. */
int gang_riot_overlay(const GangMember *m)
{
    int anim=m->secondary_anim;
    if(!m->police.riot || anim<20 || anim>22)return -1;
    return m->speed!=0?RIOT_GAS_OVERLAY[anim-20]:m->overlay;
}
float gang_riot_overlay_frame(const GangMember *m)
{return m->speed!=0?m->police.gas_pose_frame:m->police.pose_frame;}
float gang_riot_frame(const GangMember *m)
{
    return m->secondary_anim>=20 && m->secondary_anim<=22
        ? (m->speed==0?m->police.gas_pose_frame:m->police.pose_frame):m->frame;
}

static int riot_private_tick(Gang *g,GangMember *m,const float *target,const float *player)
{
    if(m->state==4) { /* 079df0: arm, but continue following the formation. */
        m->state=5;m->gun.first=2;police_follow(g,m,target,player);return 1;
    }
    if(m->state!=5)return 0;
    police_follow(g,m,target,player); /* 079e10, including during a throw */
    if(m->flags&0x80000000u) {
        police_anim(m,20);m->flags&=~0x80000000u;
        if(++m->gun.first>=3)m->gun.first=0;
    }
    if(!(g->flags&2)){m->flags&=~2;m->state=3;m->timer=0;}
    return 1;
}

static void riot_gait_tick(GangMember *m)
{
    /* 077440: motion 23/7 at the same clock, with a ramped speed blend. */
    float wanted=fminf(m->speed/1.388888955f,1.f);
    m->police.gait+=fmaxf(-.1f,fminf(.1f,wanted-m->police.gait));
    m->police.pose_frame=m->police.walk_frame;
    m->police.walk_frame+=1.f;
    float end=RIOT_FRAMES[RIOT_MOTION[1]]-1.f;
    if(m->police.walk_frame>=end)m->police.walk_frame-=end;
}
static void riot_animation_tick(Gang *g,GangMember *m,int changed)
{
    m->overlay=-1;
    if(m->anim>=20 && m->anim<=22) {
        int anim=m->anim;
        if(changed) {
            m->secondary_frame=0;
            if(anim==21) { /* 079fb0 emits once on entry, at the member's position. */
                m->frame=0;
                GangCombat *c=g->combat;float from[3]={m->pos[0],m->pos[1]+10.f,m->pos[2]};
                if(c && c->gas)c->gas(c->context,from,m->gun.first);
                if(c && c->sound)c->sound(c->context,0x1000c,m->pos);
            }
        }
        if(m->speed!=0)riot_gait_tick(m);
        else if(anim==21) {
            /* 079fb0 keeps the throwing pose while the authored feet turn. */
            int delta=(short)(police_bam(m->visual_yaw)-police_bam(m->yaw));
            if(!delta)m->frame=0;
            else {
                m->overlay=RIOT_TURN_OVERLAY[delta>0];
                m->frame+=1.f;float end=RIOT_FRAMES[m->overlay]-1.f;
                if(m->frame>=end)m->frame-=end;
                m->police.pose_frame=m->frame;
            }
        }
        /* 20/22 use clock +1ec; 21 uses +1f4. A new clip advances on
           its entry tick. Pose overlays only replace authored channels. */
        m->police.gas_pose_frame=m->secondary_frame;
        m->secondary_frame+=1.f;
        int clip=anim==21?RIOT_GAS_OVERLAY[1]:anim==22?RIOT_GAS_OVERLAY[2]:RIOT_MOTION[20];
        if(m->secondary_frame>=RIOT_FRAMES[clip]-1.f) {
            if(anim<22)m->anim=anim+1;
            else {m->anim=m->speed!=0?1:0;m->frame=m->police.walk_frame;m->police.blend=0;}
        }
        return;
    }
    /* Shared 076764/0768e8 and damage/recovery handlers, with Kidoutai's
       own motion lengths. Do not run the Keikan bank's 21-frame entry. */
    if(changed) {
        if(m->anim==1){m->frame=1.f;m->police.gait=0;}
        return;
    }
    if(m->anim==0 && m->speed!=0){police_anim(m,1);return;}
    if(m->anim==1 && m->speed==0){police_anim(m,0);return;}
    float step=1.f;
    if(m->police.blend>0) {
        m->police.blend=fmaxf(0,m->police.blend-.2f);
        if(m->anim==16){if(m->police.blend==0)police_anim(m,17);return;}
        if(m->anim==1){m->police.gait=0;step=.7f;}
    } else if(m->anim==1) {
        float wanted=fminf(m->speed/1.388888955f,1.f);
        m->police.gait+=fmaxf(-.1f,fminf(.1f,wanted-m->police.gait));
        step=.7f+.3f*m->police.gait;
    }
    m->frame+=step;
    float end=RIOT_FRAMES[gang_police_motion(m)]-1.f;
    if(m->frame<end)return;
    switch(m->anim) {
    case 9:m->frame=end;break;
    case 10:case 11:case 18:police_anim(m,12);break;
    case 13:case 14:case 19:police_anim(m,15);break;
    case 12:case 15:police_anim(m,0);break;
    default:if(end>0)m->frame-=end;break;
    }
}

static void riot_unlink(Gang *g)
{
    if(g->formation_lead)g->formation_lead->formation_next=NULL;
    g->formation_lead=NULL;g->target_player=1;
}
static void riot_target(Gang *g)
{
    Gang *p=g->formation_lead;if(!p)return;
    float tail=(p->kind==3 || p->kind==9 || p->kind==25)?(p->nmem<3?7.f:p->nmem==3?8.f:33.f):(p->nmem<3?15.f:33.f); /* table 1b76e0, vector after the members */
    g->target[0]=p->pos[0]-tail*p->dir[0];g->target[1]=p->pos[1];
    g->target[2]=p->pos[2]-tail*p->dir[2];g->target_player=0;
}
static void riot_join(Gang *g,const float *player)
{
    /* 072538 joins only free same-family leaders in the same graph node.
       The one closer to the player leads; append to its existing tail. */
    if(g->formation_lead || !g->combat || g->node<0)return;
    Gang *pool=g->combat->formation_groups;
    for(int i=0;pool && i<g->combat->formation_group_count;i++) {
        Gang *p=pool+i;
        if(p==g || !p->active || p->type!=g->type || (p->flags&0x3001b) ||
           p->formation_lead || p->graph!=g->graph || p->node!=g->node)continue;
        float a=0,b=0;for(int j=0;j<3;j++){a+=(g->pos[j]-player[j])*(g->pos[j]-player[j]);b+=(p->pos[j]-player[j])*(p->pos[j]-player[j]);}
        Gang *lead=a>b?p:g,*tail=a>b?g:p;
        while(lead->formation_next)lead=lead->formation_next;
        lead->formation_next=tail;tail->formation_lead=lead;riot_target(tail);return;
    }
}
static void riot_attack_tick(Gang *g,int disabled)
{
    Gang *pool=g->combat?g->combat->formation_groups:NULL;
    Gang *previous=g->formation_lead,*next=g->formation_next;
    int total=g->nmem;for(Gang *p=next;p;p=p->formation_next)total+=p->nmem;
    switch(g->fire_state) {
    case 0:g->fire_state=1;g->fire_clock=0;g->fire_cursor=0;break;
    case 1: /* 073ad0: only the first subgroup arbitrates other encounters. */
        if(!previous && (g->flags&2))for(int i=0;pool && i<g->combat->formation_group_count;i++) {
            Gang *p=pool+i;
            if(p!=g && p->active && p->type==g->type && (p->flags&2)){g->flags&=~2;break;}
        }
        if(g->flags&2)g->fire_state=2;
        break;
    case 2: /* 073b40: five launch orders per volley, shared across the chain. */
        if(!previous){g->fire_left=5;g->burst_left=total/5+3;g->fire_delay=RIOT_ATTACK[1];}
        else {g->fire_clock=RIOT_ATTACK[0];g->fire_delay=0;}
        g->fire_state=3;break;
    case 3: /* 073bfc: source comparisons use the old, post-increment clock. */
        if((g->flags&0x10) || !(g->flags&2)){g->fire_state=0;g->flags&=~2;break;}
        if(!previous) {
            if(g->burst_left<=0 && !(g->flags&0x80000000u)) {
                int busy=0;
                for(Gang *p=next;p;p=p->formation_next)if(p->flags&0x80000000u)busy=1;
                if(!busy) {
                    g->flags&=~2;
                    for(Gang *p=next;p;p=p->formation_next)p->flags&=~2;
                }
                break;
            }
            if(g->fire_clock++>=(int)RIOT_ATTACK[0]){g->fire_clock=0;g->burst_left--;g->flags|=0x80000000u;}
        }
        if(!(g->flags&0x80000000u)) {
            g->fire_cursor=0;
            if(!previous){g->fire_left=5;g->fire_delay=RIOT_ATTACK[1];if(disabled || (g->flags&0x4000))g->burst_left=0;}
        } else if(g->fire_left<=0 || g->fire_cursor>=g->nmem || disabled)g->flags&=~0x80000000u;
        else if(g->fire_delay++>=(int)RIOT_ATTACK[1]) {
            g->fire_delay=0;g->m[g->fire_cursor++].flags|=0x80000002u;g->fire_left--;
            if(g->fire_cursor==g->nmem && next && g->fire_left>0){next->fire_left=g->fire_left;next->flags|=0x80000002u;}
        }
        break;
    }
}
