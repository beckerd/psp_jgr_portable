int (*gang_landing_tag)(void *,const float *,float *);
/* Rocket leader 0970b4, dispatch 1c74b0, parameters 1c7714/1c777c.
   The route modes reuse the helicopter's BAM angles and deferred ray storage.
   node=previous waypoint, wpi=next, pathi=last state, cnt=substate,
   timer=state clock, npath=previous traversal direction, saved=return state. */
static void rocket_member_flags(Gang *g,unsigned set,unsigned clear)
{for(int i=0;i<g->nmem;i++)g->m[i].flags=(g->m[i].flags&~clear)|set;}
static int rocket_near(Gang *g,const float *p) /* 0989a0 */
{
    float x=p[0]-g->pos[0],y=p[1]-g->pos[1],z=p[2]-g->pos[2];
    int near=x*x+y*y+z*z<=250.f*250.f;
    if(near)g->flags|=0x2000000;else g->flags&=~0x2000000;
    return near;
}
static int rocket_index(const Gang *g,int i)
{return i<0?g->nwp-1:i>=g->nwp?0:i;}
static int rocket_step_index(const Gang *g,int i,int direction)
{
    for(int n=0;n<g->nwp;n++) {i=rocket_index(g,i+direction);if(!(g->wp_action[i]&0x18))break;}
    return i;
}
static float rocket_distance2(const float *a,const float *b)
{float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];return x*x+y*y+z*z;}
static void rocket_choose_direction(Gang *g,const float *p) /* 0983c0 */
{
    g->npath=g->burst_left;
    int nearest=-1;float best=0;
    for(int i=0;i<g->nwp;i++)if(!(g->wp_action[i]&0x18)) {
        float d=rocket_distance2(p,g->wp[i]);if(nearest<0 || d<best){best=d;nearest=i;}
    }
    if(nearest<0)return;
    if(nearest==g->node) {
        /* 0984b8 repeats the forward-neighbour distance (r12), even after
           finding a backward neighbour. Equal results keep the direction. */
        return;
    } else {
        float f=0,b=0;int fi=g->node,bi=fi;
        /* The +10 attributes are edge lengths, not cumulative distances. */
        for(int n=0;n<g->nwp;n++) {
            if(fi==nearest && f<=b){g->burst_left=1;return;}
            if(fi!=nearest){f+=g->wp_aux[fi];fi=rocket_step_index(g,fi,1);}
            if(bi==nearest && b<=f){g->burst_left=-1;return;}
            if(bi!=nearest){bi=rocket_step_index(g,bi,-1);b+=g->wp_aux[bi];}
        }
        g->burst_left=f<=b?1:-1;
    }
}
static void rocket_brake(Gang *g) /* 0982f0 */
{
    int n=(int)ceilf((g->speed-g->wp_speed[g->wpi])/ROCKET_DEC)+1;
    float ticks=(float)(uint32_t)n;g->route_brake=(g->speed-(.5f*ROCKET_DEC)*ticks)*ticks;g->heli_brake_steps=n;
}
static void rocket_advance(Gang *g,const float *p) /* 09859c */
{
    int i=g->wpi;g->node=i;
    if(!(g->flags&0x800000) && g->state!=6) {
        rocket_choose_direction(g,p);
        for(int n=0;n<g->nwp;n++) {
            int next=rocket_index(g,i+1);
            if((g->wp_action[next]&8) && !(g->flags&0x100000) && !rocket_near(g,p)) {
                g->flags|=0x100000;i=next;break;
            }
            g->flags&=~0x100000;i=rocket_index(g,i+g->burst_left);
            if(!(g->wp_action[i]&0x18))break;
        }
    } else {i=rocket_step_index(g,i,g->burst_left);g->flags&=~0x100000;}
    unsigned action=g->wp_action[g->node];
    for(int k=0;k<2;k++)if(action&(2u<<k)) {
        unsigned bit=k?0x400000:0x800000;
        if(((action&1)==0)==(g->burst_left>0)) {
            g->flags|=bit;
            if(!k && (g->flags&0x100000)){g->flags&=~0x100000;i=rocket_index(g,i+g->burst_left);}
        } else g->flags&=~bit;
    }
    g->wpi=i;g->action=action;g->next_action=g->wp_action[i];memcpy(g->target,g->wp[i],12);rocket_brake(g);
}
static void rocket_translate(Gang *g) /* 098acc */
{heli_forward(g);for(int k=0;k<3;k++)g->pos[k]+=g->dir[k]*g->speed;}
static int rocket_heading(Gang *g,const float *v) /* 06da4c */
{
    int a[3]={g->heli_angles[0],g->heli_angles[1],g->heli_angles[2]};heli_vector_angles(v,a);
    for(int k=0;k<2;k++)g->heli_angles[k]=heli_turn(g->heli_angles[k],a[k],ROCKET_TURN);
    g->heli_angles[2]=0;return g->heli_angles[1]==a[1];
}
static int rocket_move(Gang *g) /* 09820c: no arrival snap */
{
    float v[3];for(int k=0;k<3;k++)v[k]=g->target[k]-g->pos[k];
    float d=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]),speed=g->wp_speed[g->wpi];
    if(d<=g->route_brake && g->speed>speed){g->heli_brake_steps--;g->speed=fmaxf(speed,g->speed-ROCKET_DEC);}
    if(g->speed<speed)g->speed=fminf(speed,g->speed+ROCKET_ACC);
    int arrived=d<ROCKET_ARRIVE;if(!arrived)rocket_heading(g,v);rocket_translate(g);return arrived;
}
static int rocket_request(Gang *g,int flag) /* 098930 */
{
    if(g->flags&flag)return 0;
    g->saved=g->state;g->state=13;g->flags=(g->flags&~0x30000)|flag;return 1;
}
static void rocket_probe(Gang *g) /* 098c18: query now, consume next tick */
{
    g->heli_contact=g->speed!=0 && gang_heli_probe &&
        gang_heli_probe(g_wall_ctx,g->pos,g->heli_candidate,99,g->heli_hit);
}
static void rocket_contact(Gang *g) /* 098bb4 */
{
    if(g->speed==0)return;
    if(g->heli_contact){memcpy(g->heli_candidate,g->heli_hit,12);g->flags&=~0x80;}
    else g->flags|=0x80;
}
static int rocket_height(Gang *g,float target) /* 098b38 */
{
    g->speed=.9259259700775146f;float d=g->pos[1]-target;
    if(d>1.5f){g->pos[1]-=fminf(d,g->speed);return 0;}
    if(d< -1.5f){g->pos[1]+=fminf(-d,g->speed);return 0;}return 1;
}
static void rocket_leader_tick(Gang *g,const float *p)
{
    if(g->flags&0x10) {
        int invisible=1;for(int i=0;i<g->nmem;i++)if(!(g->m[i].flags&0x200000))invisible=0;
        if(invisible){g->flags|=0x20;rocket_member_flags(g,0x20,0);return;}
        if(g->state!=12 && !(g->state==13 && g->saved==12))g->state=12;
    }
    if(g->pathi!=g->state){g->pathi=g->state;g->cnt=g->timer=0;}
    switch(g->state) {
    case 1:
        if(gang_triggered(g,p)){g->flags&=~0x101;g->state=2;rocket_member_flags(g,0,1);}break;
    case 2:
        if(!g->cnt) {
            if(!g->heli_angles[0]){g->pos[1]-=g->speed;if(g->pos[1]<g->target[1]+20.f)g->cnt=1;}
            else {g->pos[1]+=g->speed;if(g->pos[1]>g->target[1]-20.f)g->cnt=1;}
        } else if(g->cnt==1 && rocket_move(g)){rocket_advance(g,p);g->state=3;}break;
    case 3:
        if(g->flags&0x800000)g->state=9;
        else if(!rocket_near(g,p)) {
            rocket_member_flags(g,0,2);
            g->state=(g->flags&0x100000)?10:g->burst_left==g->npath?4:7;
        } else {
            heli_forward(g);
            if(p[0]!=0 || p[1]!=0 || p[2]!=0)g->target_player=
                (p[0]-g->pos[0])*g->dir[0]+(p[1]-g->pos[1])*g->dir[1]+(p[2]-g->pos[2])*g->dir[2]>=0;
            const float *dir=g->combat?g->combat->player_direction:g->dir;
            int same=(p[0]==0 && p[1]==0 && p[2]==0) || dir[0]*g->dir[0]+dir[1]*g->dir[1]+dir[2]*g->dir[2]>=0;
            g->state=same?(g->target_player?8:6):(g->target_player?5:7);
        }
        g->cnt=g->timer=0;break;
    case 4:case 5:case 6:case 9:case 10:
        if(!g->cnt){if(rocket_request(g,0x10000))break;g->cnt=1;}
        if(g->cnt>(g->state==5 || g->state==9?2:1))break;
        if(g->state==5 && g->cnt==1){if(g->flags&0x400000)g->flags|=2;g->cnt=2;break;}
        if(g->state==9 && g->cnt==1){rocket_member_flags(g,0x800000,0);g->cnt=2;}
        if(rocket_move(g)) {
            if(g->state==10)g->state=11;
            else {
                rocket_advance(g,p);
                if(g->state==5){g->state=3;g->flags&=~2;}
                else if(g->state==4 || (g->state==9 && !(g->flags&0x800000)))g->state=3;
            }
        }
        if(g->pathi==6 && ++g->timer>90)g->state=3;
        break;
    case 7:
        if(!g->cnt){if(rocket_request(g,0x10000))break;g->heli_angles[0]=g->heli_angles[2]=0;g->cnt=1;}
        if(g->cnt==1) {
            const float speed=1.8518519401550293f;
            if(g->speed<speed)g->speed=fminf(speed,g->speed+ROCKET_ACC);
            else if(g->speed>speed)g->speed=fmaxf(speed,g->speed-ROCKET_DEC);
            if(g->speed==speed)g->cnt=2;
            rocket_translate(g);
        } else if(g->cnt==2 || g->cnt==3) {
            if(g->cnt==2){rocket_member_flags(g,0x40000,0);g->cnt=3;}
            g->heli_angles[0]+=0x333;rocket_translate(g);
            if(g->heli_angles[0]>=0x8000){g->heli_angles[0]=0x8000;g->cnt=4;}
        } else if(g->cnt==4){g->heli_angles[1]+=0x8000;g->heli_angles[0]=g->heli_angles[2]=0;g->state=3;g->npath=g->burst_left;}break;
    case 8:
        if(!g->cnt){if(rocket_request(g,0x20000))break;g->cnt=1;}
        if(g->cnt==1){if(g->flags&0x400000)g->flags|=2;g->cnt=2;memcpy(g->heli_candidate,g->pos,12);g->heli_candidate[1]=p[1];g->speed=.9259259700775146f;rocket_probe(g);}
        else if(g->cnt==2){rocket_contact(g);rocket_height(g,g->heli_candidate[1]+15.f);g->heli_candidate[1]=p[1];rocket_probe(g);if(!rocket_near(g,p)){g->flags&=~2;g->cnt=3;}}
        else if(g->cnt==3 && rocket_height(g,g->wp[g->node][1]))g->state=3;
        break;
    case 11:
        if(g->graffiti && g->graffiti->complete && !(g->flags&0x2000))g->flags|=0x6000;
        if(!g->cnt){if(rocket_request(g,0x20000))break;g->cnt=1;}
        if(g->cnt==1){rocket_member_flags(g,0x80000,0);g->speed=.9259259700775146f;memcpy(g->heli_candidate,g->pos,12);g->heli_candidate[1]-=g->speed;rocket_probe(g);g->tagprog=gang_landing_tag?gang_landing_tag(g_wall_ctx,g->pos,g->lastnode):0;g->cnt=2;}
        else if(g->cnt==2) {
            int tag=g->tagprog;float center[3];memcpy(center,g->lastnode,12);
            rocket_contact(g);memcpy(g->pos,g->heli_candidate,12);g->heli_candidate[1]-=.9259259700775146f;
            g->tagprog=gang_landing_tag?gang_landing_tag(g_wall_ctx,g->pos,g->lastnode):0;
            if(!(g->flags&0x80)) {
                int old=g->node;g->node=g->wpi;g->wpi=old;memcpy(g->target,g->wp[old],12);
                g->heli_yaw_target=g->heli_angles[1];
                if(tag){float v[3];int a[3]={0,g->heli_yaw_target,0};for(int k=0;k<3;k++)v[k]=center[k]-g->pos[k];heli_vector_angles(v,a);g->heli_yaw_target=a[1];}
                g->timer=tag==2?210:60;g->cnt=3;
            } else rocket_probe(g);
        } else if(g->cnt==3) {
            g->heli_angles[1]=heli_turn(g->heli_angles[1],g->heli_yaw_target,ROCKET_TURN);
            if(g->flags&0x4000){g->timer=9000;g->flags&=~0x4000;rocket_member_flags(g,0x2000,0);}
            if(--g->timer<1){rocket_member_flags(g,0,0x2000);g->cnt=4;}
        } else if(g->cnt==4) {
            float v[3];int a[3]={0,g->heli_angles[1],0};for(int k=0;k<3;k++)v[k]=g->target[k]-g->pos[k];heli_vector_angles(v,a);
            g->heli_angles[1]=heli_turn(g->heli_angles[1],a[1],ROCKET_TURN);
            if(g->heli_angles[1]==a[1]){rocket_member_flags(g,0x10000,0);g->state=4;g->speed=2.777777910232544f;g->flags=(g->flags&~0x20000)|0x10000;}
        }break;
    case 12:
        if(!g->cnt){if(rocket_request(g,0x20000))break;g->cnt=1;}
        if(g->cnt==1){if(++g->timer>=60){rocket_member_flags(g,0x10,0);g->cnt=2;}}
        else if(g->cnt==2)g->pos[1]+=6.481481552124023f;
        break;
    case 13:
        if(!g->cnt) {
            if(g->speed>=.9259259700775146f){g->speed-=ROCKET_DEC;if(g->speed>=0)rocket_translate(g);else g->speed=0;}
            else {rocket_member_flags(g,(g->flags&0x10000)?0x10000:0x20000,0);g->cnt=1;}
        } else if(g->cnt==1) {
            if(g->timer++>30){g->state=g->saved;g->timer=g->cnt=0;if(g->flags&0x10000)g->speed=2.777777910232544f;}
            else if(g->speed>0)rocket_translate(g);
        }break;
    }
    heli_forward(g);
}
void gang_spawn_rocket(Gang *g,int kind,const float (*wp)[3],const float *speed,const float *attr,const int *flags,int nwp,int trig,const float *quad)
{
    memset(g,0,sizeof *g);if(nwp<2)return;
    g->active=1;g->kind=g->type=kind;g->mode=7;g->trig=trig;if(quad)memcpy(g->quad,quad,32);
    g->nwp=nwp>GANG_MAX_WAYPOINTS?GANG_MAX_WAYPOINTS:nwp;
    for(int i=0;i<g->nwp;i++){memcpy(g->wp[i],wp[i],12);g->wp_speed[i]=speed[i];g->wp_aux[i]=attr[i];g->wp_action[i]=flags[i];}
    memcpy(g->pos,wp[0],12);g->speed=speed[0];g->burst_left=g->npath=1;
    g->nmem=((unsigned)flags[0]>>16)&65535;if(g->nmem<1)g->nmem=1;if(g->nmem>5)g->nmem=5;
    for(int i=0;i<g->nmem;i++){GangMember *m=g->m+i;m->motion=-1;m->flags=0x101;m->anim=1;m->secondary_anim=13;}
    const float zero[3]={0};rocket_advance(g,zero);
    /* 097230 intentionally resets these flags after initial route selection. */
    g->flags=0x101;float v[3];for(int k=0;k<3;k++)v[k]=wp[1][k]-g->pos[k];heli_vector_angles(v,g->heli_angles);
    g->heli_angles[0]=g->heli_angles[0]<0?0x8000:0;heli_forward(g);
    g->state=1;g->pathi=-1;g->log_state=-1;g->unit_state=1;rocket_place_members(g);
}
static void rocket_tick(Gang *g,const float *p)
{
    rocket_leader_tick(g,p);rocket_attack_orders(g,p);rocket_place_members(g);
    if(g->state!=g->log_state){printf("rocket%d: state %d->%d wp %d/%d flags %x x%d\n",g->kind,g->log_state,g->state,g->wpi,g->nwp,g->flags,g->nmem);g->log_state=g->state;}
}
