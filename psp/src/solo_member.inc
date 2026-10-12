/* Tagging rivals (18/19/20), original 0df884 and states 1cf880.
   +74 pending feet and +150 terrain results are deliberately one tick ahead
   of the displayed group position. Do not use a forward avoidance probe. */
#include "npc_visibility.h"
static int solo_bam(float r){return (int)lrintf(r*(65536.f/6.283185307179586f));}
static float solo_rad(int a){return (int16_t)a*(6.283185307179586f/65536.f);}
static void solo_wall_turn(int a[3],const float n[3],int choice) /* 06df9c */
{
    int turn=choice ? 16384:-16384;
    float x=heli_sin(a[1])*heli_cos(a[0]),z=heli_cos(a[1])*heli_cos(a[0]);
    if((n[0]==0 && n[2]==0) || (x==0 && z==0)){a[1]+=turn;return;}
    int normal=heli_atan(n[0],n[2]),heading=heli_atan(x,z);
    a[1]=normal+((int16_t)(heading-normal)>=0 ? turn:-turn);
}
static void solo_move(Gang *g) /* 0e041c */
{
    GangMember *m=g->m;
    if(g->speed<4.1666665077f){g->speed+=.1851851791f;if(g->speed>4.1666665077f)g->speed=4.1666665077f;}
    if(g->flags&0x200){
        unsigned r=g->combat && g->combat->random15 ? g->combat->random15(g->combat->context):(unsigned)rand()&32767;
        m->solo.choice=(int)((float)r/32768.f+.5f);
        solo_wall_turn(m->solo.angles,m->solo.normal,m->solo.choice);
    }
    m->solo.pending[0]+=heli_sin(m->solo.angles[1])*g->speed;
    m->solo.pending[1]+=g->vy;
    m->solo.pending[2]+=heli_cos(m->solo.angles[1])*g->speed;
    float d[3];for(int k=0;k<3;k++)d[k]=g->pos[k]-g->lastnode[k];
    if(!(g->flags&0x20000) && sqrtf((d[0]*d[0]+d[1]*d[1])+d[2]*d[2])>15.f)g->flags|=0x20000;
}
static void solo_turn_depart(Gang *g) /* 0dfec8 */
{
    GangMember *m=g->m;
    m->solo.angles[1]=heli_turn(m->solo.angles[1],m->solo.target,0x1555);
    if(m->solo.angles[1]==m->solo.target)g->state=4;
    solo_move(g);
}
static void solo_consume_terrain(Gang *g) /* 0e01d0 */
{
    GangMember *m=g->m;RivalTerrain *h=&m->solo.terrain;float *p=m->solo.pending;
    if(g->flags&0x100)return;
    g->flags&=~0x200;
    if(g->speed==0 && g->vy==0){memcpy(g->pos,p,12);return;}
    if(h->hit){
        p[0]=h->point[0];p[2]=h->point[2];
        if(h->wall){memcpy(m->solo.normal,h->normal,12);for(int k=0;k<3;k++)p[k]+=h->normal[k];
            g->flags|=0x200;g->unit_state=2;}
    }
    if(h->floor){
        p[1]=h->height;float d[3];for(int k=0;k<3;k++)d[k]=p[k]-g->pos[k];
        if(fabsf(d[0])>=.1f || fabsf(d[2])>=.1f){
            int a[3];memcpy(a,m->solo.angles,sizeof a);heli_vector_angles(d,a);
            m->solo.angles[0]=heli_turn(m->solo.angles[0],a[0],0x1555);
            m->solo.angles[1]=heli_turn(m->solo.angles[1],a[1],0x1555);m->solo.angles[2]=0;
        }
        g->vy=d[1];g->flags&=~0x80;
    }else{
        if(g->vy>0)g->vy=0;else {g->vy-=.2f;if(g->vy < -2.7777779102f)g->vy=-2.7777779102f;}
        m->solo.angles[0]=0;g->flags|=0x80;
    }
    memcpy(g->pos,p,12);memset(h,0,sizeof *h);
    if(!(g->flags&0x200))m->solo.choice=0;
}
static void solo_animation(Gang *g) /* 0dec74 / 0dee0c / 0deeb8 */
{
    GangMember *m=g->m;int entered=m->solo.old_anim!=g->unit_state;
    m->solo.old_anim=g->unit_state;
    if(g->unit_state==2){
        if(entered)m->frame=0;
        else if(++m->frame>29.f){m->frame=29.f;g->unit_state=1;}
    }else if(g->unit_state==1){
        float step=g->speed<1.388888955f ? fminf(1.7f,2.f-g->speed/1.388888955f):1.f;
        m->solo.walk_frame+=step;
        float length=g->kind==20 ? 29.f:28.f;
        if(m->solo.walk_frame>=length)m->solo.walk_frame-=length;
        m->frame=m->solo.walk_frame;
    }else {m->frame+=1.f;if(m->frame>=59.f)m->frame-=59.f;}
}
void gang_spawn_solo(Gang *g,int kind,const float *pos,float yaw,int entries,int trig,const float *quad)
{
    memset(g,0,sizeof *g);g->active=1;g->kind=kind;g->mode=1;g->trig=trig;if(quad)memcpy(g->quad,quad,32);
    memcpy(g->pos,pos,12);g->dir[0]=sinf(yaw);g->dir[2]=cosf(yaw);g->burst_left=entries>1 ? entries-1:0;g->nmem=1;
    GangMember *m=g->m;memcpy(m->pos,pos,12);m->yaw=yaw;m->motion=-1;
    memcpy(m->solo.pending,pos,12);m->solo.angles[1]=solo_bam(yaw);m->solo.old_anim=-1;
    memcpy(g->lastnode,pos,12);g->state=2;g->unit_state=0;
}
static void solo_tick(Gang *g,const float *ppos,int police_attack,float (*ground)(void *,float,float,float),void *gctx)
{
    (void)police_attack;(void)ground;(void)gctx;GangMember *m=g->m;
    if(g->flags&0x20){g->active=0;return;}
    if(g->state==4 || g->state==5)solo_consume_terrain(g);
    /* 0dffb4: body contact is consumed once, before the AI dispatch. */
    if(g->solo_contact && g->state!=5 && !(g->flags&(0x100|0x20000))){
        g->state=5;g->unit_state=2;
        if(!(g->flags&0x40000)){m->solo.target=m->solo.angles[1]+16384;g->flags|=0x40000;}
        memcpy(g->target,g->pos,12);g->target[1]+=10.f;g->flags|=0x20000;g->solo_drops=1;
    }
    g->solo_contact=0;
    switch(g->state){
    case 2:
        if((g->flags&0x10) || gang_triggered(g,ppos)){
            g->state=3;g->unit_state=1;m->solo.target=m->solo.angles[1]+16384;g->flags|=0x40000;
            m->solo.angles[0]=m->solo.angles[2]=0;m->pitch=m->roll=0;
        }break;
    case 3:solo_turn_depart(g);break;
    case 4:
        if(g->flags&0x10000){g->flags|=0x20;g->active=0;return;}
        solo_move(g);break;
    case 5:
        m->solo.angles[1]=heli_turn(m->solo.angles[1],m->solo.target,0x1555);
        if(g->unit_state!=2)g->state=4;
        break;
    }
    solo_animation(g);
    if(g->state>2){m->yaw=solo_rad(m->solo.angles[1]);m->pitch=solo_rad(m->solo.angles[0]);m->roll=solo_rad(m->solo.angles[2]);}
    g->dir[0]=heli_sin(m->solo.angles[1]);g->dir[2]=heli_cos(m->solo.angles[1]);memcpy(m->pos,g->pos,12);
    /* 0e00a8 queues the actual previous-feet +3 -> pending-feet -1 ray. */
    memset(&m->solo.terrain,0,sizeof m->solo.terrain);
    if(!(g->flags&0x10100) && g->state==4 && g->combat && g->combat->rival_terrain)
        g->combat->rival_terrain(g->combat->context,g->pos,m->solo.pending,&m->solo.terrain);
    if(!(g->flags&(0x10100|0x20000)) && g->state!=5 && g->combat && g->combat->rival_contact)
        g->solo_contact=g->combat->rival_contact(g->combat->context,g);
}
void gang_solo_view(Gang *gangs,int count,const float view[16],int fov,float aspect)
{
    /* 0dfa7c -> 06dd7c / 0c9d50. Source sphere (0,10.342,0), r=10,
       far clip 400. Adapt the original cone to PSP's wider viewport as NPCs do.
       Only escaped state 4 uses this draw flag to retire, on its next tick. */
    NpcVisibility v=npc_visibility_params(fov,heli_sin);npc_visibility_aspect(&v,aspect);
    for(int i=0;i<count;i++){
        Gang *g=gangs+i;if(!g->active || (g->mode!=1 && g->mode!=6 && g->mode!=5 && g->mode!=7))continue;
        int rocket=g->mode==7;
        for(int member=0;member<(rocket?g->nmem:1);member++) {
            GangMember *m=g->m+member;float root[16];gang_member_matrix(g,m,root);float p[3];
            for(int k=0;k<3;k++)p[k]=root[k*4+1]*(rocket?8.682700157f:10.3420000076f)+root[k*4+2]*(rocket?-.4147999883f:0.f)+root[k*4+3];
            float x=(view[0]*p[0]+view[1]*p[1]+view[2]*p[2]+view[3])*v.x_scale;
            float y=view[4]*p[0]+view[5]*p[1]+view[6]*p[2]+view[7];
            float z=-(view[8]*p[0]+view[9]*p[1]+view[10]*p[2]+view[11]);
            /* 09c2d0: Sniper 400/500; 099ed4: Rocket 150/300/700.
               Keep the draw flag live even when hidden so retirement works. */
            float radius=rocket?9.f:10.f,far=rocket?700.f:g->mode==1?400.f:500.f;
            int visible=z>=-radius && z<=far && (sqrtf(x*x+y*y)-z*v.cone_tan)*v.cone_inverse_cos<=radius;
            if(rocket){if(visible)m->flags&=~0x200000;else m->flags|=0x200000;}
            else {if(visible)g->flags&=~0x10000;else g->flags|=0x10000;}
        }
    }
}
