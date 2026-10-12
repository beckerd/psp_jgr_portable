/* Local to gang.c. Source 0a7dd0/7e68/7fd4/80f4/8778/88b8/8e5c/8f6c.
   Keep BAM task words; float yaw is only the renderer boundary. */
int (*gang_heli_probe)(void *,const float *,const float *,unsigned,float *);
#ifdef __PSP__
/* The production player already owns this ROM. Standalone host fixtures
   can embed it without linking the complete player; both paths are tested. */
extern float player_fsca_sin(int angle);
static float heli_sin(int a) { return player_fsca_sin(a); }
#else
#include "generated/source_data/player_fsca.inc"
static float heli_sin(int a)
{ unsigned u=(unsigned)a&65535;float v=SH4_SIN[u&32767];return u>=32768 ? -v:v; }
#endif
static float heli_cos(int a) { return heli_sin(a+16384); }
static int heli_turn(int a,int b,int cap) /* 1043f8 */
{ int d=(int16_t)(b-a);return d>cap ? a+cap:d < -cap ? a-cap:b; }
static int heli_atan(float y,float x) /* 103df0; same recovered FSCA search as player */
{
    if(x==0.f)return y<0.f ? -16384:16384;
    if(y==0.f)return x<0.f ? 32768:0;
    float s=fabsf(y)*(1.f/sqrtf(y*y+x*x));int lo=0,hi=16384,mid;
    do {mid=lo+(hi-lo)/2;float v=heli_sin(mid);if(v==s)break;
        if(v>s)hi=mid;else lo=mid;
    } while(hi-lo!=1);
    if(x<0.f)mid=32767-mid;
    return y<0.f ? -mid:mid;
}
static void heli_vector_angles(const float *v,int *a) /* 06da4c */
{
    /* The source leaves the caller's angles untouched on this early exit. */
    if(fabsf(v[0])<.1f && fabsf(v[2])<.1f)return;
    int yaw=heli_atan(v[0],v[2]);
    float z=fmaf(v[0],heli_sin(yaw),v[2]*heli_cos(yaw));
    a[0]=heli_atan(v[1],fabsf(z));a[1]=yaw;a[2]=0;
}
static void heli_forward(Gang *g) /* 104300; roll does not affect forward */
{
    float cp=heli_cos(g->heli_angles[0]);
    g->dir[0]=heli_sin(g->heli_angles[1])*cp;
    g->dir[1]=heli_sin(g->heli_angles[0]);
    g->dir[2]=heli_cos(g->heli_angles[1])*cp;
}
static void heli_bank_tick(Gang *g) /* 8db4; source only clamps the positive target */
{
    int d=(int16_t)(g->heli_angles[1]-g->heli_yaw_old);
    float weight=(4.62962961f-g->speed)/4.62962961f;
    float v=(8192.f/2730.f)*weight;
    int target=(int)(v*(float)d);if(target>=8192)target=8192;
    g->heli_bank=heli_turn(g->heli_bank,target,182);
    g->heli_angles[2]=g->heli_bank;g->heli_yaw_old=g->heli_angles[1];
}
static void heli_set_brake_distance(Gang *g) /* 80f4, unsigned step conversion */
{
    int n=(int)ceilf((g->speed-g->wp_speed[g->wpi])/HELI_DEC)+1; /* 013668 -> 01175c is ceil */
    float steps=(float)(uint32_t)n;
    g->route_brake=(g->speed-(.5f*HELI_DEC)*steps)*steps;
    g->heli_brake_steps=n;
}
static int heli_heading(Gang *g,const float *v,float distance) /* 7e68 */
{
    if(distance<HELI_ARRIVE)return 1;
    int a[3]={g->heli_angles[0],g->heli_angles[1],0};heli_vector_angles(v,a);
    g->heli_yaw_target=a[1];
    /* Yes, original compares current PITCH to desired YAW for the ramp. */
    int d=abs((int16_t)(a[1]-g->heli_angles[0]));
    if(g->heli_turn>d)g->heli_turn=d;
    else {g->heli_turn=heli_turn(g->heli_turn,d,64);if(g->heli_turn>HELI_TURN)g->heli_turn=HELI_TURN;}
    g->heli_angles[0]=heli_turn(g->heli_angles[0],a[0],g->heli_turn);
    g->heli_angles[1]=heli_turn(g->heli_angles[1],a[1],g->heli_turn);
    g->heli_angles[2]=0;return 0;
}
static void heli_speed_tick(Gang *g,float distance) /* 7dd0 */
{
    float wanted=g->wp_speed[g->wpi];
    if(distance<=g->route_brake && g->speed>wanted) {
        g->heli_brake_steps--;g->speed-=HELI_DEC;if(g->speed<wanted)g->speed=wanted;
    }
    if(g->speed<wanted){g->speed+=HELI_ACC;if(g->speed>wanted)g->speed=wanted;}
}
static void heli_integrate(Gang *g) /* 7d4c: old direction, not recomputed after braking */
{
    for(int k=0;k<3;k++)g->pos[k]+=g->dir[k]*g->speed;
    memcpy(g->heli_candidate,g->pos,12);
}
static void heli_consume_contact(Gang *g,int landing) /* 8c08 / 8a68 */
{
    if(g->heli_contact) {
        if(landing)memcpy(g->heli_candidate,g->heli_hit,12);
        g->flags=(g->flags&~0x80)|0x200;
    } else g->flags=(g->flags|0x80)&~0x200;
    memcpy(g->pos,g->heli_candidate,12);g->heli_contact=0;
}
static void heli_queue_probe(Gang *g,int landing) /* 8b9c / 8c84 */
{
    float a[3],b[3];memcpy(a,g->pos,12);memcpy(b,g->heli_candidate,12);
    if(!landing){a[1]+=5.f;b[1]-=1.f;b[0]+=g->dir[0]*45.f;b[2]+=g->dir[2]*45.f;}
    g->heli_contact=gang_heli_probe && gang_heli_probe(g_wall_ctx,a,b,landing ? 1:3,g->heli_hit);
}
static int heli_move(Gang *g,int crash) /* 7fd4 / 8778 */
{
    if(crash){heli_consume_contact(g,0);if(g->flags&0x200)return 1;}
    float v[3];for(int k=0;k<3;k++)v[k]=g->target[k]-g->pos[k];
    float distance=sqrtf((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]);
    heli_speed_tick(g,distance);int arrived=heli_heading(g,v,distance);
    if(crash && arrived)return 1;
    heli_forward(g);
    if(crash){for(int k=0;k<3;k++)g->heli_candidate[k]+=g->dir[k]*g->speed;heli_queue_probe(g,0);}
    else heli_integrate(g);
    heli_bank_tick(g);return arrived;
}
/* 0a8648: the attack pass follows X/Z while descending onto the route.
   It consumes last tick's probe and does not use flight's 45-unit lookahead. */
