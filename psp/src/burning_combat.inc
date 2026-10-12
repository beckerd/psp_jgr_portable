/* Original 07e6d4 stream attachments and 08d56c strongman collision. */
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_burning_source.inc"
#else
#include "generated/source_data/burning_source.inc"
#endif
int enemycombat_prepare_burning(EnemyCombatWorld *w)
{
    if(!w->burning_attempted) {
        w->burning_attempted=1;w->burning_error=chars_load(&w->burning_models,"burning.jgc");
        if(!w->burning_error && (w->burning_models.h->n_chars!=13 || w->burning_models.h->n_tex!=10)){chars_free(&w->burning_models);w->burning_error=-2;}
        if(!w->burning_error){w->fires=calloc(ENEMY_FIRE_CAPACITY,sizeof *w->fires);if(!w->fires)w->burning_error=-3;}
        if(!w->burning_error && !enemycombat_prepare_flames(w))w->burning_error=-4;
        printf("BURNING_ASSETS load=%d pool=%u\n",w->burning_error,(unsigned)(ENEMY_FIRE_CAPACITY*sizeof(EnemyFire)));
    }
    return !w->burning_error;
}
static void burning_impact(EnemyCombatWorld *w,const float point[3])
{
    for(int i=0;i<ENEMY_BURN_CAPACITY;i++)if(!w->burns[i].life) {
        EnemyBurn *b=w->burns+i;memset(b,0,sizeof *b);memcpy(b->pos,point,12);
        unsigned r=random15(w);b->image=(r*8)>>15;b->mesh=r>=16384;b->life=300;w->burns_created++;return;
    }
    w->burns_dropped++;
}
static void burning_impacts_tick(EnemyCombatWorld *w)
{
    for(int i=0;i<ENEMY_BURN_CAPACITY;i++) {
        EnemyBurn *b=w->burns+i;if(!b->life)continue;
        enemy_burn_step(b,b->image==7?random15(w):0);
        if(b->state==3){b->life=0;continue;}
        /* A zero life marks a free slot; shrinking flames remain occupied. */
        if(!b->life)b->life=-1;
        if(b->state==1) {
            float d2=0;for(int j=0;j<3;j++){float d=b->pos[j]-w->gang.target[j];d2+=d*d;}
            if(d2<2500){float p[3]={b->pos[0],b->pos[1]+3,b->pos[2]};actor_contact(w,p,p,3.f,29);}
        }
    }
}
void enemycombat_fire_pose(const Chars *c,const JgcChar *ch,const GangMember *m,CharPose *pose)
{
    int pitch,yaw;float pf,yf;gang_fire_overlay(m,&pitch,&pf,&yaw,&yf);
    for(unsigned b=0;b<ch->nbones;b++) {
        chars_sample_pose(c,ch,ch->motion0+gang_police_motion(m),m->frame,b,pose+b);
        CharPose other;
        if(m->police.blend>0 && m->police.old_motion>=0 && (unsigned)m->police.old_motion<ch->nmotions) {
            chars_sample_pose(c,ch,ch->motion0+m->police.old_motion,m->police.old_frame,b,&other);chars_blend_pose(pose+b,&other,m->police.blend);
        } else if(m->anim==1 && m->police.gait>0) {
            chars_sample_pose(c,ch,ch->motion0+gang_police_gait_motion(m),m->frame,b,&other);chars_blend_pose(pose+b,&other,m->police.gait);
        }
        if(pitch>=0)chars_overlay_pose(c,ch,ch->motion0+pitch,pf,b,1,pose+b);
        if(yaw>=0)chars_overlay_pose(c,ch,ch->motion0+yaw,yf,b,1,pose+b);
    }
}
static unsigned fire_stream(void *context,const void *group,void *member,int command,int duration)
{
    EnemyCombatWorld *w=context;const Gang *g=group;GangMember *m=member;
    EnemyFire *s=NULL;
    if(!w->fires)return 6;
    for(int i=0;i<ENEMY_FIRE_CAPACITY;i++)if(m->gun.probe && w->fires[i].token==m->gun.probe){s=w->fires+i;break;}
    if(command==-1){if(s)s->token=0;m->gun.probe=0;return 6;}
    if(command==1) {
        if(s)s->token=0;
        for(int i=0;i<ENEMY_FIRE_CAPACITY;i++)if(!w->fires[i].token){s=w->fires+i;break;}
        if(!s){w->fire_dropped++;m->gun.probe=0;return 6;}
        memset(s,0,sizeof *s);if(!++w->fire_serial)++w->fire_serial;
        s->token=m->gun.probe=w->fire_serial;s->seen=w->fire_tick;s->variant=g->kind==25;w->fire_created++;return 0;
    }
    if(!s){m->gun.probe=0;return 6;}
    s->seen=w->fire_tick;
    if(command==2){s->flags|=4;return s->flags;}
    if(s->flags&2)return s->flags;
    Chars *c=w->actors;int model=g->kind==25?33:50;
    if(!c || !c->h || c->h->n_chars<=(unsigned)model || c->chars[model].nbones!=12){s->flags|=6;return s->flags;}
    const JgcChar *ch=c->chars+model;CharPose pose[12];float root[16],matrices[12][16],origin[3],end[3],direction[3];
    enemycombat_fire_pose(c,ch,m,pose);gang_member_matrix(g,m,root);chars_attachment_matrices(c,ch,pose,root,matrices);
    for(int i=0;i<2;i++) {
        const float *a=matrices[FIRE_ANCHORS[i].bone],*p=FIRE_ANCHORS[i].pos;float *out=i?end:origin;
        for(int j=0;j<3;j++)out[j]=a[j*4]*p[0]+a[j*4+1]*p[1]+a[j*4+2]*p[2]+a[j*4+3];
    }
    for(int j=0;j<3;j++)direction[j]=end[j]-origin[j];
    if(fabsf(direction[0])>=.1f || fabsf(direction[2])>=.1f) {
        int yaw=player_bam_atan2(direction[0],direction[2]);
        float z=fmaf(direction[0],player_fsca_sin(yaw),direction[2]*player_fsca_sin(yaw+16384));
        int target[2]={player_bam_atan2(direction[1],fabsf(z)),yaw};
        for(int j=0;j<2;j++){int delta=(short)(target[j]-s->angles[j]);if(delta>0x2aaa)delta=0x2aaa;if(delta< -0x2aaa)delta=-0x2aaa;s->angles[j]+=delta;}
    }
    float cy=player_fsca_sin(s->angles[0]+16384);
    direction[0]=player_fsca_sin(s->angles[1])*cy;direction[1]=player_fsca_sin(s->angles[0]);direction[2]=player_fsca_sin(s->angles[1]+16384)*cy;
    /* 07f160 consumes the previous registration before stream propagation. */
    if(s->state==1) {
        s->impact_clock++;
        s->flags&=~0x10;
        if(s->pending_body>0){s->last=s->pending_body-1;s->flags|=0x10;s->pending_body=0;}
        if(s->pending_wall) {
            s->flags&=~8;
            if(s->pending_wall==2){
                s->flags|=8;float wall2=0;for(int j=0;j<3;j++){float d=s->wall_point[j]-origin[j];wall2+=d*d;}
                while(s->last>s->first){float d2=0;for(int j=0;j<3;j++){float d=s->point[s->last][j]-origin[j];d2+=d*d;}if(d2<=wall2)break;s->last--;}
                if(s->impact_clock>30){burning_impact(w,s->wall_point);s->impact_clock=0;}
            }
            s->pending_wall=0;
        }
    }
    enemy_fire_step(s,origin,direction,duration,(s->flags&4)!=0,random15(w),random15(w));
    if(s->flags&2)return s->flags;
    /* Only one scene ray every three ticks. Body broadphase is bounded by
       the twenty authored spheres, independent of the level's triangles. */
    if(s->state<=1) {
        if(++s->probe_clock>=3) {
            s->probe_clock=0;float normal[3];
            s->pending_wall=probe(w,origin,s->point[s->last],99,s->wall_point,normal)?2:1;
        }
        for(int i=s->first;i<=s->last;i++)if(actor_contact(w,s->point[i],s->point[i],3.f,29)){s->pending_body=i+1;break;}
    }
    return s->flags;
}
static void strong_attack(void *context,const void *group,const void *member)
{
    EnemyCombatWorld *w=context;const Gang *g=group;const GangMember *m=member;
    if(fabsf(m->pos[0]-w->gang.target[0])>80 || fabsf(m->pos[1]-w->gang.target[1])>100 || fabsf(m->pos[2]-w->gang.target[2])>80)return;
    int body=gang_kairiki_body(m),model=body<0?31:body;
    Chars *c=body<0?w->actors:&w->burning_models;
    if(!c || !c->h || c->h->n_chars<=(unsigned)model || c->chars[model].nbones!=16)return;
    const JgcChar *ch=c->chars+model;CharPose pose[16];float root[16],matrices[16][16],point[3];
    int motion=body<0?gang_police_motion(m):0;
    for(int b=0;b<16;b++)chars_sample_pose(c,ch,ch->motion0+motion,m->frame,b,pose+b);
    gang_member_matrix(g,m,root);chars_attachment_matrices(c,ch,pose,root,matrices);
    unsigned anchor=(unsigned)m->police.head;if(anchor>=3)return;
    const float *a=matrices[KAIRIKI_ANCHORS[anchor].bone],*p=KAIRIKI_ANCHORS[anchor].pos;
    for(int j=0;j<3;j++)point[j]=a[j*4]*p[0]+a[j*4+1]*p[1]+a[j*4+2]*p[2]+a[j*4+3];
    actor_contact(w,point,point,KAIRIKI_RADIUS,27);
}
