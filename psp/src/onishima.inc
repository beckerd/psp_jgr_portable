/* Private type-17 movement (08771c/087e10) and animation (088334).
   This controller runs at the existing 30 Hz world tick, never draw rate. */
static void onishima_accelerate(Gang *g,float limit)
{
    if(g->speed<limit)g->speed+=g->par[P_ACC];
    if(g->speed>limit)g->speed=limit;
}
static float onishima_move(Gang *g)
{
    GangMember *m=g->m;float point[3];route_point(g,point);
    float delta[3]={point[0]-g->pos[0],0,point[2]-g->pos[2]};
    float distance=sqrtf(delta[0]*delta[0]+delta[2]*delta[2]);
    float baseline=(g->flags&0x100000) && g->combat?fabsf(g->combat->player_speed):0;
    if(distance<g->par[(g->flags&0x100000)?11:10]) {
        g->speed=fmaxf(0,g->speed-g->par[P_DEC]);
        if(g->speed>baseline)g->speed=fmaxf(baseline,g->speed-g->par[P_DEC]);
    }
    if(distance<g->par[12])return 0;
    if(m->chief.turn_wait>0)m->chief.turn_wait--;
    else {
        int yaw=(int)lrintf(atan2f(g->dir[0],g->dir[2])*10430.378350470453f);
        int desired[3]={0,yaw,0};heli_vector_angles(delta,desired);
        yaw=heli_turn(yaw,desired[1],(int)g->par[P_MAXTURN]);
        g->dir[0]=heli_sin(yaw);g->dir[2]=heli_cos(yaw);
    }
    float next[3]={g->pos[0]+g->dir[0]*g->speed,g->pos[1],g->pos[2]+g->dir[2]*g->speed};
    int here=g->graph?graph_node_at(g->graph,next):-1;
    if(g->graph && here<0 && g->state!=6)return -1;
    if(g->combat && g->combat->police_obstacle) {
        float normal[3];
        if(g->combat->police_obstacle(g->combat->context,g->pos,next,normal)) {
            /* 06df9c contact reflection, with the source 15-tick turn
               hold. Do not rotate a fixed half-radian into the wall. */
            float dot=g->dir[0]*normal[0]+g->dir[2]*normal[2];
            float reflected[3]={g->dir[0]-2*dot*normal[0],0,g->dir[2]-2*dot*normal[2]};
            steer(g->dir,reflected,32768);m->chief.turn_wait=15;return distance;
        }
    }
    if(here>=0)memcpy(g->lastnode,g->pos,12);
    memcpy(g->pos,next,12);g->node=here;
    if(g->npath>0 && g->pathi<g->npath && (here==g->path[g->pathi] || distance-g->speed<g->par[9]))g->pathi++;
    return distance-g->speed;
}
static int onishima_primary(const Gang *g)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_onishima_primary.inc"
#else

#include "generated/source_data/onishima_primary.inc"

#endif

    int s=g->unit_state;if(s<0 || s>=18)return 15;
    if(s>=5 && s<=7) {
        float turn=fabsf(remainderf(g->m[0].yaw-g->m[0].chief.old_yaw,6.28318530718f));
        return g->speed>0 || (s==6 && turn>=182*(6.28318530718f/65536.f))?22:15;
    }
    return raw[s]<0?g->m[0].anim:raw[s];
}
static void onishima_animation(Gang *g)
{
    GangMember *m=g->m;int state=g->unit_state;
    if(m->chief.old_state!=state) {
        m->chief.old_motion=m->anim;m->chief.old_frame=m->frame;
        m->chief.blend=(state==0 || state==1 || state==3)?.8f:0;
        m->chief.old_state=state;m->frame=m->secondary_frame=0;
    } else if(m->chief.blend>0)m->chief.blend=fmaxf(0,m->chief.blend-.2f);
    m->anim=onishima_primary(g);m->overlay=-1;
    if(state>=5 && state<=7) {
        
#if JGR_PORTABLE_ASSETS
#include "source_data_onishima_overlay.inc"
#else

#include "generated/source_data/onishima_overlay.inc"

#endif

        m->overlay=overlays[!!m->chief.variant][state-5];
        /* 088c8c/088dec clamp using semantic 11/12, respectively. */
        unsigned duration=ONISHIMA_FRAMES[state==6?10:state==7?11:m->overlay];
        m->secondary_frame=fminf(m->secondary_frame+1,duration-1);
        if(state==7 && m->secondary_frame>=duration-1)g->unit_state=1;
    }
    if(m->anim==22 && g->speed>0 && m->chief.blend<=0) {
        float target=fminf(g->speed/PAR_ONISHIMA[P_MAXSPD],1);
        m->chief.gait+=fmaxf(-.1f,fminf(.1f,target-m->chief.gait));
        m->frame=m->chief.cycle_frame;
        m->chief.cycle_frame+=1;if(m->chief.cycle_frame>=ONISHIMA_FRAMES[22]-1.f)m->chief.cycle_frame-=ONISHIMA_FRAMES[22]-1.f;
    } else if(m->chief.blend<=0) {
        m->frame+=1;if(m->frame>=ONISHIMA_FRAMES[m->anim]-1.f)m->frame-=ONISHIMA_FRAMES[m->anim]-1.f;
    } else m->chief.gait=0;
    /* 088564/08879c select locomotion from actual displacement/turning. */
    if((state==0 || state==1) && m->chief.blend<=0) {
        int moving=g->speed>0 || m->yaw!=m->chief.old_yaw;
        g->unit_state=moving?1:0;
    }
}
