/* Dog-private state/animation handlers 093fdc..09521c. Common pursuit,
   leap launch, body contact and grip ownership are shared in the original. */
float gang_dog_turn_frame(const GangMember *m)
{
    int head=m->police.head;
    return head>0 ? 9.f*(16384-head)/16384.f : 19.f-(head+16384)*10.f/16384.f;
}
static void dog_animation_tick(GangMember *m,int changed)
{
    if(changed) {
        /* 09411c advances the walk clock once while entering the blend. */
        if(m->anim==1)m->frame=1.f;
        return;
    }
    if(m->anim==0) {
        if(m->police.blend>0)m->police.blend=fmaxf(0,m->police.blend-.2f);
        else m->frame+=1.f;
        if(m->police.gait!=0 || police_bam(m->visual_yaw)!=police_bam(m->yaw))police_anim(m,1);
    } else if(m->anim==1) {
        if(m->speed==0 && police_bam(m->visual_yaw)==police_bam(m->yaw)){police_anim(m,0);return;}
        if(m->police.blend>0)m->police.blend=fmaxf(0,m->police.blend-.2f);
        m->frame+=1.f;
    } else if(m->anim==16) {
        m->police.blend-=.2f;
        if(m->police.blend<=0){m->police.blend=0;police_anim(m,17);}
        return;
    } else m->frame+=m->anim==4?14.f/21.f:1.f;
    float end=DOG_CLIP_FRAMES[gang_police_motion(m)]-1.f;
    if(m->frame>=end) {
        switch(m->anim) {
        case 4:case 5:case 7:case 9:case 18:case 19:m->frame=end;break;
        default:if(end>0)m->frame-=end;break;
        }
    }
}
static void dog_pace(GangMember *m,const float *par)
{
    /* 094cf8/09521c: two points, +/-20 along the spawn owner's X axis.
       Travel steers by 0x444; the turn pose approaches by 0x38e. */
    if(m->flags&0x40000){m->speed=0;return;}
    m->speed=.648148179f;
    float a=police_rad(m->police.pace_yaw),x=m->police.pace_end?-20.f:20.f;
    float dx=m->police.pace_origin[0]+cosf(a)*x-m->pos[0];
    float dz=m->police.pace_origin[2]-sinf(a)*x-m->pos[2];
    int heading=police_approach(police_bam(m->yaw),police_bam(atan2f(dx,dz)),0x444);
    if(sqrtf(dx*dx+dz*dz)<=par[12])m->police.pace_end=1-m->police.pace_end;
    float angle=police_rad(heading);
    m->pos[0]+=sinf(angle)*m->speed;m->pos[1]+=m->police.vy;m->pos[2]+=cosf(angle)*m->speed;
    int wanted=(short)(heading-m->police.pace_yaw);
    m->police.head=(short)police_approach(m->police.head,wanted,0x38e);
    if(m->police.head>16384)m->police.head=16384;
    if(m->police.head< -16384)m->police.head=-16384;
    m->yaw=angle;
}
static void dog_release_fall(GangMember *m,int anim)
{
    m->pos[1]+=m->police.vy;
    if(m->anim==16 || m->anim==17)police_anim(m,anim);
    if(!(m->flags&0x80)) {
        m->state=19;m->pitch=0;police_anim(m,5);m->speed=0;m->flags&=~4;
    }
    m->yaw=m->visual_yaw;
}
