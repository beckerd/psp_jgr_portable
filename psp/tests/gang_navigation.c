/* Synthetic geometry/parameters only. Exercise the production navigation. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/gang.h"
#define P_ACC 6
#define P_DEC 7
#define P_MAXTURN 21
int (*gang_wall)(void *,const float *,const float *);
static void *g_wall_ctx;
/* Host angle adapter; private integration also exercises the SH-4 table. */
static float heli_sin(int a){return sinf((a&65535)*0.000095873799242853f);}
static float heli_cos(int a){return heli_sin(a+16384);}
static int heli_turn(int a,int b,int cap){int d=(int16_t)(b-a);return d>cap?a+cap:d < -cap?a-cap:b;}
static void heli_vector_angles(const float *v,int *a)
{if(fabsf(v[0])>=.1f || fabsf(v[2])>=.1f)a[1]=(int)lrintf(atan2f(v[0],v[2])*10430.378350470453f);}
#include "../src/gang_navigation.inc"
static const float parameters[24]={[3]=2,[4]=3,[6]=.25f,[7]=.5f,[9]=6,[10]=8,[11]=8,[12]=2,[13]=3,[16]=1000,[21]=8192};
static GNode nodes[3];static GGraph graph;static Graphs routes;
static int links[]={1,0,2,1};
static float portals[][3]={{0,0,15},{0,0,15},{15,0,30},{15,0,30}};
static void setup(void)
{
    memset(nodes,0,sizeof nodes);
    nodes[0]=(GNode){.min={-10,-10,-10},.max={10,10,15},.pos={0,0,0},.nlinks=1,.link0=0};
    nodes[1]=(GNode){.min={-10,-10,15},.max={15,10,40},.pos={0,0,30},.nlinks=2,.link0=1};
    nodes[2]=(GNode){.min={15,-10,20},.max={50,10,40},.pos={30,0,30},.nlinks=1,.link0=3};
    graph=(GGraph){.n=3,.nodes=nodes};routes=(Graphs){.nlinks=4,.link=links,.lpos=&portals[0][0]};
}
static Gang group(int kind)
{return (Gang){.active=1,.kind=kind,.type=kind,.par=parameters,.graph=&graph,.dir={0,0,1},.node=0,.route_brake=2};}
static int wall_calls;
static int wall(void *c,const float *a,const float *b){(void)c;(void)a;(void)b;wall_calls++;return 1;}
static void pursuit(void)
{
    Gang g=group(3);g.state=7;g.flags=2;g.speed=1;g.npath=1;g.path[0]=2;
    g.target[2]=12;gang_wall=wall;
    for(int t=0;t<120;t++)assert(riot_approach(&g,0)>=0);
    assert(fabsf(g.pos[0])<.001f && g.pos[2]>4 && g.pos[2]<=12 && g.speed==0 && wall_calls==0);
    /* Reject a step off the graph without changing position or heading. */
    g.pos[0]=49;g.pos[2]=30;g.dir[0]=1;g.dir[2]=0;g.target[0]=100;g.target[2]=30;g.speed=3;
    float before[3],heading[3];memcpy(before,g.pos,12);memcpy(heading,g.dir,12);
    assert(riot_approach(&g,0)<0 && !memcmp(g.pos,before,12) && !memcmp(g.dir,heading,12));
}
static void path(void)
{
    const int families[]={3,4,6,9,25};
    for(unsigned i=0;i<sizeof families/sizeof *families;i++) {
        Gang g=group(families[i]);g.state=4;memcpy(g.target,nodes[2].pos,12);
        assert(plan_route(&g,&routes)==1 && g.npath==2);float pt[3];route_point(&g,pt);
        assert(!memcmp(pt,nodes[0].pos,12));g.speed=1;
        assert(follow_target(&g));route_point(&g,pt);assert(!memcmp(pt,portals[0],12));
        /* Entering a destination box alone must not skip its portal. */
        g.pos[0]=3;g.pos[2]=15;g.dir[0]=-1;g.dir[2]=0;
        assert(follow_target(&g) && g.pathi==0);
        memcpy(g.pos,portals[0],12);assert(follow_target(&g));
        assert(g.pathi==1 && g.node==1);route_point(&g,pt);assert(!memcmp(pt,nodes[1].pos,12));
        memcpy(g.pos,pt,12);assert(follow_target(&g));route_point(&g,pt);assert(!memcmp(pt,portals[2],12));
        memcpy(g.pos,pt,12);assert(!follow_target(&g) && g.node==2 && g.pathi==g.npath);
        /* Already beyond the centre: do not turn around to visit it. */
        g=group(families[i]);g.pos[2]=5;memcpy(g.target,nodes[2].pos,12);
        assert(plan_route(&g,&routes)==1);route_point(&g,pt);assert(!memcmp(pt,portals[0],12));
    }
}
static void graph_exit(void)
{
    const int families[]={3,4,6,9,25,21};
    for(unsigned i=0;i<sizeof families/sizeof *families;i++) {
        Gang g=group(families[i]);g.state=4;g.npath=2;g.pathi=1;g.speed=2;
        gang_player_graph(&g,NULL);
        assert(!g.graph && !g.npath && !g.pathi && g.node==-1);
        float before[3];memcpy(before,g.pos,12);
        assert(riot_approach(&g,0)<0 && !memcmp(before,g.pos,12));
        gang_player_graph(&g,&graph);assert(g.graph==&graph && g.node==-1);
    }
}
static void wall_route(void)
{
    Gang g=group(3);g.state=4;g.speed=1;g.target[2]=12;gang_wall=wall;
    follow_target(&g);assert(wall_calls==0 && fabsf(g.pos[0])<.001f && g.pos[2]>.99f);
}
int main(int argc,char **argv)
{
    assert(argc==2);setup();
    if(!strcmp(argv[1],"pursuit"))pursuit();
    else if(!strcmp(argv[1],"path"))path();
    else if(!strcmp(argv[1],"graph"))graph_exit();
    else if(!strcmp(argv[1],"wall"))wall_route();
    else assert(0);
    return 0;
}
