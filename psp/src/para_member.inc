/* Para: private states 1/19/20/4/5/21 at 07c88c..07cd84.
   The other states and formation movement share 077638/074f30 with Keikan. */
static void para_animation_tick(GangMember *m,int changed)
{
    if(changed) {
        /* 0768e8 enters the gait cycle with one source frame. */
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
    float end=PARA_FRAMES[gang_police_motion(m)]-1.f;
    if(m->frame<end)return;
    switch(m->anim) {
    case 21:case 22:case 23:m->frame=end;break;
    case 24:police_anim(m,0);break;
    case 10:case 11:case 18:police_anim(m,12);break;
    case 13:case 14:case 19:police_anim(m,15);break;
    case 12:case 15:police_anim(m,0);break;
    default:if(end>0)m->frame-=end;break;
    }
}
static void para_face(Gang *g,GangMember *m,const float *player)
{
    /* 07543c rotates the body; it does not translate or snap to a slot. */
    float delta[3]={player[0]-m->pos[0],player[1]-m->pos[1],player[2]-m->pos[2]};
    int a[3]={police_bam(m->pitch),police_bam(m->yaw),0};
    if((m->flags|g->flags)&2) {
        int wanted[3]={a[0],a[1],0};heli_vector_angles(delta,wanted);
        a[0]=heli_turn(a[0],wanted[0],(int)g->par[21]);
        a[1]=heli_turn(a[1],wanted[1],(int)g->par[21]);
    } else {
        a[0]=heli_turn(a[0],0,(int)g->par[21]);
        a[1]=heli_turn(a[1],heli_atan(g->dir[0],g->dir[2]),(int)g->par[21]);
    }
    m->pitch=police_rad(a[0]);m->yaw=police_rad(a[1]);
}
static int para_private_tick(Gang *g,GangMember *m,const float *target,const float *player)
{
    switch(m->state) {
    case 0: /* 07c7c4 retains the creator's airborne position. */
        m->state=1;m->anim=20;m->frame=0;m->pitch=m->roll=0;return 1;
    case 1: /* 07c88c */
        m->flags|=0x100;
        if(m->flags&0x10)m->flags&=0x20;
        else if(!(m->flags&1)) {
            m->state=19;police_anim(m,20);m->police.vy=-1.388888955f;
            m->speed=.925925970f;m->flags&=~0x100;m->police.grounded=0;
        }
        return 1;
    case 19: /* 07c918: the literal is bfb1c71d in BOTH descent states. */
        if(!(m->flags&0x80)){m->state=20;police_anim(m,21);m->speed=.648148179f;}
        m->police.vy=-1.388888955f;police_follow(g,m,target,player);m->flags|=0x40;return 1;
    case 20: /* 07c98c: finish this member's landing before pursuit. */
        if(m->frame>=PARA_FRAMES[gang_police_motion(m)]-1.f) {
            m->state=3;police_anim(m,1);m->flags&=~0x100000;
        }
        m->speed=fmaxf(0,m->speed-g->par[7]);police_follow(g,m,target,player);return 1;
    case 4: /* 07ca08: stand and raise the weapon, never a police dive. */
        m->gun=(EnemyGun){.delay=(int)PARA_ATTACK[7],.left=(int)PARA_ATTACK[3],.first=1};
        m->state=5;m->flags&=~0x60000000u;police_anim(m,22);para_face(g,m,player);
        if(g->combat && g->combat->sound)g->combat->sound(g->combat->context,0x7000c,m->pos);
        return 1;
    case 5: { /* 07ca9c: held burst orders, five shots at six-tick spacing. */
        GangCombat *c=g->combat;EnemyGun *gun=&m->gun;para_face(g,m,player);
        if(--gun->delay<1) {
            if(!(m->flags&0x80000000u)) {
                if(!(g->flags&2)){m->flags&=~2;m->state=21;gun->delay=0;}
            } else {
                float from[3]={m->pos[0],m->pos[1]+10.f,m->pos[2]};
                int mode=gun->first || !(m->flags&0x20000000u)?0:1;
                if(c && c->shot)c->shot(c->context,from,c->target,7,mode);
                gun->first=0;
                if(gun->left==(int)PARA_ATTACK[3]) {
                    police_anim(m,23);m->frame=0;
                    if(c && c->sound)c->sound(c->context,0x7000d,m->pos);
                }
                if(--gun->left<1){m->flags&=~0xe0000000u;gun->left=PARA_ATTACK[3];gun->delay=PARA_ATTACK[7];}
                else gun->delay=PARA_ATTACK[5];
            }
        } else if((unsigned)gun->delay<=PARA_ATTACK[8] && c) {
            if(!(m->flags&0x40000000u)) {
                float from[3]={m->pos[0],m->pos[1]+10.f,m->pos[2]};
                if(c->shot)gun->probe=c->shot(c->context,from,c->target,7,2);
                m->flags|=0x60000000u;
            } else if(m->flags&0x20000000u) {
                if((!c->shot_result || !c->shot_result(c->context,gun->probe)) && c->vulnerable)c->warning=1;
                else m->flags&=~0x20000000u;
            }
        }
        return 1;
    }
    case 21: /* 07cd84 */
        if(m->anim==23)police_anim(m,24);
        else if(m->anim!=24){m->state=3;police_anim(m,0);}
        return 1;
    default:return 0;
    }
}
static void para_attack_tick(Gang *g,float distance,int off_graph,int disabled)
{
    /* 07425c/0742a0/074378/074408, 1b829c. Every member keeps its own
       burst clock; the leader issues at most five ready-member orders. */
    switch(g->fire_state) {
    case 0:g->fire_state=1;g->fire_cursor=0;break;
    case 1:if(g->fire_clock>=(int)PARA_ATTACK[0] && (g->flags&2)) {
        /* 0742a0 scans the live same-family groups before member readiness. */
        if(g->combat) {
            const Gang *groups=g->combat->formation_groups;
            int other=0;
            for(int i=0;groups && i<g->combat->formation_group_count;i++)
                if(groups+i!=g && groups[i].active && groups[i].type==7 && (groups[i].flags&2))other=1;
            if(other){g->flags&=~2;break;}
        }
        int busy=0;for(int i=0;i<g->nmem;i++)busy|=g->m[i].flags&2;
        if(busy){g->flags&=~2;g->fire_clock=PARA_ATTACK[0]/2;}
        else g->fire_state=2;
        }break;
    case 2:g->flags|=8;g->fire_left=PARA_ATTACK[6];g->fire_cursor=0;g->fire_delay=0;g->fire_state=3;break;
    case 3:
        if((g->flags&0x10) || !(g->flags&2)){g->flags&=~2;g->fire_state=0;break;}
        if(g->fire_clock>=(int)PARA_ATTACK[0]) {
            g->fire_clock=0;g->flags|=0x80000000u;g->fire_left=PARA_ATTACK[6];
        }
        if(!(g->flags&0x80000000u)) {
            if(disabled || off_graph || (g->flags&0x4000) || distance>100.f)g->flags&=~2;
            g->fire_cursor=0;g->fire_left=PARA_ATTACK[6];g->fire_delay=PARA_ATTACK[1];
        } else if(g->fire_left<1 || g->fire_cursor>=g->nmem || disabled) {
            g->flags&=~0x80000000u;g->fire_cursor=0;
        } else if(++g->fire_delay>=(int)PARA_ATTACK[1]) {
            g->fire_delay=0;GangMember *m=g->m+g->fire_cursor++;
            if(m->flags&0x40){m->flags|=0x80000002u;g->fire_left--;}
        }
        break;
    }
    g->fire_clock++;
}
