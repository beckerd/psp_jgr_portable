/* Fire 07dfdc..07e694 and Kairiki 08d8c4..08d944. These families share
   077638's member integration, but neither uses the ordinary tackle AI. */
static unsigned burning_random(Gang *g)
{return g->combat && g->combat->random15?g->combat->random15(g->combat->context):32767;}
static void burning_face(Gang *g,GangMember *m,const float *player)
{
    int target=police_bam(atan2f(player[0]-m->pos[0],player[2]-m->pos[2]));
    if(!((g->flags|m->flags)&2))target=police_bam(atan2f(g->dir[0],g->dir[2]));
    m->visual_yaw=m->yaw;m->yaw=police_rad(police_approach(police_bam(m->yaw),target,(int)g->par[21]));
}
static void fire_command(Gang *g,GangMember *m,int command)
{
    GangCombat *c=g->combat;
    m->gun.first=c && c->fire_stream?c->fire_stream(c->context,g,m,command,m->gun.left):6;
    if(command==-1)m->gun.first=0;
}
void gang_fire_overlay(const GangMember *m,int *pitch,float *pf,int *yaw,float *yf)
{
    *pitch=*yaw=-1;*pf=*yf=0;
    if(!m->police.fire)return;
    if((m->state==5 || m->state==21) && m->police.head) {
        *pitch=FIRE_AIM_MOTION[m->police.head>0];
        *pf=fminf(abs(m->police.head)/16384.f,1.f)*(FIRE_FRAMES[*pitch]-1);
    }
    if((m->state==21 || m->state==22) && m->aim_step) {
        int a=(short)m->aim_step;*yaw=FIRE_AIM_MOTION[2+(a>0)];
        *yf=fminf(abs(a)/16384.f,1.f)*(FIRE_FRAMES[*yaw]-1);
    }
}
static void fire_aim(GangMember *m,const float *target)
{
    float dx=target[0]-m->pos[0],dz=target[2]-m->pos[2];
    m->police.head=(short)police_bam(atan2f(target[1]-m->pos[1]-10.f,hypotf(dx,dz)));
}
static int fire_private_tick(Gang *g,GangMember *m,const float *target,const float *player)
{
    switch(m->state) {
    case 1:
        if(m->flags&0x10){m->state=3;police_anim(m,1);}
        else if(!(m->flags&1)) {m->state=2;police_anim(m,9);fire_command(g,m,-1);m->gun.left=30;fire_command(g,m,1);}
        else {police_contact_tick(g,m);if(burning_random(g)*(1.f/32768.f)<.0016666667f)m->state=20;}
        return 1;
    case 2:
        if(m->frame>=police_motion_end(m) && (m->gun.first&2)) {fire_command(g,m,-1);m->state=3;police_anim(m,0);}
        return 1;
    case 3:
        police_follow(g,m,target,player);
        if(m->flags&2)m->state=4;
        else if(m->flags&0x2000){m->state=23;police_anim(m,23);m->speed=0;m->flags&=~0x8000000;}
        else {police_contact_tick(g,m);if((m->flags&0x40) && m->speed==0 && burning_random(g)*(1.f/32768.f)<.0016666667f)m->state=20;}
        return 1;
    case 4:
        m->state=5;police_anim(m,20);fire_command(g,m,-1);
        m->gun.left=g->kind==25?150:90;
        m->police.pace_end=g->combat?(g->combat->history_at+27)%30:0;
        burning_face(g,m,player);if(g->combat)g->combat->report=1;return 1;
    case 5:
        burning_face(g,m,player);
        if(g->combat) {g->combat->report=1;fire_aim(m,g->combat->history[m->police.pace_end++%30]);}
        if(m->anim==22)m->state=19;
        else if((m->flags&0x2010) || (g->combat && g->combat->police_attack)){fire_command(g,m,2);m->state=19;}
        return 1;
    case 19:
        if(m->anim!=22){m->state=3;police_anim(m,0);m->flags&=~2;}
        return 1;
    case 20: {
        m->state=21;police_anim(m,20);fire_command(g,m,-1);
        m->police.pace_yaw=m->gun.delay?((m->police.pace_yaw+1)&3):(int)(burning_random(g)/8192);
        m->gun.delay++;
        const float *v=FIRE_IDLE_TARGETS[m->police.pace_yaw];m->gun.left=(int)v[3];
        float c=cosf(m->yaw),s=sinf(m->yaw),p[3]={m->pos[0]-c*v[0]-s*v[2],m->pos[1]+v[1],m->pos[2]+s*v[0]-c*v[2]};
        fire_aim(m,p);m->aim=(short)(police_bam(atan2f(p[0]-m->pos[0],p[2]-m->pos[2]))-police_bam(m->yaw));m->aim_step=0;return 1;
    }
    case 21:
        if(m->anim==22 || (m->flags&0x2010)) {if(m->flags&0x2010)fire_command(g,m,2);m->state=22;m->timer=0;m->aim=0;}
        return 1;
    case 22:
        if(m->anim!=22) {
            if(m->flags&0x2010){m->state=3;police_anim(m,0);m->gun.delay=0;}
            else if(g->speed==0 && m->gun.delay<4){if(++m->timer>=150)m->state=20;}
            else {m->state=m->flags&1?1:3;police_anim(m,m->flags&1?8:0);m->gun.delay=0;}
            m->flags&=~2;
        }return 1;
    case 23:police_contact_tick(g,m);if(!(m->flags&0x2000)){m->state=3;police_anim(m,0);}return 1;
    default:return 0;
    }
}
static void fire_animation_tick(Gang *g,GangMember *m,int changed)
{
    if(m->state==21 || m->state==22)m->aim_step=police_approach(m->aim_step,m->aim,0x71c);
    if(m->anim>=20 && m->anim<=23) {
        if(changed) {
            m->frame=0;
            if(m->anim==21){
                fire_command(g,m,1);
                /* 07d678: retain +260, and don't overlap a live voice. */
                if(g->combat && g->combat->voice_sound)
                    m->gun.audio_token=g->combat->voice_sound(g->combat->context,m->gun.audio_token,0x90001,m->pos);
            }
        } else {
            float end=police_motion_end(m);m->frame+=1;
            if(m->anim==21 || m->anim==23) {if(m->frame>=end+1.f)m->frame-=end+1.f;}
            else if(m->frame>end) {
                m->frame=end;
                if(m->anim==20)police_anim(m,21);
                else if((m->gun.first&2) || !m->gun.probe){fire_command(g,m,-1);police_anim(m,m->flags&1?8:0);}
            }
            if(m->anim==21 && (m->gun.first&4))police_anim(m,22);
        }
    } else police_animation_tick(m,changed);
    if(m->gun.probe)fire_command(g,m,0);
}
/* 073e60: sequential five-member volley, shared with the linked formation. */
static void fire_attack_tick(Gang *g,int off_graph,int disabled)
{
    GangCombat *c=g->combat;Gang *pool=c?c->formation_groups:NULL;
    switch(g->fire_state) {
    case 0:g->fire_state=1;g->fire_delay=15;g->fire_cursor=0;g->fire_left=5;break;
    case 1:
        if(!g->formation_lead && (g->flags&2))for(int i=0;pool && i<c->formation_group_count;i++) {
            Gang *p=pool+i;if(p!=g && p->active && p->type==g->type && (p->flags&2)){g->flags&=~2;break;}
        }
        if(g->flags&2){g->fire_state=2;g->flags|=8;}break;
    case 2:if(--g->fire_delay<1){g->fire_state=3;g->fire_delay=0;}break;
    case 3:
        if(g->flags&0x10){g->fire_state=4;break;}
        if(disabled || off_graph || (g->flags&0x4000)){g->fire_cursor=g->nmem;g->fire_delay=0;}
        if(--g->fire_delay<1) {
            g->fire_delay=1;
            if(g->fire_cursor<g->nmem && (g->m[g->fire_cursor].flags&0x40)){g->m[g->fire_cursor++].flags|=0x80000002u;g->fire_left--;}
            if(g->fire_cursor>=g->nmem) {
                if(g->formation_next && g->fire_left>0){g->formation_next->fire_left=g->fire_left;g->formation_next->flags|=0x80000002u;g->fire_left=0;}
                g->fire_state=4;g->fire_delay=0;
            }
        }break;
    case 4:
        if(!(g->flags&0x10) && (g->flags&2)) {
            for(int i=0;i<g->nmem;i++)if(g->m[i].flags&2)return;
            g->flags&=~0x80000000u;
            if(!g->formation_lead) {
                for(Gang *p=g->formation_next;p;p=p->formation_next)if(p->flags&0x80000000u)return;
                g->flags&=~2;for(Gang *p=g->formation_next;p;p=p->formation_next)p->flags&=~2;
            }
        } else {g->fire_state=0;g->flags&=~0x8000000au;}
        break;
    }
}
int gang_kairiki_body(const GangMember *m)
{
    if(!m->police.kairiki)return -1;
    if(m->anim==20 && m->frame>=20 && m->frame<63)return 0;
    if(m->anim==21 && m->frame>=14 && m->frame<33)return 1;
    return -1;
}
static int kairiki_private_tick(Gang *g,GangMember *m,const float *target,const float *player)
{
    switch(m->state) {
    case 1:if(m->flags&0x10){m->state=3;police_anim(m,1);}else if(!(m->flags&1)){m->state=2;police_anim(m,9);}return 1;
    case 3:police_follow(g,m,target,player);if(g->flags&2){m->state=4;m->timer=0;}return 1;
    case 4:if(++m->timer>15){police_anim(m,4);m->state=5;}burning_face(g,m,player);return 1;
    case 5:if(!(g->flags&2))m->state=3;return 1;
    default:return 0;
    }
}
static void kairiki_animation_tick(Gang *g,GangMember *m,int changed)
{
    if(m->anim==4) {
        int high=g->combat && g->combat->target[1]-m->pos[1]>5.f;
        police_anim(m,high?20:21);m->police.head=high;
        m->police.pose_frame=m->police.gas_pose_frame=-1;
        if(g->combat && g->combat->sound)g->combat->sound(g->combat->context,high?0x60002:0x60003,m->pos);
    } else if(m->anim==20 || m->anim==21) {
        int high=m->anim==20;float f=++m->frame;
        if(m->police.pose_frame>=0 && ++m->police.pose_frame>(high?12:18))m->police.pose_frame=-1;
        if(m->police.gas_pose_frame>=0 && ++m->police.gas_pose_frame>17)m->police.gas_pose_frame=-1;
        if(f==(high?20:14))m->police.pose_frame=0;
        if(high && f==40)m->police.head=2;
        if(high && f==47)m->police.gas_pose_frame=0;
        if(f>police_motion_end(m)){police_anim(m,0);g->flags&=~2;m->police.pose_frame=m->police.gas_pose_frame=-1;}
        if(g->combat && g->combat->strong_attack && (m->police.pose_frame>=0 || m->police.gas_pose_frame>=0))g->combat->strong_attack(g->combat->context,g,m);
    } else {
        police_animation_tick(m,changed);
        if(m->anim==1 && ((m->frame>=4 && m->frame<5) || (m->frame>=16 && m->frame<17)) && g->combat && g->combat->gun_sound)
            m->gun.audio_token=g->combat->gun_sound(g->combat->context,m->gun.audio_token,0x60007,m->pos);
    }
}