static int heli_lowpass(Gang *g)
{
    heli_consume_contact(g,1);
    float v[3]={g->target[0]-g->pos[0],0,g->target[2]-g->pos[2]};
    float distance=sqrtf(v[0]*v[0]+v[2]*v[2]);
    heli_speed_tick(g,distance);int arrived=heli_heading(g,v,distance);heli_forward(g);
    g->heli_candidate[0]+=g->dir[0]*g->speed;
    g->heli_candidate[2]+=g->dir[2]*g->speed;
    if(g->flags&0x80)g->heli_candidate[1]-=2.77777791f;
    if(g->speed!=0) {
        float a[3],b[3];memcpy(a,g->pos,12);memcpy(b,g->heli_candidate,12);a[1]+=5;b[1]-=1;
        g->heli_contact=gang_heli_probe && gang_heli_probe(g_wall_ctx,a,b,3,g->heli_hit);
    }
    return arrived;
}
static int heli_landing(Gang *g) /* 88b8 */
{
    heli_consume_contact(g,1);if(!(g->flags&0x80))return 1;
    g->heli_candidate[1]-=2.77777791f;heli_queue_probe(g,1);return 0;
}
static int heli_wrap_index(const Gang *g,int i)
{ return i<0 ? g->nwp-1:i>=g->nwp ? 0:i; }
static void heli_choose_route(Gang *g,const float *player) /* 0a81c8 */
{
    int nearest=-1;float best=0;
    for(int i=0;i<g->nwp;i++)if(!(g->wp_action[i]&0x18)) {
        float x=player[0]-g->wp[i][0],y=player[1]-g->wp[i][1],z=player[2]-g->wp[i][2];
        float d=(x*x+y*y)+z*z;
        if(nearest<0 || d<best){nearest=i;best=d;}
    }
    if(nearest<0)return;
    int f=g->wpi,b=f;
    if(f==nearest) {
        for(int n=0;n<g->nwp;n++) {f=heli_wrap_index(g,f+1);if(!(g->wp_action[f]&0x18))break;}
        float dot=0;for(int k=0;k<3;k++)dot+=(player[k]-g->pos[k])*(g->wp[f][k]-g->pos[k]);
        g->burst_left=dot>0?1:-1;g->flags|=0x2000000;return;
    }
    float forward=0,backward=0;
    /* Preserve the source's interleaved comparison and tie direction. */
    for(int n=0;n<g->nwp*2+2;n++) {
        if(f==nearest){if(forward<=backward){g->burst_left=1;return;}}
        else {forward+=g->wp_aux[f];f=heli_wrap_index(g,f+1);}
        if(b==nearest){if(backward<=forward){g->burst_left=-1;return;}}
        else {b=heli_wrap_index(g,b-1);backward+=g->wp_aux[b];}
    }
}
static void heli_choose_crash(Gang *g) /* 8e5c: weighted bidirectional search */
{
    int f=g->wpi,b=f;float fw=0.f,bw=0.f;
    for(int n=0;n<g->nwp;n++) {
        if(g->wp_action[f]&8) {
            if(fw<=bw){g->heli_crash_entry=f;g->burst_left=1;return;}
        } else {fw+=g->wp_aux[f];f=heli_wrap_index(g,f+1);}
        if(g->wp_action[b]&8) {
            if(bw<=fw){g->heli_crash_entry=b;g->burst_left=-1;return;}
        } else {
            b=heli_wrap_index(g,b-1);
            bw+=g->wp_aux[(g->wp_action[b]&8) ? heli_wrap_index(g,b-1):b];
        }
    }
}
static int heli_advance_crash(Gang *g) /* 8f6c, physical +1 check before signed traversal */
{
    int next=heli_wrap_index(g,g->wpi+1);
    if(g->wp_action[next]&8)g->flags|=0x800000;
    else {
        next=g->wpi;int found=0;
        /* Source has no bound; refuse malformed all-control routes safely. */
        for(int n=0;n<g->nwp;n++) {next=heli_wrap_index(g,next+g->burst_left);
            if(!(g->wp_action[next]&0x18)){found=1;break;}}
        if(!found)return 0;
    }
    g->wpi=next;g->next_action=g->wp_action[next];memcpy(g->target,g->wp[next],12);
    heli_set_brake_distance(g);return 1;
}
