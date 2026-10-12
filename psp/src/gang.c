#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gang.h"
#include "tank_orders.h"
int (*gang_wall)(void *ctx, const float *from, const float *to); static void *g_wall_ctx;
void gang_parent_finish(unsigned *flags,int state,int children)
{ if(state && !children)*flags|=6; } /* 070454, parent +6c/+6e/+1c */
int gang_clock_steps(GangClock *clock,float dt,unsigned pressed)
{
    const float tick=1.f/30.f;int steps=0;
    clock->pressed|=pressed;clock->fraction+=dt;
    if(clock->fraction>4*tick)clock->fraction=4*tick;
    while(clock->fraction>=tick){clock->fraction-=tick;steps++;}
    return steps;
}
unsigned gang_clock_pressed(GangClock *clock)
{ unsigned pressed=clock->pressed;clock->pressed=0;return pressed; }

static unsigned rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static float rdf(const unsigned char *p) { unsigned u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

void graphs_free(Graphs *G) { for (int a = 0; a < 6; a++) { for (int i = 0; i < G->area[a].ngraphs; i++) free(G->area[a].g[i].nodes); free(G->area[a].g); } free(G->link); free(G->lpos); memset(G, 0, sizeof *G); }
int graphs_load(Graphs *G, const char *path)
{
    graphs_free(G);
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc(n); if (!d || fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return -2; }
    fclose(f);
    if (memcmp(d, "GRA1", 4)) { free(d); return -3; }
    const unsigned char *q = d + 8;
    for (int a = 0; a < 6; a++) {
        int ng = rd32(q); q += 4; G->area[a].ngraphs = ng; G->area[a].g = calloc(ng + 1, sizeof(GGraph));
        for (int i = 0; i < ng; i++) {
            GGraph *g = &G->area[a].g[i]; g->n = rd32(q); g->has_xf = rd32(q + 4); for (int k = 0; k < 12; k++) g->xf[k] = rdf(q + 8 + 4 * k); q += 56;
            g->nodes = calloc(g->n + 1, sizeof(GNode));
            for (int j = 0; j < g->n; j++) { GNode *nd = &g->nodes[j]; for (int k = 0; k < 9; k++) (&nd->min[0])[k] = rdf(q + 4 * k); nd->nlinks = rd32(q + 36); nd->link0 = rd32(q + 40); q += 44; }
        }
    }
    G->nlinks = rd32(q); q += 4; G->link = calloc(G->nlinks + 1, sizeof(int)); G->lpos = calloc(3 * (G->nlinks + 1), sizeof(float));
    for (int i = 0; i < G->nlinks; i++) { G->link[i] = rd32(q); for (int k = 0; k < 3; k++) G->lpos[3 * i + k] = rdf(q + 4 + 4 * k); q += 16; }
    free(d); return 0;
}

const GGraph *graphs_pick(const Graphs *G, int area, const float *ppos)
{
    if (area < 0 || area >= 6) return 0;
    for (int i = 0; i < G->area[area].ngraphs; i++) if (graph_node_at(&G->area[area].g[i], ppos) >= 0) return &G->area[area].g[i];
    return 0;
}

#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_bomb_source.inc"
#else
#include "generated/source_data/bomb_source.inc"
#endif
/* Mixed float/integer parameter record 0x8c1b918c used by the Onishima
   controller. Integer fields 0/1/2 and 20..23 are represented numerically
   because the portable controller consumes them after an int cast. */
#if JGR_PORTABLE_ASSETS
#include "source_data_onishima_source.inc"
#else
#include "generated/source_data/onishima_source.inc"
#endif
/* Mixed record 0x8c21b524 used by Taban.  It shares Onishima's locomotion
   constants but attacks at the source's 50-unit threshold (+0x20). */
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_taban_source.inc"
#else
#include "generated/source_data/taban_source.inc"
#endif

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_gang_c__par_t0_0.inc"
#else

#include "generated/source_data/gang_c__PAR_T0_0.inc"

#endif
 /* 8c1b7264 */

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_gang_c__par_t1_0.inc"
#else

#include "generated/source_data/gang_c__PAR_T1_0.inc"

#endif
   /* 0x8c1b752c type 1 */
#if JGR_PORTABLE_ASSETS
#include "source_data_dog_source.inc"
#else
#include "generated/source_data/dog_source.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_riot_source.inc"
#else
#include "generated/source_data/riot_source.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_killer_source.inc"
#else
#include "generated/source_data/killer_source.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_para_source.inc"
#else
#include "generated/source_data/para_source.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_burning_source.inc"
#else
#include "generated/source_data/burning_source.inc"
#endif
static const float *par_for_type(int type) { switch (type) { case 0: return PAR_T0; case 1: return PAR_T1; case 2: return PAR_T2; case 3: return PAR_T3; case 4: return PAR_T4; case 6: return PAR_T6; case 7: return PAR_T7; case 9: return PAR_T9; case 0x17: return GANG_PAR_B; case 0x19: return PAR_T25; default: return GANG_PAR_A; } }
#define P_IDLE 0
#define P_REGROUP 1
#define P_CHASECHK 2
#define P_MAXSPD 3
#define P_CHASESPD 4
#define P_ACC 6
#define P_DEC 7
#define P_RANGE 8
#define P_MAXTURN 21
static int heli_atan(float y,float x);
static void solo_wall_turn(int a[3],const float n[3],int choice);
static int heli_turn(int a,int b,int cap);
static float heli_sin(int a);
static float heli_cos(int a);
static void heli_vector_angles(const float *v,int *a);

/* FUN_8c07c228 indexes these two source tables by member count.  The first
   table (0x8c1b8160) supplies each Para's destination/ground slot and the
   second (0x8c1b8228) its initial airborne position.  Both vectors are
   transformed by the creator's Ninja Y matrix before being stored. */

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_gang_c__para_ground_0.inc"
#else

#include "generated/source_data/gang_c__PARA_GROUND_0.inc"

#endif


#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_gang_c__para_air_0.inc"
#else

#include "generated/source_data/gang_c__PARA_AIR_0.inc"

#endif


/* The creator's +0x94 pointer is indexed by member count, then consumed by
   FUN_8c1034bc once per member.  These are the exact vectors behind pointer
   tables 0x8c1b7250 (Keikan), 0x8c1b76e0 (alternate Kidoutai), and the
   shared layout at 0x8c1b7518/7b30/7df8/a3b8/8868. */

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_gang_c__form_common_0.inc"
#else

#include "generated/source_data/gang_c__FORM_COMMON_0.inc"

#endif


#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_gang_c__form_keikan_0.inc"
#else

#include "generated/source_data/gang_c__FORM_KEIKAN_0.inc"

#endif


#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_gang_c__form_kidoutai_alt_0.inc"
#else

#include "generated/source_data/gang_c__FORM_KIDOUTAI_ALT_0.inc"

#endif


static const float *formation_offset(int kind, int count, int index)
{
    const float (*table)[5][3] = kind == 0 ? FORM_KEIKAN : kind == 4 ? FORM_KIDOUTAI_ALT : FORM_COMMON;
    return table[count - 1][index];
}


/* Duration of semantic animation 9's slot-0 entry clip.  These values are
   the frame words in the private ENEMY banks selected by the creator table. */
static int formation_entry_frames(int kind)
{
    switch (kind) {
    case 0: return 21;                  /* 001_keikandat */
    case 1: case 4: case 27: return 29; /* 003_kidoutaidat */
    case 2: return 18;                  /* 005_inudat */
    case 3: case 29: return 20;         /* 007_killerdat */
    case 5: case 23: return 34;         /* 009_bombdat */
    case 6: return 27;                  /* 011_kairikidat semantic 0 -> raw 1 */
    case 9: case 25: return 34;         /* 017_firedat */
    default: return 1;
    }
}

/* FUN_8c077a44 -> FUN_8c077b5c -> FUN_8c077bac.  The creator starts every
   ordinary formation member in state 0 / semantic animation 8 / flags
   0x101.  Once the leader clears flag 1, state 1 plays semantic animation 9
   once and state 2 enters the normal state-0 animation dispatcher. */
static void formation_member_entry_tick(Gang *g)
{
    if (g->type == 7) return;   /* Para has its own airborne states 20..24. */
    for (int i = 0; i < g->nmem; i++) {
        GangMember *m = &g->m[i];
        if (m->state == 0) { m->state = 1; m->anim = 8; }
        if (!(g->flags & 1)) m->flags &= ~1;
        if (m->state == 1 && !(m->flags & 1)) {
            m->state = 2; m->anim = 9; m->frame = 0.f;
        } else if (m->state == 2 && m->frame >= formation_entry_frames(g->kind) - 1) {
            m->state = 3; m->anim = 0; m->frame = 0.f;
        }
    }
}

void gang_spawn(Gang *g, int kind, int type, const float *pos, float yaw, int nmem, const GGraph *graph, int trig, const float *quad)
{
    memset(g, 0, sizeof *g); g->trig = trig; if (quad) memcpy(g->quad, quad, 32); g->active = 1; g->kind = kind; g->type = type; g->par = par_for_type(type);
    memcpy(g->pos, pos, 12); g->dir[0] = sinf(yaw); g->dir[2] = cosf(yaw); g->graph = graph;
    g->nmem = nmem > 5 ? 5 : nmem;   /* all source +0x94 formation tables stop at member count five */
    if (type == 6) g->nmem = 1;   /* FUN_8c08cff8 overwrites group +0xbc with one */
    if (type == 7 && g->nmem > 5) g->nmem = 5;   /* source tables contain counts 1..5 */
    for (int i = 0; i < g->nmem; i++) {
        GangMember *m = &g->m[i];
        if (type == 7 && g->nmem > 0) {
            const float *dst = PARA_GROUND[g->nmem - 1][i], *air = PARA_AIR[g->nmem - 1][i];
            for(int k=0;k<3;k++)m->off[k]=-dst[k];
            m->pos[0] = pos[0] - air[0] * g->dir[2] - air[2] * g->dir[0];
            m->pos[1] = pos[1] + air[1];
            m->pos[2] = pos[2] + air[0] * g->dir[0] - air[2] * g->dir[2];
            m->flags = 0x100101;m->police.para=1;
        } else {
            const float *off = formation_offset(kind, g->nmem, i);
            memcpy(m->off, off, 12);
            memcpy(m->pos, pos, 12);
        }
        m->yaw = yaw; m->motion = -1;
        if(kind==2)m->police.dog=1;
        if(kind==4)m->police.riot=1;
        if(kind==5 || kind==23)m->police.bomb=1;
        if(kind==3 || kind==29)m->police.killer=1;
        if(kind==9 || kind==25)m->police.fire=kind;
        if(kind==6){m->police.kairiki=1;m->police.pose_frame=m->police.gas_pose_frame=-1;}
        /* FUN_8c077a44 and the private Dog/Kairiki initializers all enter
           member state 0 with semantic animation state 8 and flags 0x101. */
        m->state = 0; m->anim = type == 7 ? 20 : 8;
        if (type != 7) m->flags = 0x101;
    }
    g->state = 0; g->unit_state = type == 7 ? 20 : 0; g->flags = 0x101; g->node = -1;
}

#include "gang_navigation.inc"

/* FUN_8c06e918 (trigger kind 0): the player's xz inside the sub-record quad (two triangles, either winding) */
static int xz_cross(float ax, float az, float bx, float bz) { float c = ax * bz - az * bx; return c < 0.f ? -1 : c > 0.f ? 1 : 0; }
static int xz_in_tri(float px, float pz, const float *a, const float *b, const float *c)
{
    int s0 = xz_cross(a[0] - px, a[1] - pz, b[0] - px, b[1] - pz), s1 = xz_cross(b[0] - px, b[1] - pz, c[0] - px, c[1] - pz), s2 = xz_cross(c[0] - px, c[1] - pz, a[0] - px, a[1] - pz);
    return (s0 <= 0 && s1 <= 0 && s2 <= 0) || (s0 >= 0 && s1 >= 0 && s2 >= 0);
}
int gang_force_activate;   /* debug: `gangtest` file */
static int gang_trigger_geometry(const Gang *g, const float *ppos)
{
    if (gang_force_activate) return 1;
    if (g->trig == 0) return xz_in_tri(ppos[0], ppos[2], g->quad, g->quad + 2, g->quad + 4) || xz_in_tri(ppos[0], ppos[2], g->quad + 6, g->quad + 2, g->quad + 4);
    if (g->trig == 5) { /* 06ef84: the same quad, in the leader's local yaw frame. */
        float dx=ppos[0]-g->pos[0],dz=ppos[2]-g->pos[2];
        float x=dx*g->dir[2]-dz*g->dir[0],z=dx*g->dir[0]+dz*g->dir[2];
        return xz_in_tri(x,z,g->quad,g->quad+2,g->quad+4) || xz_in_tri(x,z,g->quad+6,g->quad+2,g->quad+4);
    }
    if (g->trig == 1) { /* 06ea88: radius AND signed BAM aperture. */
        float dx=ppos[0]-g->pos[0],dz=ppos[2]-g->pos[2];int limit;
        memcpy(&limit,g->quad,sizeof limit);
        if(dx*dx+dz*dz>g->quad[1]*g->quad[1])return 0;
        int angle=abs(heli_atan(dx*g->dir[2]-dz*g->dir[0],dx*g->dir[0]+dz*g->dir[2]));
        return angle<=limit;
    }
    if (g->trig == 4) { float dx = ppos[0] - g->pos[0], dz = ppos[2] - g->pos[2]; return dx * dx + dz * dz <= g->quad[0] * g->quad[0]; }   /* FUN_8c06ef34: radius is the first float at group +0xc8 */
    if ((g->trig == 2 || g->trig == 3) && g->have_prev) {   /* FUN_8c06ec70 / FUN_8c06eda4 (FUN_8c06eb24): the move prev->cur crosses the edge quad[0..1] -> quad[2..3] */
        float ax = g->pprev[0], az = g->pprev[2], bx = ppos[0], bz = ppos[2], cx = g->quad[0], cz = g->quad[1], dx = g->quad[2], dz = g->quad[3];
        float d1 = (bx - ax) * (cz - az) - (bz - az) * (cx - ax), d2 = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
        float d3 = (dx - cx) * (az - cz) - (dz - cz) * (ax - cx), d4 = (dx - cx) * (bz - cz) - (dz - cz) * (bx - cx);
        return ((d1 < 0.f && d2 > 0.f) || (d1 > 0.f && d2 < 0.f)) && ((d3 < 0.f && d4 > 0.f) || (d3 > 0.f && d4 < 0.f));
    }
    return 0;
}
static int gang_triggered(Gang *g,const float *ppos)
{
    /* 06e40c: event 1 sets a one-shot trigger bit on each child. Consume
       it here, at the source trigger call, rather than guessing activation
       from a later AI state. The geometric predicates still need a full audit. */
    int forced=(g->flags&0x1000)!=0;
    if(forced)g->flags&=~0x1000;
    if(!forced && !gang_trigger_geometry(g,ppos))return 0;
    if(g->parent_flags)*g->parent_flags|=1;
    return 1;
}

/* Type 16 is Sniper, established by FUN_8c09bf24 relocating all 13 pointers
   at 0x8c1b60a0 into 031_sniperdat.bin.  Its source object is stationary:
   states 2/3 wait and aim, state 4 starts animation sub-state 1, and state 5
   waits for that 19-frame firing clip.  `rank` is the generic entry's final
   word; FUN_8c09beb8 stores rank-1 at task +0x84 to choose the paired clips. */
void gang_spawn_sniper(Gang *g, const float *pos, float yaw, int rank, int trig, const float *quad)
{
    memset(g,0,sizeof *g);g->active=1;g->kind=g->type=16;g->mode=5;g->trig=trig;
    if(quad)memcpy(g->quad,quad,32);
    memcpy(g->pos,pos,12);g->dir[0]=sinf(yaw);g->dir[2]=cosf(yaw);g->action=rank>0?rank-1:0;
    g->state=1;g->flags=0x10100;g->nmem=1;memcpy(g->m[0].pos,pos,12);
    g->m[0].yaw=g->m[0].visual_yaw=yaw;g->m[0].motion=-1;g->m[0].secondary_anim=-1;g->m[0].sniper.angles[1]=(int)lrintf(yaw*(65536.f/6.28318530718f));g->log_state=-1;
}
static void sniper_consume_terrain(Gang *g) /* 09cc30 */
{
    GangMember *m=g->m;RivalTerrain *h=&m->sniper.terrain;float *p=m->sniper.pending;
    if(g->flags&0x100)return;
    g->flags&=~0x200;
    if(g->speed==0 && g->vy==0){memcpy(g->pos,p,12);return;}
    if(h->hit) {
        p[0]=h->point[0];p[2]=h->point[2];
        if(h->wall){memcpy(m->sniper.normal,h->normal,12);for(int k=0;k<3;k++)p[k]+=h->normal[k];g->flags|=0x200;}
    }
    if(h->floor) {
        p[1]=h->height;float delta[3]={p[0]-g->pos[0],0,p[2]-g->pos[2]};
        int a[3];memcpy(a,m->sniper.angles,sizeof a);heli_vector_angles(delta,a);
        for(int k=0;k<2;k++)m->sniper.angles[k]=heli_turn(m->sniper.angles[k],a[k],0x1555);
        m->sniper.angles[2]=0;g->vy=p[1]-g->pos[1];g->flags&=~0x80;
    } else {g->vy=g->vy>0?0:fmaxf(-2.777777910232544f,g->vy-.2f);m->sniper.angles[0]=0;g->flags|=0x80;}
    memcpy(g->pos,p,12);memset(h,0,sizeof *h);
}
static void sniper_tick(Gang *g,const float *ppos,float (*ground)(void *,float,float,float),void *gctx)
{
    (void)ground;(void)gctx;
    GangCombat *c=g->combat;GangMember *m=g->m;
    if(g->flags&0x20){g->active=0;return;}
    if(g->state==8)sniper_consume_terrain(g);
    else if(m->sniper.body && g->state!=3 && !(g->flags&0x100)){g->state=6;g->unit_state=2;}
    int old_state=g->state;
    float dx=ppos[0]-g->pos[0],dy=ppos[1]-g->pos[1],dz=ppos[2]-g->pos[2];
    /* 09cf84 uses the original placed forward vector, not the laser aim. */
    int close=dx*dx+dy*dy+dz*dz<2500.f && dx*g->dir[0]+dy*g->dir[1]+dz*g->dir[2]>0;
    int trigger=(g->state==3 || (g->state==2 && !(g->flags&0x10) && !close && g->timer<1))?gang_triggered(g,ppos):0;
    SniperInput in={trigger,close,
        c && c->shot_result?c->shot_result(c->context,m->gun.probe)>0:0,
        c?c->vulnerable:0,c?c->history_at:0,c?c->player_speed:0,m->sniper.beam};
    uint32_t flags=g->flags,shot_flags=flags;
    unsigned events=sniper_step(&g->state,&g->timer,&g->unit_state,&g->fire_cursor,&flags,&in);
    g->flags=flags;
    if(events&SNIPER_WARNING && c)c->warning=1;
    if(events&SNIPER_AIM && c) {
        const float *aim=(g->flags&0x80000)?c->target:c->history[g->fire_cursor];
        float ax=aim[0]-g->pos[0],ay=aim[1]-g->pos[1],az=aim[2]-g->pos[2];
        float vector[3]={ax,ay,az};int angles[3]={0,m->sniper.angles[1],0};heli_vector_angles(vector,angles);
        if(!g->action) {
            /* 09d378: rank 1 snaps the render rotation, saving its previous
               yaw for the left/right aiming clip. Record angles stay put. */
            int previous=(int)lrintf(m->yaw*(65536.f/6.28318530718f));
            m->aim=(int16_t)(previous-angles[1]);
            m->yaw=angles[1]*(6.28318530718f/65536.f);
        } else m->aim=(int16_t)(angles[1]-m->sniper.angles[1]);
        m->secondary_anim=g->action && m->aim?(m->aim<0?10:9):-1;
        m->sniper.blend=fminf(1.f,fabsf((float)m->aim)/8192.f);
    }
    if(old_state==5 && g->state==2) {
        m->pitch=m->sniper.angles[0]*(6.28318530718f/65536.f);
        m->yaw=m->sniper.angles[1]*(6.28318530718f/65536.f);m->roll=m->sniper.angles[2]*(6.28318530718f/65536.f);
    }
    if(c && c->anchor && c->shot && (events&(SNIPER_QUERY|SNIPER_FIRE))) {
        float origin[3];
        if(c->anchor(c->context,g,10,origin)) {
            if(events&SNIPER_QUERY)m->gun.probe=c->shot(c->context,origin,c->target,16,2);
            if(events&SNIPER_FIRE)c->shot(c->context,origin,(shot_flags&0x80000)?c->target:c->history[g->fire_cursor],16,(shot_flags&0x80000)?1:0);
        }
    }
    if(events&SNIPER_TURN) {
        if(old_state==6){float v[3]={-dx,-dy,-dz};int a[3]={0,m->sniper.angles[1],0};heli_vector_angles(v,a);m->sniper.target=a[1];}
        else {m->sniper.angles[1]=heli_turn(m->sniper.angles[1],m->sniper.target,0x1555);m->yaw=m->sniper.angles[1]*(6.28318530718f/65536.f);}
        if(g->state==8)memcpy(m->sniper.pending,g->pos,12);
    }
    if(events&SNIPER_RUN) {
        g->speed=fminf(3.7037038803100586f,g->speed+.18518517911434174f);
        if(g->flags&0x200){unsigned r=c && c->random15?c->random15(c->context):(unsigned)rand()&32767;
            solo_wall_turn(m->sniper.angles,m->sniper.normal,(int)((float)r/32768.f+.5f));}
        float cp=heli_cos(m->sniper.angles[0]);
        m->sniper.pending[0]+=heli_sin(m->sniper.angles[1])*cp*g->speed;
        m->sniper.pending[1]+=g->vy;
        m->sniper.pending[2]+=heli_cos(m->sniper.angles[1])*cp*g->speed;
        m->yaw=m->sniper.angles[1]*(6.28318530718f/65536.f);
    }
    if(m->anim!=g->unit_state){m->frame=0;m->anim=g->unit_state;}
    else if(!(g->flags&0x120)) {
        /* 09be9c is the walk/run blend threshold, separate from run speed. */
        m->frame+=g->unit_state==3 && g->speed<1.388888955116272f ? fminf(1.f,1.2f-g->speed/1.388888955116272f):1.f;
        if(g->unit_state==1 && m->frame>=19.f)g->unit_state=0;
        if(g->unit_state==2 && m->frame>=(g->action?33.f:16.f))g->unit_state=3;
    }
    if(g->state!=g->log_state){printf("sniper: state %d->%d timer %d rank %d\n",g->log_state,g->state,g->timer,g->action+1);g->log_state=g->state;}
    m->pitch=m->sniper.angles[0]*(6.28318530718f/65536.f);
    m->roll=m->sniper.angles[2]*(6.28318530718f/65536.f);
    /* Collision registrations are consumed on the following source tick. */
    m->sniper.beam=m->sniper.body=0;
    if(c && !(g->flags&0x10100)) {
        if(g->state==8 && c->rival_terrain)c->rival_terrain(c->context,g->pos,m->sniper.pending,&m->sniper.terrain);
        else if(c->sniper_contact)m->sniper.body=c->sniper_contact(c->context,g);
    }
    if(g->state==3 && c && c->anchor && c->beam_contact) {
        float origin[3];if(c->anchor(c->context,g,10,origin))m->sniper.beam=c->beam_contact(c->context,origin,
            (g->flags&0x80000)?c->target:c->history[g->fire_cursor],1.f);
    }
    memcpy(m->pos,g->pos,12);
}

/* Type 22 is Denki: FUN_8c0fd120's five relocated motion pointers all match
   037_denkidat.bin.  The actor stays at its record transform.  The constants
   below are the live parameter block at 0x8c21bbb8: three discharges, 60-tick
   interval, 120-tick initial cooldown.  State/motion transitions follow
   FUN_8c0fd848..FUN_8c0fdc44 and semantic motions 0..4 map identically. */
void gang_spawn_denki(Gang *g, const float *pos, float yaw, int trig, const float *quad)
{
    memset(g, 0, sizeof *g); g->active = 1; g->kind = 22; g->type = 22; g->mode = 6; g->trig = trig; if (quad) memcpy(g->quad, quad, 32);
    memcpy(g->pos, pos, 12); g->dir[0] = sinf(yaw); g->dir[2] = cosf(yaw); g->state = 0; g->unit_state = 0; g->nmem = 1;
    memcpy(g->m[0].pos, pos, 12); g->m[0].yaw = yaw; g->m[0].motion = -1; g->m[0].anim=-1; g->log_state = -1;
    g->m[0].denki.delay=60;g->m[0].denki.scale=.1f;
    for(int i=0;i<3;i++)g->m[0].denki.arc[i]=-20*(i+1);
}

static void denki_tick(Gang *g, const float *ppos)
{
    float dx = ppos[0] - g->pos[0], dy = ppos[1] - g->pos[1], dz = ppos[2] - g->pos[2];
    float pdist = sqrtf(dx * dx + dy * dy + dz * dz);
    if(g->flags&0x20)return;
    GangMember *m=g->m;
    /* 0fd848/0fda0c: arc bursts, charge growth and discharge decay. */
    if(g->state==2 || g->state==4){
        for(int i=0;i<3;i++)if(--m->denki.arc[i]<-m->denki.delay){
            m->denki.arc[i]=9;m->denki.yaw[i]=2*(g->combat && g->combat->random15 ? g->combat->random15(g->combat->context):(unsigned)rand()&32767);
        }
        if(g->state==2){if(--m->denki.delay<0)m->denki.delay=0;m->denki.scale=fminf(1.5f,m->denki.scale+1.5f/120.f);}
        else {if(++m->denki.delay>60)m->denki.delay=60;m->denki.scale=fmaxf(.1f,m->denki.scale-1.5f/180.f);}
    }
    switch (g->state) {
    case 0: /* 0fd7c0: the original draw callback supplies visibility. */
        g->flags|=0x10100;g->state=1;break;
    case 1:   /* 0fd7e0: wait offscreen; proximity alone must not reveal it. */
        if ((g->flags&0x10000) || gang_force_activate) { g->flags&=~0x100;g->state = 2; g->timer = 120; }
        else if(g->flags&0x10){g->flags|=0x20;g->active=0;}
        break;
    case 2:   /* FUN_8c0fd848 */
        if (--g->timer < 0) { g->timer = 0; if (gang_triggered(g, ppos)) { g->state = 3; g->unit_state = 2; g->timer = 60; g->cnt = 0;m->denki.delay=0;m->denki.scale=1.5f;for(int i=0;i<3;i++)m->denki.arc[i]=1; if(g->combat && g->combat->sound)g->combat->sound(g->combat->context,0x160000,g->pos); } }
        break;
    case 3:   /* semantic motion 2 completes into looping attack motion 3 */
        if (g->unit_state != 2){g->state = 4;for(int i=0;i<3;i++)m->denki.arc[i]=i*15;}
        break;
    case 4:   /* FUN_8c0fda0c: one discharge whenever the strict >60 test wins */
        if (++g->timer > 60) {
            if(g->combat && g->combat->electric)g->combat->electric(g->combat->context,g);
            g->timer = 0; g->cnt++; if (g->cnt >= 3) { g->state = 5; g->unit_state = 4; m->denki.delay=60;m->denki.scale=.1f; }
        }
        break;
    case 5:   /* motion 4 completes to idle; FUN_8c0fdc44 picks 120..179 ticks */
        if (g->unit_state != 4) { g->state = 2; g->timer = 120 + ((g->combat && g->combat->random15 ? g->combat->random15(g->combat->context):(unsigned)rand() & 0x7fff) * 60) / 32768; }
        break;
    default: break;
    }
    /* 0fdcb4: the stationary ten-unit attack sphere is disabled during
       recovery and before visibility activation, and registered within 50. */
    if(!(g->flags&0x10100) && g->state!=5 && pdist<50.f && g->combat && g->combat->contact){
        const float point[3]={g->pos[0],g->pos[1]+10.342f,g->pos[2]};
        g->combat->contact(g->combat->context,point,point,10.f,24);
    }
    if (g->state != g->log_state) { printf("denki: state %d->%d pos %.0f,%.0f,%.0f timer %d burst %d pd %.0f\n", g->log_state, g->state, g->pos[0], g->pos[1], g->pos[2], g->timer, g->cnt, pdist); g->log_state = g->state; }
    /* 0fdd50 dispatches animation AFTER AI. Entry is frame zero;
       0bdf30 ends one-shots strictly after nframes-1, and idle/attack loop.
       Motion 0 is the 29-frame settle, then motion 1 is the idle loop. */
    if(g->flags&0x120)return;
    m->denki.frame+=1.f;if(m->denki.frame>=4.f)m->denki.frame-=4.f;
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_gang_c__lengths_0.inc"
#else

#include "generated/source_data/gang_c__lengths_0.inc"

#endif

    if(m->anim!=g->unit_state){m->anim=g->unit_state;m->frame=0;}
    else if(++m->frame>=(float)lengths[m->anim]){
        if(m->anim==1 || m->anim==3)m->frame-=lengths[m->anim];
        else {m->frame=lengths[m->anim]-1;g->unit_state=m->anim==0?1:m->anim==2?3:0;}
    }
}
#include "solo_member.inc"
/* Type 17 is Onishima (FUN_8c0858d8 -> FUN_8c085ba0).  The main-state
   dispatch is 0x8c1b926c: 0/1 initialize, 2 plans a route, 3 follows graph
   nodes, 4 approaches the waypoint, 5 chases, 7/8 report, 9 recovers,
   11 selects the next 16-byte waypoint, and 12 performs the timed turn. */

#include "onishima.inc"
static void patrol_anim(Gang *g, int state)
{
    if (g->unit_state != state) g->unit_state = state;
}
void gang_spawn_patrol(Gang *g, int kind, const float (*wp)[3], int nwp, float yaw, int trig, const float *quad)
{
    memset(g, 0, sizeof *g); g->active = 1; g->kind = kind; g->mode = 2; g->trig = trig; if (quad) memcpy(g->quad, quad, 32);
    g->nwp = nwp > GANG_MAX_WAYPOINTS ? GANG_MAX_WAYPOINTS : nwp; for (int i = 0; i < g->nwp; i++) memcpy(g->wp[i], wp[i], 12);
    memcpy(g->lastnode,wp[0],12);
    memcpy(g->pos, wp[0], 12); g->dir[0] = sinf(yaw); g->dir[2] = cosf(yaw); g->nmem = 1; memcpy(g->m[0].pos, wp[0], 12); g->m[0].yaw = yaw; g->m[0].motion = -1;
    g->par = PAR_ONISHIMA; g->state = 0; g->unit_state = 9; g->log_state = -1;
    g->m[0].anim=15;g->m[0].overlay=-1;g->m[0].chief.old_state=-1;g->m[0].chief.old_motion=-1;
}
static void patrol_tick(Gang *g, const Graphs *G, int area, const float *ppos, int police_attack, float (*ground)(void *, float, float, float), void *gctx)
{
    g->m[0].chief.old_yaw=g->m[0].yaw;
    const float *par = g->par; float dx = ppos[0] - g->pos[0], dz = ppos[2] - g->pos[2]; float pdist = sqrtf(dx * dx + dz * dz);
    /* 085ba0: the reveal task, not the R press, latches a completed tag.
       The current animation binding has no pending +120 transition. */
    if(g->graffiti && g->graffiti->complete && !(g->flags&0x400000))g->flags|=0x600000;
    if(g->flags&0x200000)g->state=15;
    switch (g->state) {
    case 0:   /* FUN_8c08603c */
        g->flags = 0x101; g->speed = 0.f; g->cnt = g->timer = 0;
        g->wpi = g->nwp > 1 ? 1 : 0; memcpy(g->target, g->wp[g->wpi], 12);
        patrol_anim(g, 9); g->state = 1; break;
    case 1:   /* FUN_8c086110: the PSP ground query has already settled the actor */
        g->flags &= ~0x101; patrol_anim(g, 0); g->state = 2; break;
    case 2: { /* FUN_8c08618c / FUN_8c06e36c */
        g->timer=0;
        if((g->flags&0x100000) && !graphs_pick(G,area,ppos)){decelerate(g);break;}
        g->graph=graphs_pick(G,area,g->pos);
        g->node=g->graph?graph_node_at(g->graph,g->pos):-1;
        if(g->node<0) {
            g->graph=graphs_pick(G,area,g->lastnode);
            int last=g->graph?graph_node_at(g->graph,g->lastnode):-1;
            if(last<0)decelerate(g);
            else {memcpy(g->target,g->graph->nodes[last].pos,12);g->npath=0;g->state=6;}
            break;
        }
        if(g->flags&0x100000)memcpy(g->target,ppos,12);
        int route=plan_route(g,G);
        g->state=route==1?3:(g->flags&0x100000)?5:4;break; }
    case 3:   /* FUN_8c0862f8: follow the planned graph path */
        patrol_anim(g, 1); onishima_accelerate(g, par[P_MAXSPD]);
        if (onishima_move(g)<0 || g->pathi>=g->npath) g->state = 2;
        if (gang_triggered(g, ppos) && !police_attack) { memcpy(g->target, ppos, 12); g->state = 5; g->cnt = 0; g->flags|=0x100000; }
        break;
    case 4:   /* FUN_8c08642c: direct waypoint approach */
        patrol_anim(g, 1); onishima_accelerate(g, par[P_MAXSPD]);
        g->npath = 0; {float left=onishima_move(g);if(left<0)g->state=2;else if(left<=par[9])g->state=11;}
        if (gang_triggered(g, ppos) && !police_attack) { memcpy(g->target, ppos, 12); g->state = 5; g->cnt = 0; g->flags|=0x100000; }
        break;
    case 5: { /* FUN_8c086578: chase until Onishima can report */
        patrol_anim(g, 1); memcpy(g->target, ppos, 12); g->npath = 0;
        onishima_accelerate(g, par[P_MAXSPD]);
        float left=onishima_move(g);
        if(left<0 || (g->graph && g->node!=graph_node_at(g->graph,ppos))){g->state=2;break;}
        if (left <= par[P_RANGE]) { g->flags |= 2; g->state = 7; g->cnt = 0; }
        break; }
    case 6:   /* 086648: recover to the last valid graph node, then replan. */
        patrol_anim(g,1);onishima_accelerate(g,par[P_MAXSPD]);onishima_move(g);
        g->node=g->graph?graph_node_at(g->graph,g->pos):-1;
        if(g->node>=0){g->state=2;memcpy(g->target,ppos,12);}
        break;
    case 7:   /* FUN_8c0866d8: whistle/report wind-up lasts 0x36 ticks */
        if(g->combat)g->combat->report=1;
        if (g->cnt++ == 0) {
            patrol_anim(g,5);g->m[0].chief.variant=(rand()&32767)>=16384;
            if(g->combat && g->combat->sound)g->combat->sound(g->combat->context,0x110002,g->pos);
        }
        memcpy(g->target, ppos, 12);g->npath=0;onishima_accelerate(g,par[P_CHASESPD]);onishima_move(g);
        if (g->cnt > 0x36) {
            g->state = 8; g->cnt = 0x36;
            g->m[0].gun=(EnemyGun){.delay=54,.left=6};g->flags&=~0x60000000u;
        }
        break;
    case 8:   /* FUN_8c0867d4: active police-manager report */
        if(g->combat)g->combat->report=1;
        memcpy(g->target, ppos, 12); g->npath = 0; onishima_accelerate(g, par[P_CHASESPD]); onishima_move(g);
        if (pdist > par[P_RANGE] || (g->combat && g->combat->player_state==8)) { g->flags &= ~2; g->state = 9; g->cnt = 0; }
        else if(g->combat) {
            GangCombat *c=g->combat;EnemyGun *gun=&g->m[0].gun;
            int blocked=c->shot_result ? c->shot_result(c->context,gun->probe)>0:0;
            EnemyGunEvents e=enemy_onishima_gun_tick(gun,(uint32_t *)&g->flags,blocked,c->vulnerable);
            float origin[3]={g->pos[0],g->pos[1]+10.f,g->pos[2]};
            if(e.query && c->shot)gun->probe=c->shot(c->context,origin,c->target,17,2);
            if(e.mode>=0 && c->shot)c->shot(c->context,origin,c->target,17,e.mode);
            if(e.warning)c->warning=1;
            if(e.first_shot) {
                patrol_anim(g,6);g->m[0].chief.old_state=-1;
                if(c->sound)c->sound(c->context,e.first_shot==1?0x110005:0x110006,g->pos);
            }
            if(e.finished){g->state=9;g->cnt=0;}
        }
        break;
    case 9:   /* FUN_8c086b90: 90-tick recovery */
        if (g->cnt++ == 0) {
            patrol_anim(g,7);g->speed=0;
            if(g->combat && g->combat->sound)g->combat->sound(g->combat->context,g->combat->player_state==8?0x110004:0x110003,g->pos);
        }
        if (g->cnt > 0x59) { g->flags &= ~0x20002; g->cnt = 0; patrol_anim(g, 0); g->state = 2; }
        break;
    case 11:  /* FUN_8c086d4c: select next waypoint and choose turn pose */
        g->speed = 0.f; g->wpi = (g->wpi + 1) % (g->nwp > 0 ? g->nwp : 1); memcpy(g->target, g->wp[g->wpi], 12);
        /* With no interaction target (+170), 06e134 preserves current
           yaw. The next route movement turns toward the new waypoint. */
        g->tgt_yaw=atan2f(g->dir[0],g->dir[2]);
        patrol_anim(g, 9); g->timer = 0x3c; g->state = 12; break;
    case 12: { /* FUN_8c086e7c: turn at parameter +0x54 (0x1555 BAM/tick) */
        float delta[3] = { sinf(g->tgt_yaw), 0.f, cosf(g->tgt_yaw) };
        steer(g->dir, delta, (int)par[P_MAXTURN]);
        if (--g->timer < 1) { patrol_anim(g, 0); g->state = 2; }
        break; }
    case 15: /* 086ff0: stop, clear report/damage bits, select tagged clip */
        g->speed=0;g->flags&=0xffddfffdu;g->cnt=0;
        memcpy(g->target,ppos,12);g->state=16;patrol_anim(g,17);g->hit=0;
        /* 0961b8 sound and 087e98 attack-volume contacts still need their
           original task bindings; do not synthesize damage here. */
        break;
    case 16: /* 0870a4: unsigned +98 counter, strict > parameter +5c */
        g->cnt=(unsigned)g->cnt+1;
        if((unsigned)g->cnt>(unsigned)par[23]){g->state=2;patrol_anim(g,0);}
        break;
    default: g->state = 2; patrol_anim(g, 0); break;
    }
    if (g->state != g->log_state) { printf("patrol%d: state %d->%d pos %.0f,%.0f,%.0f wp %d pd %.0f\n", g->kind, g->log_state, g->state, g->pos[0], g->pos[1], g->pos[2], g->wpi, pdist); g->log_state = g->state; }
    if (ground) g->pos[1] = ground(gctx, g->pos[0], g->pos[1], g->pos[2]);
    memcpy(g->m[0].pos, g->pos, 12); g->m[0].yaw = atan2f(g->dir[0], g->dir[2]); onishima_animation(g);
}

/* Type 21 is Taban, a dedicated 0x4f4-byte actor created by FUN_8c0f92b8;
   it is not the Onishima waypoint actor used by type 17.  The main-state
   dispatch below is 0x8c21b5b8 (FUN_8c0f99b0..FUN_8c0fa580).  The animation
   controller dispatch at 0x8c21b430 selects Taban's private semantic table
   0x8c1b65cc. Clip lengths come from tabandat, not the 17-frame gait. */
int gang_taban_motion(const Gang *g)
{return TABAN_MOTION[g->unit_state>=0 && g->unit_state<12 ? g->unit_state:5];}
int gang_taban_gait_motion(void) {return TABAN_GAIT_MOTION;}
static void taban_anim(Gang *g, int state)
{
    if (g->unit_state != state) { g->unit_state = state; g->m[0].frame = 0.f; }
}

static void taban_anim_tick(Gang *g)
{
    GangMember *m = &g->m[0];
    if(m->chief.old_state!=g->unit_state) {
        m->chief.old_state=g->unit_state;m->frame=0.f;
        return; /* 0f8818: initialize, then advance starting next tick */
    }
    if(g->unit_state==1) {
        float want=fminf(g->speed/PAR_TABAN[3],1.f);
        m->chief.gait+=fmaxf(-.1f,fminf(.1f,want-m->chief.gait));
    }
    if(g->unit_state==2 && g->speed==0) {taban_anim(g,0);return;}
    m->frame += 1.f;
    if (m->frame < TABAN_FRAMES[gang_taban_motion(g)]) return;
    /* FUN_8c0f8f5c, FUN_8c0f90d8, FUN_8c0f9194 and FUN_8c0f9250. */
    if (g->unit_state == 4) taban_anim(g, 0);
    else if (g->unit_state == 7) taban_anim(g, 8);
    else if (g->unit_state == 9) taban_anim(g, 0);
    else if (g->unit_state == 11) taban_anim(g, 1);
    else m->frame-=TABAN_FRAMES[gang_taban_motion(g)];
}

void gang_spawn_taban(Gang *g, const float *pos, float yaw, const GGraph *graph, int trig, const float *quad)
{
    memset(g, 0, sizeof *g); g->active = 1; g->kind = 21; g->type = 21; g->mode = 9; g->par = PAR_TABAN;
    g->trig = trig; if (quad) memcpy(g->quad, quad, 32); g->graph = graph;
    memcpy(g->pos, pos, 12); memcpy(g->lastnode, pos, 12); g->dir[0] = sinf(yaw); g->dir[2] = cosf(yaw);
    g->nmem = 1; memcpy(g->m[0].pos, pos, 12); g->m[0].yaw = yaw; g->m[0].motion = -1;
    g->state = 0; g->unit_state = 5; g->log_state = -1;g->m[0].chief.old_state=-1;
}

/* 0fa744's direct pursuit is distinct from the generic formation mover.
   Test the proposed position against the CURRENT player graph before
   committing it. The old generic wall probe turned by an arbitrary .5
   radians, and tested arrival against the actor's previous node. */
static float taban_follow(Gang *g,float player_speed)
{
    float delta[3]={g->target[0]-g->pos[0],0,g->target[2]-g->pos[2]};
    float distance=sqrtf(delta[0]*delta[0]+delta[2]*delta[2]);
    if(distance<g->par[11]) {
        decelerate(g);
        if(g->speed>fabsf(player_speed))g->speed=fmaxf(fabsf(player_speed),g->speed-g->par[7]);
    }
    if(distance<g->par[12])return 0;
    int yaw=(int)lrintf(atan2f(g->dir[0],g->dir[2])*10430.378350470453f);
    int angles[3]={0,yaw,0};heli_vector_angles(delta,angles);
    yaw=heli_turn(yaw,angles[1],(int)g->par[P_MAXTURN]);
    g->dir[0]=heli_sin(yaw);g->dir[2]=heli_cos(yaw);
    float next[3]={g->pos[0]+g->dir[0]*g->speed,g->pos[1]+g->vy,g->pos[2]+g->dir[2]*g->speed};
    if(g->state!=6) {
        int node=graph_node_at(g->graph,next);
        if(node<0)return -1.f;
        memcpy(g->lastnode,g->pos,12);g->node=node;
    }
    memcpy(g->pos,next,12);return distance-g->speed;
}

static void taban_floor(Gang *g,const float before[3],float (*ground)(void *,float,float,float),void *ctx)
{
    /* 0fac78: stationary grounded actors do not enqueue another floor
       query. Moving/falling actors use the short source floor sweep. */
    if(!g->floor_initialized) {
        if(ground)g->pos[1]=ground(ctx,g->pos[0],g->pos[1],g->pos[2]);
        g->floor_initialized=1;return;
    }
    if(!(g->flags&0x80) && g->speed==0 && g->vy==0 && !memcmp(before,g->pos,12))return;
    if(g->pos[1]==before[1])g->pos[1]+=g->vy;
    float y;
    if(g->combat && g->combat->police_floor) {
        if(g->combat->police_floor(g->combat->context,before,g->pos,g->flags,&y) && g->vy<=0) {
            g->pos[1]=y;g->vy=0;g->flags&=~0x80;
        } else {
            if(g->vy>0)g->vy=0;
            g->vy=fmaxf(-g->par[18],g->vy-g->par[17]);g->flags|=0x80;
        }
    } else if(ground)g->pos[1]=ground(ctx,g->pos[0],g->pos[1],g->pos[2]);
}

static void taban_tick(Gang *g, const Graphs *G, int area, const float *ppos, float player_speed, int player_off_graph,
                       float (*ground)(void *, float, float, float), void *gctx)
{
    const float *par = g->par;
    float before[3];memcpy(before,g->pos,12);
    float dx = ppos[0] - g->pos[0], dz = ppos[2] - g->pos[2], pdist = sqrtf(dx * dx + dz * dz);
    if (g->target_player) memcpy(g->target, ppos, 12);
    if ((g->flags & 0x10) && g->state != 11 && g->state != 12) { g->speed = 0.f; g->state = 11; }
    switch (g->state) {
    case 0:   /* FUN_8c0f99b0 */
        g->flags = 0x101; g->speed = 0.f; g->cnt = g->timer = 0; g->target_player = 1;
        memset(&g->taban,0,sizeof g->taban);
        taban_anim(g, 5); g->state = 1; break;
    case 1:   /* FUN_8c0f9a84: settle/visibility, then the authored trigger */
        g->flags &= ~0x100;
        if (gang_triggered(g, ppos)) { g->flags &= ~1; taban_anim(g, 4); g->state = 2; }
        break;
    case 2:   /* FUN_8c0f9b48: entry motion 4 returns itself to motion 0 */
        if (g->unit_state != 4) g->state = 3;
        break;
    case 3: { /* FUN_8c0f9b60: acquire the current graph and choose a route */
        g->timer = 0;
        if (!g->graph) g->graph = graphs_pick(G, area, ppos);
        if (player_off_graph || !g->graph) { decelerate(g); break; }
        g->node = graph_node_at(g->graph, g->pos);
        if (g->node < 0) {
            int n2 = graph_node_at(g->graph, g->lastnode);
            if (n2 < 0) decelerate(g);
            else { memcpy(g->target, g->graph->nodes[n2].pos, 12); g->target_player = 0; g->state = 6; }
        } else {
            memcpy(g->lastnode, g->graph->nodes[g->node].pos, 12); g->target_player = 1; memcpy(g->target, ppos, 12);
            g->state = plan_route(g, G) == 1 ? 4 : 5;
        }
        break; }
    case 4:   /* FUN_8c0f9c94: graph route, with the source 150-tick replan */
        taban_anim(g, 1); accelerate(g, par[P_MAXSPD]);
        if (g->timer++ > (int)par[P_CHASECHK] || !follow_target(g)) g->state = 3;
        if (++g->cnt > (int)par[P_IDLE]) { g->saved = g->state; g->state = 10; g->cnt = 0; g->flags |= 0x10000; }
        break;
    case 5: { /* FUN_8c0f9d10: direct chase and 50-unit report/attack gate */
        taban_anim(g, 1); accelerate(g, par[P_MAXSPD]);
        float distance=taban_follow(g,player_speed);
        if (distance<0 || g->node != graph_node_at(g->graph,ppos)) { g->flags&=~0x40;g->state = 3; break; }
        if (distance <= par[P_RANGE]) { g->flags |= 2; g->speed = 0.f; g->cnt = 0; taban_anim(g, 7); g->state = 7; }
        else if (++g->cnt > (int)par[P_IDLE]) { g->saved = 5; g->state = 10; g->cnt = 0; g->flags |= 0x10000; }
        break; }
    case 6:   /* FUN_8c0f9dcc: return from outside the graph */
        taban_anim(g, 1); accelerate(g, par[P_MAXSPD]); taban_follow(g,player_speed);
        g->node = graph_node_at(g->graph, g->pos);
        if (g->node >= 0) { memcpy(g->lastnode, g->graph->nodes[g->node].pos, 12); g->target_player = 1; g->state = 3; }
        break;
    case 7:   /* FUN_8c0f9e50: motion 7 completes into the reporting loop */
        steer(g->dir, (float[3]){dx, 0.f, dz}, (int)par[P_MAXTURN]);
        if (g->unit_state != 7) { g->state = 8; g->cnt = 0;
            if(g->combat && g->combat->sound)g->combat->sound(g->combat->context,0x150005,g->pos);
        }
        if(g->cnt++>15) {g->taban.extent+=2;if(g->taban.extent>19)g->taban.extent=19;}
        if(g->cnt==15 && g->combat && g->combat->sound)g->combat->sound(g->combat->context,0x150004,g->pos);
        break;
    case 8:   /* FUN_8c0f9f64: active report, at most 31 ticks in range */
        steer(g->dir, (float[3]){dx, 0.f, dz}, (int)par[P_MAXTURN]);
        g->flags |= 0x20000;
        if (!(g->flags & 2) || pdist > par[P_RANGE] || g->cnt++ > 30 || player_off_graph) {
            taban_anim(g, 9); g->timer = g->cnt = 0; g->state = 9;
        }
        break;
    case 9:   /* FUN_8c0fa084: recovery motion returns to route selection */
        if(g->taban.extent>0)g->taban.extent--;
        steer(g->dir, (float[3]){dx, 0.f, dz}, (int)par[P_MAXTURN]);
        if (g->unit_state != 9) { g->flags &= ~0x20003; g->timer = g->cnt = 0; g->state = 3; }
        break;
    case 10:  /* FUN_8c0fa0f8 / FUN_8c0faf64: source idle/regroup pause */
        taban_anim(g, 2); decelerate(g);
        if (++g->cnt >= (int)par[P_REGROUP]) { g->flags &= ~0x10000; g->cnt = 0; g->state = g->saved; }
        break;
    case 11:  /* FUN_8c0fa1b0 */
        taban_anim(g, 1); g->state = 12; break;
    case 12:  /* FUN_8c0fa230: leave under the source 0x10 control flag */
        accelerate(g, par[P_MAXSPD] * 2.f);
        if (!follow_target(g)) g->active = 0;
        break;
    default:
        /* States 13..15 are Taban's authored launch/airborne response and
           remain disabled until their collision/effect records are ported. */
        g->state = 3; taban_anim(g, 0); break;
    }
    taban_anim_tick(g);
    taban_floor(g,before,ground,gctx);
    memcpy(g->m[0].pos, g->pos, 12); g->m[0].yaw = atan2f(g->dir[0], g->dir[2]);
    /* 0fb21c runs after animation, before registering 0fac78 contacts. */
    if(g->state>=7 && g->state<=9 && g->combat && g->combat->taban_anchor) {
        float anchors[2][3];g->taban.ready=g->combat->taban_anchor(g->combat->context,g,anchors);
        if(g->taban.ready) {
            taban_attack_step(&g->taban,anchors);
            if(g->combat->contact) {
                float root[16];gang_member_matrix(g,g->m,root);int hit=0;unsigned valid=0;
                for(int i=1;i<g->taban.extent;i+=3) {
                    float point[3],previous[3];
                    for(int j=0;j<3;j++) {
                        const float *r=root+j*4;
                        point[j]=r[0]*g->taban.point[i][0]+r[1]*g->taban.point[i][1]+r[2]*g->taban.point[i][2]+r[3];
                        previous[j]=(g->taban.contact_valid&(1u<<(i/3)))?g->taban.contact_previous[i/3][j]:point[j];
                    }
                    float d=0;for(int j=0;j<3;j++){float x=point[j]-ppos[j];d+=x*x;}
                    if(d<50.f*50.f) {
                        hit|=g->combat->contact(g->combat->context,point,previous,3.f,27);
                        memcpy(g->taban.contact_previous[i/3],point,12);valid|=1u<<(i/3);
                    }
                }
                g->taban.contact_valid=valid;
                if(hit && g->combat->sound)g->combat->sound(g->combat->context,0x15000a,g->pos);
            }
            /* 0fb21c offers the fastest visible link to sound owner 10
               only when its speed/5 exceeds one. 0efb5c retains one cue. */
            if(g->combat->taban_sound) {
                float best=25.f;int link=0;
                for(int i=1;i<g->taban.extent;i++) {
                    const float *v=g->taban.velocity[i];float d=(v[0]*v[0]+v[1]*v[1])+v[2]*v[2];
                    if(d>best){best=d;link=i;}
                }
                if(link){float root[16],point[3];gang_member_matrix(g,g->m,root);
                    for(int j=0;j<3;j++){const float *r=root+j*4,*p=g->taban.point[link];point[j]=r[0]*p[0]+r[1]*p[1]+r[2]*p[2]+r[3];}
                    g->combat->taban_sound(g->combat->context,point);
                }
            }
        }
    } else {g->taban.ready=0;g->taban.contact_valid=0;}
    if (g->state != g->log_state) { printf("taban: state %d->%d pos %.0f,%.0f,%.0f speed %.3f anim %d pd %.0f\n", g->log_state, g->state, g->pos[0], g->pos[1], g->pos[2], g->speed, g->unit_state, pdist); g->log_state = g->state; }
}
/* FUN_8c072adc shared mover (types 11/12/14).  Its entries are 20-byte
   {x,y,z,speed,action} records, not generic actor transforms.  State 0
   (FUN_8c072b8c) places the actor at entry zero, points at entry one and
   latches both action words; state 1 (FUN_8c072cd8) waits for the record
   trigger and takes entry zero's speed; state 2 (FUN_8c072f30) follows the
   loop and changes speed/action at each entry.  Type 11's loader/table is
   PatCar (FUN_8c07f840, 0x8c1b620c), type 12 is Bike
   (FUN_8c083200, 0x8c1b62bc), and type 14 is Benz
   (FUN_8c0acb48, 0x8c1b6408). */
static void mover_set_unit_state(Gang *g, int state)
{
    if (g->unit_state != state) { g->unit_state = state; g->m[0].frame = 0.f; }
}
static void mover_apply_action(Gang *g)
{
    int a = g->action, s = g->unit_state;
    if (g->kind == 11) {   /* FUN_8c080c10, table 0x8c1b8bb4 */
        if (a >= 1 && a <= 10) mover_set_unit_state(g, (a & 1) ? 14 : 15);
        else if (a == 11) { if (s == 15) mover_set_unit_state(g, 12); else if (s == 14) mover_set_unit_state(g, 11); }
        else if (a == 12) { if (s == 3) mover_set_unit_state(g, 4); else if (s != 4) mover_set_unit_state(g, 3); }
        else if (a == 13 && !(s == 2 || (s >= 5 && s <= 10))) mover_set_unit_state(g, 2);
        else if (a == 14 && s != 14 && s != 15) mover_set_unit_state(g, 14);
        /* States 11/12 return to the default state when their source clip
           completes (FUN_8c08265c/FUN_8c082760). */
        if (g->unit_state == 11 && g->m[0].frame >= 33.f) mover_set_unit_state(g, 13);
        if (g->unit_state == 12 && g->m[0].frame >= 33.f) mover_set_unit_state(g, 13);
    } else if (g->kind == 12) {   /* FUN_8c083e9c, table 0x8c1b8ec0 */
        if (a >= 1 && a <= 10) mover_set_unit_state(g, (a & 1) ? 11 : 12);
        else if (a == 11) { if (s == 12) mover_set_unit_state(g, 9); else if (s == 11) mover_set_unit_state(g, 8); }
        else if (a == 12 && s != 2) mover_set_unit_state(g, 2);
        else if (a == 13 && s != 3) mover_set_unit_state(g, 3);
        else if (a == 14 && s != 14 && s != 15) mover_set_unit_state(g, 14);
        else if (a == 15 && s == 15) mover_set_unit_state(g, 16);
        /* FUN_8c0850e8/8c0851ec, 8c08563c and 8c085794. */
        if (g->unit_state == 8 && g->m[0].frame >= 35.f) mover_set_unit_state(g, 10);
        if (g->unit_state == 9 && g->m[0].frame >= 35.f) mover_set_unit_state(g, 10);
        if (g->unit_state == 14 && g->m[0].frame >= 15.f) mover_set_unit_state(g, 15);
        if (g->unit_state == 16 && g->m[0].frame >= 17.f) mover_set_unit_state(g, 10);
    } else if (g->kind == 14) {   /* FUN_8c0addf4, table 0x8c1c9ae4: Benz */
        if (a >= 1 && a <= 8 && s != 14 && s != 15) mover_set_unit_state(g, (a & 1) ? 14 : 15);
        else if ((a == 9 || a == 10) && !(s == 7 || s == 8 || s == 14 || s == 15)) mover_set_unit_state(g, (a & 1) ? 14 : 15);
        else if (a == 11) { if (s == 15) mover_set_unit_state(g, 12); else if (s == 14) mover_set_unit_state(g, 11); }
        else if (a == 12) { if (s == 3) mover_set_unit_state(g, 4); else if (s != 4) mover_set_unit_state(g, 3); }
        else if (a == 13 && !(s == 2 || (s >= 5 && s <= 10))) mover_set_unit_state(g, 2);
        else if (a == 14 && !(s == 2 || (s >= 5 && s <= 10))) mover_set_unit_state(g, 14);
        /* FUN_8c0abf6c/FUN_8c0ac070 return the paired clips to state 13.
           Actions 9/10 alone settle their turn clips into states 8/7.  Every
           raw Benz motion is 17 frames. */
        if (g->unit_state == 11 && g->m[0].frame >= 17.f) mover_set_unit_state(g, 13);
        if (g->unit_state == 12 && g->m[0].frame >= 17.f) mover_set_unit_state(g, 13);
        if (g->unit_state == 14 && a == 9 && g->m[0].frame >= 17.f) mover_set_unit_state(g, 8);
        if (g->unit_state == 15 && a == 10 && g->m[0].frame >= 17.f) mover_set_unit_state(g, 7);
    }
}
void gang_spawn_mover(Gang *g, int kind, const float (*wp)[3], const float *speed, const int *action, int nwp, int trig, const float *quad)
{
    memset(g, 0, sizeof *g); g->active = 1; g->kind = kind; g->mode = 3; g->trig = trig; if (quad) memcpy(g->quad, quad, 32);
    g->nwp = nwp > GANG_MAX_WAYPOINTS ? GANG_MAX_WAYPOINTS : nwp;
    for (int i = 0; i < g->nwp; i++) { memcpy(g->wp[i], wp[i], 12); g->wp_speed[i] = speed[i]; g->wp_action[i] = action[i]; }
    memcpy(g->pos, g->wp[0], 12); g->nmem = 1; memcpy(g->m[0].pos, g->pos, 12); g->m[0].motion = -1;
    g->wpi = g->nwp > 1 ? 1 : 0; memcpy(g->target, g->wp[g->wpi], 12);
    { float dx = g->target[0] - g->pos[0], dz = g->target[2] - g->pos[2], d = sqrtf(dx * dx + dz * dz); if (d > 0.0001f) { g->dir[0] = dx / d; g->dir[2] = dz / d; } else g->dir[2] = 1.f; }
    /* 070918 initializes the owner to 0x101; 072b8c ORs in 0x420000.
       Replacing those flags revealed every waiting route before its trigger. */
    g->action = g->wp_action[0]; g->next_action = g->wp_action[g->wpi]; g->flags = 0x420101; g->state = 1;
    /* Both source unit initializers execute state zero once and enter one. */
    g->unit_state = 1; g->log_state = -1;
}
static void mover_tick(Gang *g, const float *ppos, int police_attack, float (*ground)(void *, float, float, float), void *gctx)
{
    const float ACC = 0.062f, DEC = 0.154f; const int TURN = 2730;
    switch (g->state) {
    case 1:   /* wait at entry zero; FUN_8c072cd8 */
        if (gang_triggered(g, ppos)) {
            g->speed = g->wp_speed[0]; g->flags &= ~0x101; g->state = 2;
            mover_set_unit_state(g, g->kind == 12 ? 10 : (g->kind == 11 || g->kind == 14) ? 13 : g->unit_state);
        }
        break;
    case 2: { /* FUN_8c072f30 / FUN_8c072d70 */
        float delta[3] = { g->target[0] - g->pos[0], 0.f, g->target[2] - g->pos[2] };
        float d2 = delta[0] * delta[0] + delta[2] * delta[2], wanted = g->wp_speed[g->wpi];
        steer(g->dir, delta, TURN);
        if (g->speed < wanted) { g->speed += ACC; if (g->speed > wanted) g->speed = wanted; }
        else if (g->speed > wanted) { g->speed -= DEC; if (g->speed < wanted) g->speed = wanted; }
        g->pos[0] += g->dir[0] * g->speed; g->pos[2] += g->dir[2] * g->speed;
        if (d2 < 10.f * 10.f) {
            memcpy(g->pos, g->target, 12); g->action = g->wp_action[g->wpi]; g->flags |= 0x20000;
            g->wpi = (g->wpi + 1) % g->nwp; memcpy(g->target, g->wp[g->wpi], 12); g->next_action = g->wp_action[g->wpi];
        }
        break; }
    default: break;
    }
    mover_apply_action(g);
    if (g->state != g->log_state) { printf("mover%d: state %d->%d pos %.0f,%.0f,%.0f wp %d speed %.3f action %d/%d unit %d\n", g->kind, g->log_state, g->state, g->pos[0], g->pos[1], g->pos[2], g->wpi, g->speed, g->action, g->next_action, g->unit_state); g->log_state = g->state; }
    /* The untriggered owner retains its authored placement. Bike's 083964
       and 083c94 gate movement/contact work on owner flag 1. */
    if (ground && !(g->flags&1)) g->pos[1] = ground(gctx, g->pos[0], g->pos[1], g->pos[2]);
    memcpy(g->m[0].pos, g->pos, 12); g->m[0].yaw = atan2f(g->dir[0], g->dir[2]);
    if(!(g->flags&0x120))g->m[0].frame += 1.f; /* 084664: hidden units do not advance their clip */
    (void)police_attack;
}

/* Types 13/15/32 are the two helicopter banks created by FUN_8c0a56e4,
   not ground skaters.  Their task FUN_8c0a5da4 dispatches a 20-state table
   at 0x8c1c9540.  These are its source route states 0..3:

     0 FUN_8c0a6af4  place at entry zero, target entry one, direction +1
     1 FUN_8c0a6cdc  wait for the record trigger (type 32 starts immediately)
     2 FUN_8c0a6d34  process flags and select the next 0x18-byte entry
     3 FUN_8c0a6dc8  fly via FUN_8c0a7fd4 until within 20 units

   States 12/13 (FUN_8c0a7794/FUN_8c0a7848) stop and track at a stationary
   route point. States 6/7 launch paired missiles; 8..10 perform the low
   pass; 11 continues the route. States 4/5 and the
   crash path 14..18 now use the recovered helpers below. This preserves the
   real 3D waypoints, per-point speed/route fields and flags instead of
   feeding the actor to the generic ground patrol and snapping its Y
   coordinate to collision. */
#define HELI_ACC 0.076851852f     /* parameter blocks 0x8c1c9480/0x8c1c94bc +0 */
#define HELI_DEC 0.386111110f     /* +4 */
#define HELI_ARRIVE 20.f          /* +0x14 */
#define HELI_TURN 0x0aaa          /* +0x24, BAM per tick */

#include "heli_flight.inc"

static void heli_entry_flags(Gang *g, int entry, int initial)
{
    int f = g->wp_action[entry];
    static const int source_bit[3] = { 2, 4, 0x20 };
    static const int actor_bit[3] = { 0x100000, 0x40000, 0x80000 };
    for (int i = 0; i < 3; i++) if (f & source_bit[i]) {
        int enable = initial || (((f & 1) == 0) == (g->burst_left > 0));
        if (enable) g->flags |= actor_bit[i]; else g->flags &= ~actor_bit[i];
    }
}

static void heli_set_direction(Gang *g, int snap)
{
    float v[3];for(int k=0;k<3;k++)v[k]=g->target[k]-g->pos[k];
    if(snap)heli_vector_angles(v,g->heli_angles);
    else heli_heading(g,v,sqrtf((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]));
    heli_forward(g);
}

void gang_spawn_heli(Gang *g, int kind, const float (*wp)[3], const float *speed, const float *route_attr, const int *flags, int nwp, int trig, const float *quad)
{
    memset(g, 0, sizeof *g); g->active = 1; g->kind = kind; g->type = kind; g->mode = 4; g->trig = trig; if (quad) memcpy(g->quad, quad, 32);
    g->nwp = nwp > GANG_MAX_WAYPOINTS ? GANG_MAX_WAYPOINTS : nwp;
    if(g->nwp<=0){g->active=0;g->nwp=0;return;}
    for (int i = 0; i < g->nwp; i++) { memcpy(g->wp[i], wp[i], 12); g->wp_speed[i] = speed[i]; g->wp_aux[i] = route_attr[i]; g->wp_action[i] = flags[i]; }
    memcpy(g->pos, g->wp[0], 12); g->speed = g->wp_speed[0]; g->nmem = 1; memcpy(g->m[0].pos, g->pos, 12); g->m[0].motion = -1;
    g->burst_left = 1;   /* source task +0xa0 is the signed traversal direction */
    g->wpi = g->nwp > 1 ? 1 : 0; memcpy(g->target, g->wp[g->wpi], 12); g->action = g->wp_action[0]; g->next_action = g->wp_action[g->wpi];
    g->flags = 0x101; heli_entry_flags(g, 0, 1); heli_set_direction(g, 1); heli_set_brake_distance(g);
    memcpy(g->heli_candidate,g->pos,12);g->heli_yaw_old=g->heli_angles[1];
    g->wreck.variant=kind!=13;g->wreck.previous_anim=255;
    g->wreck.motion_frames[0]=80;g->wreck.motion_frames[1]=60;
    g->wreck_events.explosion=-1;
    g->state = 1; g->unit_state = 0; g->log_state = -1;
}

static void heli_advance(Gang *g,const float *player)
{
    int reached = g->wpi, next = reached, found = 0;
    g->action = g->wp_action[reached];
    if(!(g->flags&0x100011))heli_choose_route(g,player);
    /* FUN_8c0a8390 wraps and ignores control-only entries whose flags contain
       0x08/0x10.  FUN_8c0a81c8 can reverse this signed direction in response
       to the source player/route-distance test; its initial value is +1. */
    for (int tries = 0; !(g->flags&0x2000000) && tries < g->nwp; tries++) {
        next += g->burst_left;
        if (next < 0) next = g->nwp - 1; else if (next >= g->nwp) next = 0;
        if (!(g->wp_action[next] & 0x18)) { found = next != reached; break; }
    }
    if (!found)g->flags|=0x2000000;
    heli_entry_flags(g,reached,0);
    g->wpi = next; memcpy(g->target, g->wp[next], 12); g->next_action = g->wp_action[next]; heli_set_brake_distance(g);
}

static float heli_player_distance(const Gang *g,const float *p)
{float x=p[0]-g->pos[0],y=p[1]-g->pos[1],z=p[2]-g->pos[2];return sqrtf((x*x+y*y)+z*z);}
static int heli_in_front(const Gang *g,const float *p)
{return (p[0]-g->pos[0])*g->dir[0]+(p[1]-g->pos[1])*g->dir[1]+(p[2]-g->pos[2])*g->dir[2]>0.f;}
static int heli_missile_ready(const Gang *g,const float *p)
{
    if(!g->combat || g->combat->police_attack || !(g->flags&0x80000) ||
       (g->flags&0x402000) || g->heli_missile_cooldown)return 0;
    return g->kind==32 ? gang_triggered((Gang *)g,p) && p[1]>120.f:heli_player_distance(g,p)<=300.f;
}
static int heli_face_player(Gang *g,const float *p)
{
    float v[3];int a[3]={g->heli_angles[0],g->heli_angles[1],g->heli_angles[2]};
    for(int k=0;k<3;k++)v[k]=p[k]-g->pos[k];heli_vector_angles(v,a);
    g->heli_angles[1]=heli_turn(g->heli_angles[1],a[1],182);return g->heli_angles[1]==a[1];
}
static void heli_gun_tick(Gang *g,const float *ppos)
{
    GangCombat *c=g->combat;if(!c || !c->shot || !c->anchor || g->state==9 || g->state>=16)return;
    int ready=!c->police_attack && (g->flags&0x40000) && !(g->flags&0x2000) &&
        heli_player_distance(g,ppos)<=100.f && heli_in_front(g,ppos);
    if(!(g->flags&0x400000)) {
        if(ready){g->flags|=0x400000;g->fire_state=0;}else return;
    }
    EnemyGun *s=&g->m[0].gun;const EnemyGunParams par={120,5,3,45};
    float from[3];if(!c->anchor(c->context,g,8,from))return;
    if(g->fire_state==0) {
        uint32_t flags=g->flags;enemy_gun_begin(s,&flags,&par);s->first=0;g->flags=flags;g->fire_state=1;
        if(c->sound)c->sound(c->context,g->kind==13?0xd0000:0xf0000,g->pos);
    } else if(g->fire_state==1) {
        if(c->police_attack){s->delay=120;g->fire_state=2;return;}
        uint32_t flags=g->flags,old=flags;
        int result=c->shot_result?c->shot_result(c->context,s->probe):-1;
        EnemyGunEvents e=enemy_gun_tick(s,&flags,&par,result>0,c->vulnerable);
        g->flags=e.finished?old:flags;
        if(e.query)s->probe=c->shot(c->context,from,c->target,g->kind,2);
        if(e.warning)c->warning=1;
        if(e.mode>=0)c->shot(c->context,from,c->target,g->kind,e.mode);
        if(e.finished){s->delay=120;g->fire_state=2;}
    } else {
        if(!ready)g->flags&=~0x400000;
        else if(--s->delay<=0){s->left=5;s->first=0;g->flags&=~0x60000000;g->fire_state=1;}
    }
}

static void heli_tick(Gang *g, const float *ppos)
{
    g->wreck_events=(HeliDestructionEvents){-1,0,0};
    if(g->flags&0x20)return;
    int old_state=g->state;
    switch (g->state) {
    case 1:   /* FUN_8c0a6cdc */
        if (g->kind == 32 || gang_triggered(g, ppos)) { g->flags &= ~0x101; g->state = 3; }
        break;
    case 2:   /* FUN_8c0a6d34 */
        g->cnt=0;
        if(g->flags&0x2000){heli_choose_crash(g);if(heli_advance_crash(g))g->state=14;}
        else {heli_advance(g,ppos);g->state=(g->flags&0x2000000)?12:3;}
        g->heli_yaw_old=g->heli_angles[1];g->heli_bank=0;break;
    case 3: /* 0a6dc8: waypoint missile attack, strafing run, or turn. */
        if(heli_move(g,0))g->state=heli_missile_ready(g,ppos)?6:2;
        else if(g->combat) {
            if((g->flags&0x100000) && heli_player_distance(g,ppos)<=300.f && heli_in_front(g,ppos)) {
                g->state=8;g->unit_state=1;g->m[0].frame=-1.f;
            } else if(abs((int16_t)(g->heli_yaw_target-g->heli_angles[1]))>0x5555)g->state=4;
        }break;
    case 4: /* 0a6ec4: brake and level out before turning */
        g->speed-=HELI_DEC;if(g->speed<=0.f){g->speed=0.f;g->state=5;}
        heli_integrate(g);
        g->heli_angles[0]=heli_turn(g->heli_angles[0],0,182);
        g->heli_angles[2]=heli_turn(g->heli_angles[2],0,182);break;
    case 5: /* 0a6f54: turn in place, then resume the appropriate route */
        g->heli_angles[0]=heli_turn(g->heli_angles[0],0,182);
        g->heli_angles[1]=heli_turn(g->heli_angles[1],g->heli_yaw_target,182);
        g->heli_angles[2]=heli_turn(g->heli_angles[2],0,182);
        if(g->heli_angles[1]==g->heli_yaw_target) {
            g->state=(g->flags&0x2000) ? 14:3;
            g->heli_yaw_old=g->heli_angles[1];g->heli_bank=0;
        }break;
    case 6: /* 0a7020: stop before aiming the paired launchers. */
        if(g->speed<=0.f) {
            if(heli_face_player(g,ppos)){g->state=7;g->heli_missile_index=0;g->heli_missile_left=g->kind==13?3:1;g->heli_missile_delay=0;}
        } else {
            g->speed-=HELI_DEC;if(g->speed<0)g->speed=0;heli_integrate(g);
            g->heli_angles[0]=heli_turn(g->heli_angles[0],0,182);
        }break;
    case 7: /* 0a70e8: original pairs, launcher order and cooldowns. */
        heli_face_player(g,ppos);g->heli_angles[0]=heli_turn(g->heli_angles[0],0,182);
        if(!heli_missile_ready(g,ppos)){g->heli_missile_cooldown=g->kind==13?300:150;g->state=2;}
        else if(--g->heli_missile_delay<1) {
            GangCombat *c=g->combat;
            if(c && c->anchor && c->missile)for(int j=0;j<2;j++) {
                float from[3],to[3];
                for(int k=0;k<3;k++)to[k]=ppos[k]+c->velocity[k]*(j+1);
                if(c->anchor(c->context,g,g->heli_missile_index*2+j,from))c->missile(c->context,from,to,g->kind!=13);
            }
            if(c && c->sound)c->sound(c->context,g->kind==13?0xd0001:0xf0001,g->pos);
            if(--g->heli_missile_left<1){g->heli_missile_cooldown=g->kind==13?300:150;g->state=2;}
            else {g->heli_missile_index=(g->heli_missile_index+1)%4;g->heli_missile_delay=g->kind==13?60:150;}
        }break;
    case 8:case 9: /* 0a72fc/735c */
        if(heli_lowpass(g))heli_advance(g,ppos);
        g->heli_angles[0]=heli_turn(g->heli_angles[0],0,182);
        if(g->state==8 && g->unit_state!=1)g->state=9;
        else if(g->state==9 && !(g->flags&0x100000)){g->state=10;g->unit_state=3;g->m[0].frame=-1.f;}
        break;
    case 10: /* 0a73b8 */
        heli_integrate(g);g->heli_angles[0]=heli_turn(g->heli_angles[0],0,182);
        if(g->unit_state!=3)g->state=2;break;
    case 11: /* 0a7770 */
        if(heli_move(g,0))heli_advance(g,ppos);break;
    case 12:  /* FUN_8c0a7794: brake to a hover, still advancing this tick */
        g->speed -= HELI_DEC;
        if (g->speed < 0.f) g->speed = 0.f;
        heli_integrate(g);
        if (g->speed == 0.f) { memcpy(g->target, ppos, 12); g->cnt = 0; g->state = 13; }
        g->heli_angles[0]=heli_turn(g->heli_angles[0],0,182);
        g->heli_angles[2]=heli_turn(g->heli_angles[2],0,182);
        break;
    case 13:  /* FUN_8c0a7848: face the tracked object for 30 ticks */
        memcpy(g->target, ppos, 12);heli_face_player(g,ppos);
        g->heli_angles[0]=heli_turn(g->heli_angles[0],0,182);
        g->heli_angles[2]=heli_turn(g->heli_angles[2],0,182);
        if(heli_missile_ready(g,ppos))g->state=6;
        else if (++g->cnt > 30) { g->flags &= ~0x2000000; g->state = 2; }
        break;
    case 14: /* 0a73f8 */
        if(heli_move(g,0))heli_advance_crash(g);
        if(abs((int16_t)(g->heli_yaw_target-g->heli_angles[1]))>0x5555)g->state=4;
        else if(g->flags&0x800000)g->state=15;
        break;
    case 15: /* 0a74a8 */
        if(heli_move(g,1)) {
            g->state=16;g->flags=(g->flags|0x1000000)&~0x280;g->speed=0.f;
            g->wreck.state=16;g->wreck.anim=4;
        }break;
    case 16:case 17:case 18:break; /* main+animation run together below */
    default: break; /* Never silently reset an unrecovered state to flight. */
    }
    if(g->state>=16 && g->state<=18) {
        int landed=old_state==16 && g->wreck.clock[0]>=56.f ? heli_landing(g):0;
        g->wreck.state=g->state;g->wreck.flags=g->flags;
        g->wreck.parent_flags=g->parent_flags ? *g->parent_flags:0;
        /* On the 15->16 tick only animation executes; clock is still zero,
           so the combined helper cannot dispatch landing early. */
        g->wreck_events=heli_destruction_tick(&g->wreck,landed);
        g->state=g->wreck.state;
        if(g->parent_flags)*g->parent_flags=g->wreck.parent_flags;
    }
    /* 0a5da4: GF completion is sampled AFTER main + animation. */
    if(!(g->flags&0x100) && g->state>=2 && g->state<=15 && g->graffiti && g->graffiti->complete)g->flags|=0x2000;
    if (g->state != g->log_state) { printf("heli%d: state %d->%d pos %.0f,%.0f,%.0f wp %d speed %.3f flags %x/%x attr %.3f\n", g->kind, g->log_state, g->state, g->pos[0], g->pos[1], g->pos[2], g->wpi, g->speed, g->action, g->next_action, g->wp_aux[g->wpi]); g->log_state = g->state; }
    memcpy(g->m[0].pos,g->pos,12);
    g->m[0].pitch=g->heli_angles[0]*(6.2831853f/65536.f);
    g->m[0].yaw=g->heli_angles[1]*(6.2831853f/65536.f);
    g->m[0].roll=g->heli_angles[2]*(6.2831853f/65536.f);
    /* 0a5da4 registers 1c92ec only during state 9. Its root sphere
       (1c92dc) is (0,30,-20), radius 30, contact attribute 30. */
    if(g->state==9 && !(g->flags&0x1000100) && g->combat && g->combat->contact) {
        float root[16],point[3];gang_member_matrix(g,g->m,root);
        for(int r=0;r<3;r++)point[r]=root[r*4+1]*30.f-root[r*4+2]*20.f+root[r*4+3];
        g->combat->contact(g->combat->context,point,g->heli_body_valid?g->heli_body_previous:point,30.f,30);
        memcpy(g->heli_body_previous,point,12);g->heli_body_valid=1;
    } else g->heli_body_valid=0;
    if(g->state<16) {
        g->m[0].frame+=1.f;
        if(g->unit_state==1 && g->m[0].frame>=29.f){g->unit_state=2;g->m[0].frame=-1.f;}
        else if(g->unit_state==3 && g->m[0].frame>=20.f){g->unit_state=0;g->m[0].frame=-1.f;}
        heli_gun_tick(g,ppos);
        if(g->heli_missile_cooldown>0)g->heli_missile_cooldown--;
    }
}

/* Types 8/24 share creator FUN_8c0997b4, member task FUN_8c099cd8 and
   leader task FUN_8c0970b4.  Their entries are the same compact 0x18-byte
   3D route shape used by the source controller, with the member count in
   the high half of entry zero's +0x14 word.  FUN_8c0970b4 transforms one
   vector from 0x8c1c7610..0x8c1c76b8 for each member. */

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_gang_c__route_form_0.inc"
#else

#include "generated/source_data/gang_c__ROUTE_FORM_0.inc"

#endif

#define ROCKET_ACC     0.154629633f   /* leader block 0x8c1c7714/777c +0 */
#define ROCKET_DEC     0.771296322f   /* +4 */
#define ROCKET_ARRIVE 20.f            /* +0x14; FUN_8c0973d4 and FUN_8c098148 */
#define ROCKET_TURN   0x0c16          /* +0x18, BAM per tick */

static void rocket_member_anim(GangMember *m, int anim)
{
    if (m->anim != anim) { m->anim = anim; m->frame = 0.f; }
}

static void rocket_member_secondary_anim(GangMember *m, int anim)
{
    if (m->secondary_anim != anim) { m->secondary_anim = anim; m->secondary_frame = 0.f; }
}

/* FUN_8c098c9c dispatches the two animation bytes through 0x8c1c75b0.
   Only source handlers which explicitly finish a clip are represented here;
   looping clips retain their state.  Models 28 and 46 have the same exact
   21-motion frame counts in chars.jgc. */
static void rocket_member_animation_tick(GangMember *m)
{
    int secondary_only = m->secondary_anim >= 14 && m->secondary_anim <= 16;
    if (!secondary_only) {
        if (m->anim == 2 && m->frame >= 4.f) rocket_member_anim(m, 1);          /* FUN_8c098fa0 */
        else if (m->anim == 3 && m->frame >= 19.f) rocket_member_anim(m, 1);    /* FUN_8c099018 */
        else if (m->anim == 4 && m->frame >= 16.f) rocket_member_anim(m, 10);   /* FUN_8c0990c0 */
        else if (m->anim == 5 && m->frame >= 13.f) rocket_member_anim(m, 9);    /* FUN_8c09916c */
        else if (m->anim == 6 && m->frame >= 13.f) rocket_member_anim(m, 1);    /* FUN_8c0991e4 */
        else if (m->anim == 7 && m->frame >= 43.f) rocket_member_anim(m, 1);    /* FUN_8c099270 */
    }
    if (m->secondary_anim == 16 && m->secondary_frame >= 11.f)
        rocket_member_secondary_anim(m, 9);                                    /* FUN_8c0995d0 */
    else if (m->secondary_anim == 19 && m->secondary_frame >= 11.f)
        rocket_member_secondary_anim(m, 1);                                    /* FUN_8c09973c */
}

/* Primary member dispatcher 0x8c1c783c: FUN_8c09a09c..FUN_8c09a468.
   Projectile creation belongs to the overlay dispatcher below, not these
   states.  State 4's left/right selection is the exact yaw convergence test
   in FUN_8c09aa84. */
static void rocket_member_state_tick(Gang *g, GangMember *m, const float target[3])
{
    switch (m->state) {
    case 0:   /* FUN_8c09a09c */
        m->state = 1; m->overlay = 0; rocket_member_secondary_anim(m, 13);
        memcpy(m->pos,target,12);memcpy(m->solo.pending,target,12);
        memcpy(m->solo.angles,g->heli_angles,sizeof m->solo.angles);m->solo.target=m->solo.angles[1];
        break;
    case 1:   /* FUN_8c09a138 */
        if (!(m->flags & 1)) { m->flags &= ~0x100; m->state = 2; }
        break;
    case 2: { /* FUN_8c09a164 */
        m->flags &= ~0x10000;
        int flags = m->flags;
        if (flags & 0x10) m->state = 14;
        else if (flags & 0x20000) m->state = 14;
        else if (flags & 0x40000) m->state = 5;
        else if (flags & 0x80000) m->state = 14;
        break; }
    case 3: { /* FUN_8c09a1d0 */
        m->flags &= ~0x20000;
        int flags = m->flags;
        if (flags & 0x10) m->state = 7;
        else if (flags & 0x10000) m->state = 12;
        else if (flags & 0x80000) m->state = 8;
        break; }
    case 4: { /* FUN_8c09a260 / FUN_8c09aa84 */
        m->flags &= ~0x80000;
        if (m->flags & 0x30000) m->state = 10;
        else if (m->flags & 0x2000) rocket_member_anim(m, 12);
        else {
            m->solo.angles[1]=heli_turn(m->solo.angles[1],g->heli_angles[1],ROCKET_TURN);
            m->yaw=solo_rad(m->solo.angles[1]);
            rocket_member_anim(m,m->solo.angles[1]==g->heli_angles[1]?10:11);
        }
        break; }
    case 5:   /* FUN_8c09a2d0 */
        m->state = 6; rocket_member_anim(m, 7); break;
    case 6:   /* FUN_8c09a2e8 */
        if (m->anim != 7) { m->flags &= ~0x40001; m->state = 2; m->yaw = atan2f(g->dir[0], g->dir[2]); }
        break;
    case 7:   /* FUN_8c09a330 */
        if (m->anim == 9) rocket_member_anim(m, 8);
        break;
    case 8:   /* FUN_8c09a34c */
        if (m->anim == 9) rocket_member_anim(m, 0);
        else if (m->anim == 0 && !(g->flags & 0x80)) { rocket_member_anim(m, 4); m->state = 9; }
        break;
    case 9:   /* FUN_8c09a39c */
        if (m->anim != 4) m->state = 4;
        break;
    case 10:  /* FUN_8c09a3b8 */
        if (m->anim == 10 || m->anim == 11) { rocket_member_anim(m, 3); m->state = 11; }
        break;
    case 11:  /* FUN_8c09a3e0 */
        if (m->anim != 3) { rocket_member_anim(m, 1); m->state = 2; }
        break;
    case 12:  /* FUN_8c09a404 */
        if (m->anim == 9) { rocket_member_anim(m, 6); m->state = 13; }
        break;
    case 13:  /* FUN_8c09a428 */
        if (m->anim != 6) m->state = 2;
        break;
    case 14:  /* FUN_8c09a444 */
        if (m->anim == 1) { rocket_member_anim(m, 5); m->state = 15; }
        break;
    case 15:  /* FUN_8c09a468 */
        if (m->anim != 5) m->state = 3;
        break;
    default:
        m->state = 0; break;
    }
}

/* Original 09aae4/09ab40 burst adapter. The shared task owns the
   delayed world probes; a member retains only a generation-safe token. */
static EnemyGunParams rocket_gun_params(const Gang *g)
{return (EnemyGunParams){60,g->kind==8?5:10,g->kind==8?6:0,g->kind==8?45:30};}
static void rocket_fire(Gang *g,GangMember *m)
{
    GangCombat *c=g->combat;if(!c || !c->shot)return;
    EnemyGunParams p=rocket_gun_params(g);uint32_t flags=(uint32_t)m->flags;
    int result=c->shot_result?c->shot_result(c->context,m->gun.probe):-1;
    EnemyGunEvents e=enemy_gun_tick(&m->gun,&flags,&p,result>0,c->vulnerable);
    m->flags=(int)flags;
    float from[3]={m->pos[0],m->pos[1]+10.f,m->pos[2]};
    if(e.query)m->gun.probe=c->shot(c->context,from,c->target,g->kind,2);
    if(e.warning)c->warning=1;
    if(e.mode>=0)c->shot(c->context,from,c->target,g->kind,e.mode);
    if(e.first_shot) {
        rocket_member_secondary_anim(m,m->secondary_anim==14?15:18);
    }
    if(!m->gun.first && !e.finished && c->gun_sound)m->gun.audio_token=c->gun_sound(c->context,m->gun.audio_token,0x80004,from);
}
/* Overlay dispatcher 1c787c, including the original projectile call. */
static void rocket_member_overlay_tick(Gang *g, GangMember *m)
{
    switch (m->overlay) {
    case 0:   /* FUN_8c09a484 */
        if (m->flags & 0x800000) m->overlay = 1;
        else if (m->flags & 2) {
            if (m->anim == 9) m->overlay = 2;
            else if (m->anim == 1) m->overlay = 5;
        }
        break;
    case 1:   /* FUN_8c09a4d0 */
        if (!(g->flags & 0x800000)) { m->flags &= ~0x800000; m->overlay = 0; }
        break;
    case 2:   /* FUN_8c09a510 */
        m->overlay = 3; rocket_member_secondary_anim(m, 14);
        {EnemyGunParams p=rocket_gun_params(g);uint32_t f=m->flags;enemy_gun_begin(&m->gun,&f,&p);m->flags=f;}break;
    case 3:   /* 09a530 */
        rocket_fire(g,m);
        if (!(m->flags & 2)) { m->overlay = 4; rocket_member_secondary_anim(m, 16); }
        break;
    case 4:   /* FUN_8c09a570 */
        if (m->secondary_anim != 16) { m->overlay = 0; rocket_member_secondary_anim(m, 13); }
        break;
    case 5:   /* FUN_8c09a5b8 */
        m->overlay = 6; rocket_member_secondary_anim(m, 17);
        {EnemyGunParams p=rocket_gun_params(g);uint32_t f=m->flags;enemy_gun_begin(&m->gun,&f,&p);m->flags=f;}break;
    case 6:   /* 09a5d8 */
        rocket_fire(g,m);
        if (!(m->flags & 2)) { m->overlay = 7; rocket_member_secondary_anim(m, 19); }
        break;
    case 7:   /* FUN_8c09a618 */
        if (m->secondary_anim != 19) { m->overlay = 0; rocket_member_secondary_anim(m, 13); }
        break;
    default:
        m->overlay = 0; rocket_member_secondary_anim(m, 13); break;
    }
}

/* Member +cc is consumed before the next formation target, 09a660/09a7c4.
   The source's distance branch measures its normalized forward vector, not
   the target delta; keep that behavior and the one-tick position delay. */
static void rocket_member_move(Gang *g,GangMember *m,const float target[3])
{
    if((m->state!=4 && m->state!=9) || !m->solo.terrain.hit)memcpy(m->pos,m->solo.pending,12);
    if(m->state==2) {
        int d=(int16_t)(m->solo.angles[1]-m->solo.target);
        int roll=(int)((float)d*((4.629629611968994f-m->speed)/4.629629611968994f)*12.860282897949219f);
        if(roll>=16384)roll=16384;
        m->turn_step=heli_turn(m->turn_step,roll,0x4fa);m->roll=solo_rad(m->turn_step);
    }
    if(m->state==4)return;
    float v[3];for(int k=0;k<3;k++)v[k]=target[k]-m->pos[k];
    if(v[0]==0 && v[1]==0 && v[2]==0){m->speed=0;return;}
    m->solo.target=m->solo.angles[1];int a[3];memcpy(a,m->solo.angles,sizeof a);heli_vector_angles(v,a);
    int cap=(m->state==12 || m->state==14)?0x8000:ROCKET_TURN;
    for(int k=0;k<2;k++)m->solo.angles[k]=heli_turn(m->solo.angles[k],a[k],cap);
    m->solo.angles[2]=0;
    if(m->state==2){m->pitch=solo_rad(m->solo.angles[0]);m->yaw=solo_rad(m->solo.angles[1]);}
    else if(m->state!=6){m->pitch=0;m->yaw=solo_rad(m->solo.angles[1]);}
    m->flags|=0x40;memcpy(m->solo.pending,target,12);m->speed=g->speed;
}
static void rocket_place_members(Gang *g)
{
    /* 0970b4: formation uses T * Y(yaw+PI) * X(pitch) * Z(roll). */
    float sy=heli_sin(g->heli_angles[1]+32768),cy=heli_cos(g->heli_angles[1]+32768);
    float sx=heli_sin(g->heli_angles[0]),cx=heli_cos(g->heli_angles[0]);
    float sz=heli_sin(g->heli_angles[2]),cz=heli_cos(g->heli_angles[2]);
    for(int i=0;i<g->nmem;i++) {
        const float *off=ROUTE_FORM[g->nmem-1][i];GangMember *m=g->m+i;memcpy(m->off,off,12);
        float x=cz*off[0]-sz*off[1],y=sz*off[0]+cz*off[1];
        float z=sx*y+cx*off[2];y=cx*y-sx*off[2];
        float target[3]={g->pos[0]+cy*x+sy*z,g->pos[1]+y,g->pos[2]-sy*x+cy*z};
        rocket_member_move(g,m,target);
        rocket_member_state_tick(g,m,target);rocket_member_overlay_tick(g,m);
        rocket_member_animation_tick(m);
        if(!(m->flags&0x120)) {
            if(!(m->secondary_anim>=14 && m->secondary_anim<=16))m->frame+=1.f;
            if(m->secondary_anim!=13)m->secondary_frame+=1.f;
        }
        /* The source only blocks member movement on landing/ground contact. */
        m->solo.terrain.hit=0;
        if((m->state==4 || m->state==9) && gang_heli_probe)
            m->solo.terrain.hit=gang_heli_probe(g_wall_ctx,m->pos,m->solo.pending,99,m->solo.terrain.point);
    }
    if(g->nmem)g->unit_state=g->m[0].anim;
}

/* 0741c8's root attack-manager sequence. Member-ready animation
   states are the standing and flight loops (09948c/0996c8). */
static void rocket_attack_orders(Gang *g,const float *ppos)
{
    GangCombat *c=g->combat;if(!c)return;
    (void)ppos; /* Attack-enable bit 2 belongs to leader states 5/8. */
    switch(g->fire_state) {
    case 0:g->fire_state=1;g->fire_cursor=0;break;
    case 1:
        if(g->fire_clock>=30 && (g->flags&2)) {
            int busy=0;for(int i=0;i<g->nmem;i++)busy|=g->m[i].flags&2;
            if(busy){g->flags&=~2;g->fire_clock=15;}else g->fire_state=2;
        }break;
    case 2:g->flags|=8;g->fire_cursor=0;g->fire_left=g->kind==8?5:1;g->fire_delay=0;g->fire_state=3;break;
    case 3:
        if(!(g->flags&2) || (g->flags&0x10)){g->flags&=~2;g->fire_state=0;break;}
        if(g->fire_clock>=30){g->fire_clock=0;g->flags|=0x80000000u;g->fire_left=g->kind==8?5:1;}
        if(!(g->flags&0x80000000u)){g->fire_cursor=0;g->fire_left=g->kind==8?5:1;g->fire_delay=0;}
        else if(g->fire_left<=0 || g->fire_cursor>=g->nmem || c->police_attack){g->flags&=~0x80000000u;g->fire_cursor=0;}
        else {
            GangMember *m=g->m+g->fire_cursor++;
            if(!(m->flags&0x120) && (m->anim==1 || m->anim==9) && !(m->flags&2)) {
                m->flags|=0x80000002u;g->fire_left--;
            }
        }break;
    }
    g->fire_clock++;
}

#include "rocket_flight.inc"

/* Group type 10 is the tracked Tank formation created by FUN_8c0a135c.
   FUN_8c070c84 gives it the same compact 24-byte route shape as Rocket:
   {position, speed, accumulated route distance, flags/member count}.
   The leader task FUN_8c0a3844 dispatches states 0..7 through 0x8c1c9108;
   its normal route step is the otherwise undecoded FUN_8c0a3f3c.  The SH-4
   instructions prove an XZ distance test, FUN_8c0a3e70 speed adjustment,
   a 30-unit arrival threshold and heading-derived XZ translation. */
#define TANK_ACC       0.0308641996f  /* parameter block 0x8c1c8f9c +0 */
#define TANK_DEC       0.0617283992f  /* +4 */
#define TANK_MEMBER_ADD 0.463031530f  /* +8: member target speed above leader */
#define TANK_ARRIVE   30.f            /* +0x1c, read by FUN_8c0a3f0c */
#define TANK_BODY_STEP 0x0040         /* +0x24: formation body-turn ramp */
#define TANK_BODY_MAX  0x0aaa         /* +0x20: formation body-turn cap */
#define TANK_AIM_STEP  0x0010         /* +0x2c: aim/body turn ramp */
#define TANK_AIM_MAX   0x0222         /* +0x30: aim/body turn cap */
#define TANK_TURN      0x0222         /* +0x34, FUN_8c1043f8 turn cap */
#define TANK_FIRE_MIN 150.f            /* +0x3c: do not fire inside this radius */
#define TANK_NEAR     400.f           /* +0x40, FUN_8c0a43a4 */
#define TANK_BACK_ANGLE 0x6000        /* literal 24576 in FUN_8c0a2734 */
#define TANK_ROUTE_TURN 0x5555        /* literal 21845 in FUN_8c0a3b70 */
#define TANK_REVERSE_GATE 7938        /* (546 - 16 * 10.5) * 21 in FUN_8c0a29ac */

static int tank_step_index(const Gang *g, int index, int direction)
{
    for (int tries = 0; tries < g->nwp; tries++) {
        index += direction;
        if (index < 0) index = g->nwp - 1; else if (index >= g->nwp) index = 0;
        if (!(g->wp_action[index] & 0x10)) break;   /* FUN_8c0a4144/FUN_8c0a42e8 */
    }
    return index;
}

/* FUN_8c0a4144 compares the player with every non-control route point and
   chooses a signed traversal direction using the accumulated +0x10 values. */
static int tank_choose_direction(Gang *g, const float *focus)
{
    if (!focus || g->nwp < 2) return 0;
    int nearest = -1; float best = 0.f;
    for (int i = 0; i < g->nwp; i++) {
        if (g->wp_action[i] & 0x10) continue;
        float dx = focus[0] - g->wp[i][0], dz = focus[2] - g->wp[i][2];
        float d2 = dx * dx + dz * dz;
        if (nearest < 0 || d2 < best) { nearest = i; best = d2; }
    }
    if (nearest < 0) return 0;
    if (nearest == g->wpi) {
        /* FUN_8c0a4144 returns one in this case, making leader state 2 wait
           in state 4 instead of advancing.  It also leaves the eventual
           traversal pointed toward the nearer adjacent non-control entry. */
        int next = tank_step_index(g, g->wpi, 1);
        int prev = tank_step_index(g, g->wpi, -1);
        float ndx = focus[0] - g->wp[next][0], ndz = focus[2] - g->wp[next][2];
        float pdx = focus[0] - g->wp[prev][0], pdz = focus[2] - g->wp[prev][2];
        float next_d2 = ndx * ndx + ndz * ndz, prev_d2 = pdx * pdx + pdz * pdz;
        if (next_d2 < prev_d2) g->burst_left = 1;
        else if (prev_d2 < next_d2) g->burst_left = -1;
        return 1;
    }
    float forward = 0.f, backward = 0.f; int i = g->wpi;
    for (int tries = 0; tries < g->nwp && i != nearest; tries++) { forward += g->wp_aux[i]; i = tank_step_index(g, i, 1); }
    i = g->wpi;
    for (int tries = 0; tries < g->nwp && i != nearest; tries++) { i = tank_step_index(g, i, -1); backward += g->wp_aux[i]; }
    g->burst_left = forward <= backward ? 1 : -1;
    return 0;
}

static void tank_advance(Gang *g, const float *focus)
{
    g->action = g->wp_action[g->wpi];
    (void)tank_choose_direction(g, focus);
    g->wpi = tank_step_index(g, g->wpi, g->burst_left);
    memcpy(g->target, g->wp[g->wpi], 12);
    g->next_action = g->wp_action[g->wpi];
}

static float tank_wrap(float a)
{
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

static int tank_angle_bam(float from, float to)
{
    float d = tank_wrap(to - from) * (65536.f / 6.2831853f);
    return d < 0.f ? (int)(d - .5f) : (int)(d + .5f);
}

static float tank_turn_yaw(float from, float to, int cap)
{
    float d = tank_wrap(to - from), step = cap * (6.2831853f / 65536.f);
    if (d > step) d = step; else if (d < -step) d = -step;
    return tank_wrap(from + d);
}

static int tank_turn_bam(int from, int to, int cap)
{
    int d = (short)(to - from);
    if (d > cap) d = cap; else if (d < -cap) d = -cap;
    return (short)(from + d);
}

static void tank_member_anim(GangMember *m, int anim)
{
    if (m->anim != anim) { m->anim = anim; m->frame = 0.f; }
}

/* FUN_8c0a09ec and its eight animation handlers.  These are animation-state
   transitions, not raw clip numbers (gang_motion performs that indirection). */
static void tank_member_animation_tick(GangMember *m)
{
    if (m->anim == 1 && m->frame >= 22.f) tank_member_anim(m, 2);
    else if (m->anim == 3 && m->frame >= 30.f) tank_member_anim(m, 0);
    else if (m->anim == 4 && m->frame >= 21.f) tank_member_anim(m, 5);
    else if (m->anim == 6 && m->frame >= 30.f) tank_member_anim(m, 0);
    else if (m->anim == 7 && m->frame >= 30.f) tank_member_anim(m, 0);
}

static void tank_member_target(const Gang *g, int index, float out[3])
{
    float yaw = atan2f(g->dir[0], g->dir[2]), sy = sinf(yaw), cy = cosf(yaw);
    const float *off = ROUTE_FORM[g->nmem - 1][index];
    out[0] = g->pos[0] + off[0] * cy + off[2] * sy;
    out[1] = g->pos[1] + off[1];
    out[2] = g->pos[2] - off[0] * sy + off[2] * cy;
}

/* FUN_8c0a32cc: each Tank member chases its transformed formation point with
   its own velocity and heading.  The old PSP path hard-snapped both every
   frame, which erased the source formation dynamics and turn behavior. */
static void tank_member_follow(Gang *g, GangMember *m, const float target[3])
{
    float wanted = g->speed + TANK_MEMBER_ADD;
    if (m->speed < wanted) { m->speed += TANK_ACC; if (m->speed > wanted) m->speed = wanted; }
    else if (m->speed > wanted) { m->speed -= TANK_DEC; if (m->speed < wanted) m->speed = wanted; }
    float dx = target[0] - m->pos[0], dz = target[2] - m->pos[2];
    float distance = sqrtf(dx * dx + dz * dz);
    if (distance <= .0001f) {
        m->flags |= 0x40;
        if (m->speed > 0.f) { m->speed -= TANK_DEC; if (m->speed < 0.f) m->speed = 0.f; }
        return;
    }
    m->flags &= ~0x40;
    if (m->turn_step < TANK_BODY_MAX) { m->turn_step += TANK_BODY_STEP; if (m->turn_step > TANK_BODY_MAX) m->turn_step = TANK_BODY_MAX; }
    m->yaw = tank_turn_yaw(m->yaw, atan2f(dx, dz), m->turn_step);
    if (distance < TANK_ARRIVE) {
        m->flags |= 0x40;
        if (m->speed > 0.f) { m->speed -= TANK_DEC; if (m->speed < 0.f) m->speed = 0.f; }
    } else {
        m->pos[0] += sinf(m->yaw) * m->speed;
        m->pos[2] += cosf(m->yaw) * m->speed;
    }
}

/* Member AI table 0x8c1c90a0: FUN_8c0a25f0..FUN_8c0a2a80. */
static void tank_member_tick(Gang *g, GangMember *m, const float *ppos,
                             const float target[3])
{
    tank_member_animation_tick(m);
    switch (m->state) {
    case 0:   /* FUN_8c0a25f0 */
        m->state = 1; tank_member_anim(m, 0); m->aim = m->aim_step = m->turn_step = 0;
        break;
    case 1:   /* FUN_8c0a2674 */
        if (m->flags & 0x10) m->flags |= 0x20;
        else if (!(m->flags & 1)) { m->state = 2; tank_member_anim(m, 1); m->flags &= ~0x100; }
        break;
    case 2:   /* FUN_8c0a26c0 */
        tank_member_follow(g, m, target);
        if (m->flags & 0x20002) {
            m->state = 5;
            if (m->speed != 0.f) tank_member_anim(m, 3);
        }
        if (m->aim != 0) m->aim = tank_turn_bam(m->aim, 0, TANK_TURN);
        break;
    case 3: { /* FUN_8c0a2734: acquire and fire only in the source 150..400 annulus */
        float dx = ppos[0] - m->pos[0], dz = ppos[2] - m->pos[2];
        float distance = sqrtf(dx * dx + dz * dz);
        if (distance <= TANK_FIRE_MIN) { m->flags &= ~2; break; }
        if (!(g->flags & 2)) { m->flags &= ~2; m->state = 2; break; }
        float target_yaw = atan2f(dx, dz);
        int target_aim = tank_angle_bam(m->yaw, target_yaw);
        if (abs(target_aim) <= TANK_BACK_ANGLE) {
            m->aim_step = 0;
            m->aim = tank_turn_bam(m->aim, target_aim, TANK_AIM_MAX);
            if ((m->flags & 2) && m->aim == target_aim) {
                m->state = 4; tank_member_anim(m, 7);
                if(g->combat && g->combat->shell)g->combat->shell(g->combat->context,g,m);
            }
        } else {
            if (m->aim_step < TANK_AIM_MAX) { m->aim_step += TANK_AIM_STEP; if (m->aim_step > TANK_AIM_MAX) m->aim_step = TANK_AIM_MAX; }
            m->yaw = tank_turn_yaw(m->yaw, target_yaw, m->aim_step);
        }
        break; }
    case 4:   /* FUN_8c0a28f4: attack motion finished */
        if (m->anim != 7) { m->state = 3; m->flags &= ~2; }
        break;
    case 5:   /* FUN_8c0a2920: brake before aiming or formation reversal */
        if (m->speed > 0.f) {
            m->speed -= TANK_DEC;
            if (m->speed < 0.f) m->speed = 0.f;
            else { m->pos[0] += sinf(m->yaw) * m->speed; m->pos[2] += cosf(m->yaw) * m->speed; }
        }
        if (m->speed == 0.f) {
            m->aim_step = 0;
            if (m->flags & 2) { m->state = 3; tank_member_anim(m, 0); }
            else { m->state = 6; tank_member_anim(m, 5); }
        }
        break;
    case 6: { /* FUN_8c0a29ac: accelerate the turn into the leader's new heading */
        if (m->aim_step < TANK_AIM_MAX) { m->aim_step += TANK_AIM_STEP; if (m->aim_step > TANK_AIM_MAX) m->aim_step = TANK_AIM_MAX; }
        float leader_yaw = atan2f(g->dir[0], g->dir[2]);
        m->yaw = tank_turn_yaw(m->yaw, leader_yaw, m->aim_step);
        if (abs(tank_angle_bam(m->yaw, leader_yaw)) < TANK_REVERSE_GATE) { m->state = 7; tank_member_anim(m, 6); }
        break; }
    case 7: { /* FUN_8c0a2a80: finish the turn/recovery clip, then follow again */
        if (m->aim_step > 0) { m->aim_step -= TANK_AIM_STEP; if (m->aim_step < 0) m->aim_step = 0; }
        m->yaw = tank_turn_yaw(m->yaw, atan2f(g->dir[0], g->dir[2]), m->aim_step);
        if (m->anim != 6) { m->state = 2; m->flags &= ~0x20001; }
        break; }
    }
}

static void tank_place_members(Gang *g, const float *ppos,
                               float (*ground)(void *, float, float, float), void *gctx)
{
    for (int i = 0; i < g->nmem; i++) {
        const float *off = ROUTE_FORM[g->nmem - 1][i]; GangMember *m = &g->m[i];
        float target[3]; memcpy(m->off, off, 12); tank_member_target(g, i, target);
        tank_member_tick(g, m, ppos, target);
        m->pos[1] = ground ? ground(gctx, m->pos[0], g->pos[1] + 80.f, m->pos[2]) : target[1];
        m->frame += 1.f;
    }
    if (g->nmem) g->unit_state = g->m[0].anim;
}

void gang_spawn_tank(Gang *g, const float (*wp)[3], const float *speed, const float *route_attr, const int *flags, int nwp, int trig, const float *quad)
{
    memset(g, 0, sizeof *g); g->active = 1; g->kind = 10; g->type = 10; g->mode = 8; g->trig = trig; if (quad) memcpy(g->quad, quad, 32);
    g->nwp = nwp > GANG_MAX_WAYPOINTS ? GANG_MAX_WAYPOINTS : nwp;
    for (int i = 0; i < g->nwp; i++) { memcpy(g->wp[i], wp[i], 12); g->wp_speed[i] = speed[i]; g->wp_aux[i] = route_attr[i]; g->wp_action[i] = flags[i]; }
    memcpy(g->pos, g->wp[0], 12); g->speed = g->wp_speed[0]; g->burst_left = 1;
    g->nmem = ((unsigned)g->wp_action[0] >> 16) & 0xffff; if (g->nmem < 1) g->nmem = 1; if (g->nmem > 5) g->nmem = 5;
    g->wpi = 0; tank_advance(g, 0);
    { float dx = g->target[0] - g->pos[0], dz = g->target[2] - g->pos[2], d = sqrtf(dx * dx + dz * dz);
      if (d > 0.0001f) { g->dir[0] = dx / d; g->dir[2] = dz / d; } }
    for (int i = 0; i < g->nmem; i++) {
        float target[3]; GangMember *m = &g->m[i]; m->motion = -1; m->flags = 0x101;
        tank_member_target(g, i, target); memcpy(m->pos, target, 12);
        m->yaw = atan2f(g->dir[0], g->dir[2]);
    }
    g->flags = 0x101; g->state = 1; g->unit_state = 0; g->log_state = -1;
    tank_place_members(g, g->pos, 0, 0);
}

static void tank_tick(Gang *g, const float *ppos, float (*ground)(void *, float, float, float), void *gctx)
{
    /* 0a43a4 -> 2724d4 measures all three axes. The separate command
       manager's continuation test below deliberately uses only XZ. */
    float pdx = ppos[0] - g->pos[0], pdy = ppos[1] - g->pos[1], pdz = ppos[2] - g->pos[2];
    float player_distance = sqrtf((pdx * pdx + pdz * pdz) + pdy * pdy);
    switch (g->state) {
    case 1:   /* FUN_8c0a3ac0: dormant until FUN_8c06e40c accepts the record trigger */
        if (gang_triggered(g, ppos)) {
            g->flags &= ~0x101; g->state = 3;
            for (int i = 0; i < g->nmem; i++) g->m[i].flags &= ~1;
        }
        break;
    case 2:   /* FUN_8c0a3b70: near-player branch, route selection, then the
                 ordinary movement or >=0x5555-BAM formation-reversal path. */
        if (player_distance <= TANK_NEAR) {
            g->state = 6; g->timer = 0;
        } else if (!(g->flags & 0x11) && tank_choose_direction(g, ppos)) {
            /* FUN_8c0a42e8 returns one when the current authored entry is
               already the closest one to the player. */
            g->state = 4; g->timer = 0;
        } else {
            tank_advance(g, 0);
            { float target_yaw = atan2f(g->target[0] - g->pos[0], g->target[2] - g->pos[2]);
              g->state = abs(tank_angle_bam(atan2f(g->dir[0], g->dir[2]), target_yaw)) >= TANK_ROUTE_TURN ? 5 : 3;
              g->timer = 0; }
        }
        break;
    case 4:   /* FUN_8c0a3c14: source route-selection settle state */
        if (player_distance <= TANK_NEAR) { g->state = 6; g->timer = 0; }
        else if (++g->timer > 30) { g->state = 2; g->timer = 0; }
        break;
    case 5:   /* FUN_8c0a3c50: signal members, reverse the formation, wait for recovery */
        if (g->timer == 0) {
            for (int i = 0; i < g->nmem; i++) g->m[i].flags |= 0x20000;
            g->speed = 0.f;
            { float dx = g->target[0] - g->pos[0], dz = g->target[2] - g->pos[2], d = sqrtf(dx * dx + dz * dz);
              if (d > .0001f) { g->dir[0] = dx / d; g->dir[2] = dz / d; } }
            g->timer = 1;
        } else if (g->timer == 1) {
            int turning = 0; for (int i = 0; i < g->nmem; i++) turning |= (g->m[i].flags & 0x20000) != 0;
            if (!turning) g->timer = 2;
        } else { g->state = 3; g->timer = 0; }
        break;
    case 6:   /* FUN_8c0a3d4c: substate 0 brakes once and updates position;
                 substate 1 holds group flag 2 while FUN_8c0a43a4 stays true;
                 substate 2 returns to route selection. */
        if (g->timer == 0) {
            int stopped = g->speed <= 0.f;
            if (!stopped) { g->speed -= TANK_DEC; if (g->speed < 0.f) { g->speed = 0.f; stopped = 1; } }
            g->pos[0] += g->dir[0] * g->speed; g->pos[2] += g->dir[2] * g->speed;
            g->timer = stopped ? 2 : 1;
        } else if (g->timer == 1) {
            if (player_distance <= TANK_NEAR) g->flags |= 2;
            else { g->flags &= ~2; g->timer = 2; }
        } else {
            g->state = 2; g->timer = 0;
        }
        break;
    case 3: default: {   /* FUN_8c0a3f3c: source XZ route integrator */
        float dx = g->target[0] - g->pos[0], dz = g->target[2] - g->pos[2];
        float distance = sqrtf(dx * dx + dz * dz), wanted = g->wp_speed[g->wpi];
        if (g->speed < wanted) { g->speed += TANK_ACC; if (g->speed > wanted) g->speed = wanted; }
        else if (g->speed > wanted) { g->speed -= TANK_DEC; if (g->speed < wanted) g->speed = wanted; }
        { float delta[3] = {dx, 0.f, dz}; steer(g->dir, delta, TANK_TURN); }
        g->pos[0] += g->dir[0] * g->speed; g->pos[2] += g->dir[2] * g->speed;
        if (distance <= TANK_ARRIVE) g->state = 2;
        break; }
    }
    if (ground) g->pos[1] = ground(gctx, g->pos[0], g->pos[1], g->pos[2]);
    /* 0a135c creates a separate 0741c8 command task. The member clears
       its own order after firing; the manager must issue later bursts. */
    TankOrders orders={g->fire_clock,g->fire_cursor,g->fire_delay,g->fire_left,g->fire_state};
    unsigned flags=(unsigned)g->flags,members[GANG_MAX_MEMBERS];int another=0;
    for(int i=0;i<g->nmem;i++)members[i]=(unsigned)g->m[i].flags;
    if(orders.state==1 && orders.clock>=90 && (flags&2) && g->combat) {
        const Gang *pool=g->combat->formation_groups;
        for(int i=0;pool && i<g->combat->formation_group_count;i++)
            if(pool+i!=g && pool[i].active && pool[i].kind==10 && (pool[i].flags&2)){another=1;break;}
    }
    tank_orders_tick(&orders,&flags,members,g->nmem,another,
        g->combat ? g->combat->police_attack:0,pdx*pdx+pdz*pdz<=TANK_NEAR*TANK_NEAR);
    g->flags=(int)flags;
    for(int i=0;i<g->nmem;i++)g->m[i].flags=(int)members[i];
    g->fire_clock=orders.clock;g->fire_cursor=orders.cursor;g->fire_delay=orders.delay;
    g->fire_left=orders.left;g->fire_state=orders.state;
    tank_place_members(g, ppos, ground, gctx);
    if (g->state != g->log_state) { printf("tank: state %d->%d pos %.0f,%.0f,%.0f wp %d/%d speed %.3f attr %.3f flags %x/%x x%d pdist %.0f\n", g->log_state, g->state, g->pos[0], g->pos[1], g->pos[2], g->wpi, g->nwp, g->speed, g->wp_aux[g->wpi], g->action, g->next_action, g->nmem, player_distance); g->log_state = g->state; }
}
void gang_backtag(Gang *g, const float *ppos, float pyaw, int spray_pressed)
{
    if (!g->active || g->mode != 1 || g->tagged) return;
    float dx = ppos[0] - g->pos[0], dz = ppos[2] - g->pos[2]; float d2 = dx * dx + dz * dz;
    float bx = -sinf(g->m[0].yaw), bz = -cosf(g->m[0].yaw);   /* behind = opposite of the heading */
    int behind = (dx * bx + dz * bz) > 0.f;
    if (g->tag_ticks > 0) g->tag_ticks--;
    if (spray_pressed && behind && d2 < 14.f * 14.f) { g->tagprog += 0x1d; g->tag_ticks = 30; if (g->state != 5) { g->state = 5; g->unit_state = 2; g->m[0].frame = 0.f; } if (g->tagprog >= 0x120) { g->tagged = 1; g->active = 0; } }
    (void)pyaw;
}
unsigned gang_graffiti_policy(const Gang *g,const GangMember *m)
{
    if(!g->active || !g->graffiti)return 0;
    unsigned flags=(unsigned)m->flags,reg=0,draw=0;
    int revealed=g->graffiti->complete || g->graffiti->reveal!=0; /* 06e074 */
    if(g->kind==23) { /* 07ba50 / 07bcd8 */
        reg=!(flags&0x120) && m->anim!=20;
        draw=!(flags&0x120) && revealed;
    } else if(g->kind==25) { /* 077638 -> 07de6c; draw 07df68 */
        reg=!(flags&0x20); /* callback registers even while flags&100 */
        draw=!(flags&0x120) && revealed;
    } else if(g->kind==8 || g->kind==24) { /* 099cd8 / 099ed4 */
        reg=!(flags&0x120) && (m->state==4 || m->state==8 || m->state==9);
        draw=!(flags&0x120) && ((g->flags&0x2000) || ((flags&0x4000000) && revealed));
    } else if(g->kind==17 || g->mode==4 || g->mode==1) {
        reg=!(g->flags&0x120) && !(g->mode==4 && (g->flags&0x1000000));
        draw=reg && revealed;
    }
    return (reg && !g->graffiti->complete ? GANG_TAG_REGISTER:0) | (draw ? GANG_TAG_DRAW:0);
}
int gang_graffiti_arrow(const Gang *g,const GangMember *m)
{
    if(!g->active || (m->flags&0x120))return -1;
    /* Rival skaters use 019cfc, not the green enemy helper 06e074. */
    if(g->mode==1)return g->graffiti && !g->graffiti->complete && !(g->flags&0x120) ? 0:-1;
    if(!g->graffiti || g->graffiti->complete)return -1;
    if(g->kind==8 || g->kind==24) {
        /* 099ed4's parent&2000 short-circuits the helper entirely. */
        if((g->flags&0x2000) || !(m->flags&0x4000000))return -1;
    } else if(g->kind!=23 && g->kind!=25) {
        if(g->flags&0x120)return -1;
        if(g->mode==4 && (g->flags&0x1000000))return -1;
    }
    return 0;
}
void gang_member_matrix(const Gang *g,const GangMember *m,float root[16])
{
    if(g->mode==1 && g->state<3) {
        /* 0df884: the stationary rival uses the authored Z,Y,X root. */
        float cy=cosf(m->yaw),sy=sinf(m->yaw),cx=cosf(m->pitch),sx=sinf(m->pitch);
        float cz=cosf(m->roll),sz=sinf(m->roll);
        float result[16]={cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx,m->pos[0],
            sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx,m->pos[1],
            -sy,cy*sx,cy*cx,m->pos[2],0,0,0,1};
        memcpy(root,result,sizeof result);return;
    }
    int swat=g->kind==23 || g->kind==25;
    float yaw=g->kind==25 && m->state==6 ? m->visual_yaw:m->yaw;
    if(g->kind==2 && m->anim==8)yaw=m->police.pace_yaw*0.000095873799242853f;
    int tilt=((g->kind==0 || g->kind==2) && (m->anim==6 || m->state==16)) || (g->kind==5 && m->anim==6) || g->mode==4 || g->mode==5 || g->mode==7 || (swat && (m->anim==6 || (g->kind==25 && m->state==16)));
    float cy=cosf(yaw),sy=sinf(yaw),cx=cosf(tilt ? -m->pitch:0),sx=sinf(tilt ? -m->pitch:0);
    float cz=cosf(tilt ? -m->roll:0),sz=sinf(tilt ? -m->roll:0);
    /* Owner update order is Y,-X,-Z, not the NJS bone Z,Y,X order. */
    float result[16]={cy*cz+sy*sx*sz,-cy*sz+sy*sx*cz,sy*cx,m->pos[0],
        cx*sz,cx*cz,-sx,m->pos[1],-sy*cz+cy*sx*sz,sy*sz+cy*sx*cz,cy*cx,m->pos[2],0,0,0,1};
    memcpy(root,result,sizeof result);
}
unsigned gang_graffiti_member_tick(const Gang *g,GangMember *m)
{
    if(g->kind==25 && !(m->flags&0x20) && m->state!=6)m->visual_yaw=m->yaw;
    unsigned policy=gang_graffiti_policy(g,m);
    if(g->mode==7 && !(m->flags&0x120)) {
        m->flags&=~0x4000000;
        if(policy&GANG_TAG_REGISTER)m->flags|=0x4000000;
    }
    return gang_graffiti_policy(g,m);
}
int gang_take_legacy_tag(Gang *g)
{
    if(g->mode!=1 || g->tagged!=1)return 0;
    g->tagged=2;return 1;
}
int gang_reports_police(const Gang *g) { return g->active && ((g->mode == 2 || g->mode == 9) ? g->state == 8 : g->mode == 8 ? g->state == 6 : (g->mode >= 3 && g->mode <= 7) ? 0 : g->state == 7); }

/* 0746f4 / 0749c4 ordinary formation-member floor work. Spawn placement
   remains a one-time world query. Thereafter grounded stationary actors do
   no work; moving/falling actors sweep previous +3 to pending -1. Retain
   the source gravity/terminal speed (+44/+48 of the member tuning block).
   Para descent and the separate vehicle/chief controllers own their paths. */
static void formation_floor(Gang *g,GangMember *m,const float before[3],
                            float (*ground)(void *,float,float,float),void *ctx)
{
    if(!m->floor.initialized) {
        if(ground)m->pos[1]=ground(ctx,m->pos[0],m->pos[1],m->pos[2]);
        m->floor.initialized=1;m->floor.vy=0;return;
    }
    if(m->flags&0x4000100)return;
    if(!(m->flags&0x80) && m->floor.vy==0.f && !memcmp(before,m->pos,12))return;
    m->pos[1]+=m->floor.vy;
    float y;
    if(g->combat && g->combat->police_floor) {
        if(g->combat->police_floor(g->combat->context,before,m->pos,m->flags,&y) && m->floor.vy<=0.f) {
            m->pos[1]=y;m->floor.vy=0.f;m->flags&=~0x80;
        } else {
            if(m->floor.vy>0.f)m->floor.vy=0.f;
            m->floor.vy=fmaxf(-g->par[18],m->floor.vy-g->par[17]);m->flags|=0x80;
        }
    } else if(ground) m->pos[1]=ground(ctx,m->pos[0],m->pos[1],m->pos[2]);
}
static void formation_leader_floor(Gang *g,const float before[3],
                                   float (*ground)(void *,float,float,float),void *ctx)
{
    /* The route proxy is not a 0749c4 member. Keep its placement semantics;
       do not repeatedly recast a grounded waypoint that hasn't moved. */
    if(ground && (!g->floor_initialized || memcmp(before,g->pos,12)))
        g->pos[1]=ground(ctx,g->pos[0],g->pos[1],g->pos[2]);
    g->floor_initialized=1;
}

#include "police_member.inc"
#include "kairiki_leader.inc"

/* 0720dc: the formation route proxy brakes beside the player. */
static void formation_leader_approach(Gang *g,float distance,float player_speed)
{
    const float *p=g->par;float speed=fabsf(player_speed);
    float range=p[11]+speed;
    if(g->flags&0x10000)decelerate(g);
    else if(distance<range) {
        if(distance<g->route_brake){g->route_brake=p[13];g->speed=fminf(distance,fminf(speed,p[4]));}
        else {g->route_brake=p[12];if(g->speed>speed)g->speed=fmaxf(speed,g->speed-p[7]);}
    } else {g->route_brake=p[12];accelerate(g,p[g->flags&2?4:3]);}
    follow_target(g);
}
static void dog_begin_attack(Gang *g)
{
    GangCombat *c=g->combat;if(!c)return;
    float dot=-(g->dir[0]*c->player_direction[0]+g->dir[2]*c->player_direction[2]);
    if(dot>cosf(0x1555*0.000095873799242853f))g->flags|=0x800;
}

/* Mine entries borrow the checked GRP1 image for the lifetime of the level.
   No per-mine allocation, model pose, draw call or collision-world scan. */
#include "enemy_mine.h"
void gang_spawn_mines(Gang *g,const unsigned char *entries,unsigned count)
{
    memset(g,0,sizeof *g);g->active=1;g->kind=g->type=31;g->mode=10;
    g->mines.entries=entries;g->mines.count=count>256?256:count;
}
static void mines_tick(Gang *g,const float *player)
{
    unsigned live=0;GangCombat *c=g->combat;
    for(unsigned i=0;i<g->mines.count;i++) {
        if(g->mines.state[i]==4)continue;
        const unsigned char *e=g->mines.entries+28*i;
        float p[3]={rdf(e),rdf(e+4),rdf(e+8)},d=0;
        for(int j=0;j<3;j++){float v=p[j]-player[j];d+=v*v;}
        int chain=c && c->mine_chain?c->mine_chain(c->context,p):0;
        int event=enemy_mine_tick(g->mines.state+i,g->mines.timer+i,g->flags,d,chain);
        if(c && event==1 && c->mine_sound)c->mine_sound(c->context,p);
        if(c && event==2 && c->explosion)c->explosion(c->context,p,0);
        if(g->mines.state[i]!=4)live++;
    }
    if(!live)g->flags|=0x20;
}

void gang_tick(Gang *g, const Graphs *G, int area, const float *ppos, float pspeed, int player_off_graph, int police_attack, float (*ground)(void *, float, float, float), void *gctx)
{
    if (!g->active) return;
    if(g->flags&0x20){if((g->kind==3 || g->kind==4 || g->kind==6 || g->kind==9 || g->kind==25)){riot_unlink(g);if(g->formation_next)riot_unlink(g->formation_next);}g->active=0;return;} /* 070ba4, before this group's child AI */
    if(g->mode==1 && !g->tagged){g->arrow_frame+=1.f;if(g->arrow_frame>=120.f)g->arrow_frame-=120.f;}
    g_wall_ctx = gctx;
    { int trig_now = 0; (void)trig_now; }
    if (g->mode == 1) { solo_tick(g, ppos, police_attack, ground, gctx); memcpy(g->pprev, ppos, 12); g->have_prev = 1; return; }
    if (g->mode == 2) { patrol_tick(g, G, area, ppos, police_attack, ground, gctx); memcpy(g->pprev, ppos, 12); g->have_prev = 1; return; }
    if (g->mode == 3) { mover_tick(g, ppos, police_attack, ground, gctx); memcpy(g->pprev, ppos, 12); g->have_prev = 1; return; }
    if (g->mode == 4) { heli_tick(g, ppos); memcpy(g->pprev, ppos, 12); g->have_prev = 1; return; }
    if (g->mode == 5) { sniper_tick(g, ppos, ground, gctx); memcpy(g->pprev, ppos, 12); g->have_prev = 1; return; }
    if (g->mode == 6) { denki_tick(g, ppos); memcpy(g->pprev, ppos, 12); g->have_prev = 1; return; }
    if (g->mode == 7) { rocket_tick(g, ppos); memcpy(g->pprev, ppos, 12); g->have_prev = 1; return; }
    if (g->mode == 8) { tank_tick(g, ppos, ground, gctx); memcpy(g->pprev, ppos, 12); g->have_prev = 1; return; }
    if (g->mode == 9) { taban_tick(g, G, area, ppos, pspeed, player_off_graph, ground, gctx); memcpy(g->pprev, ppos, 12); g->have_prev = 1; return; }
    if(g->mode==10){mines_tick(g,ppos);return;}
    float leader_before[3];memcpy(leader_before,g->pos,12);
    const float *par = g->par; float dpx = ppos[0] - g->pos[0], dpz = ppos[2] - g->pos[2]; float pdist = sqrtf(dpx * dpx + dpz * dpz);
    if(g->kind==6)kairiki_leader_tick(g,G,ppos,pspeed,player_off_graph,police_attack);
    else {
    if((g->kind==3 || g->kind==9 || g->kind==25) && (g->flags&0x10) && g->state<10){
        for(int i=0;i<g->nmem;i++)g->m[i].flags|=0x10;
        g->state=10;
    }
    if((g->kind==3 || g->kind==4 || g->kind==9 || g->kind==25) && g->formation_lead)riot_target(g);
    if (g->target_player) memcpy(g->target, ppos, 12);
    switch (g->state) {
    case 0: g->target_player = 1; g->flags |= 0x101; g->cnt = 0; g->timer = 0; g->route_brake=par[12]; g->state = 1; break;
    case 1:   /* FUN_8c071610: members in place, player in range (FUN_8c06e40c: 100 units like the squads) */
        g->flags &= ~0x100;
        if (gang_triggered(g, ppos) && !police_attack) {
            g->flags &= ~1; g->state = g->type == 7 ? 2 : 3; g->timer = 0;

        }
        break;
    case 2: { /* 071740: wait for every member to finish landing, at most 300 ticks. */
        int landing=0;for(int i=0;i<g->nmem;i++)landing|=g->m[i].flags&0x100000;
        if(g->timer++>=300 || !landing)g->state=3;
        break; }
    case 3: {  /* FUN_8c07178c */
        g->timer = 0;
        if (player_off_graph) {decelerate(g);if((g->kind==3 || g->kind==4 || g->kind==9 || g->kind==25) || g->type==3 || g->kind==5 || g->kind==23)break;}   /* 0x8c1b4d9c < 0: 07178c returns after braking */
        g->node = graph_node_at(g->graph, g->pos);
        if (g->node >= 0) memcpy(g->lastnode,g->pos,12); /* 071f40: last valid position */
        if (g->node < 0) {
            int n2 = graph_node_at(g->graph, g->lastnode);
            if (n2 < 0) decelerate(g);
            else { memcpy(g->target, g->graph->nodes[n2].pos, 12); g->target_player = 0; g->state = 6; }
        } else { int r = plan_route(g, G); g->state = r == 0 ? 5 : 4; }   /* FUN_8c06e36c: same node -> approach directly (5), else travel the path (4) */
        break; }
    case 4:   /* FUN_8c071890 patrol / chase check */
        if (!player_off_graph && pdist < par[P_RANGE]) { g->state = 7; g->flags |= 2; g->target_player = 1;if((g->kind==3 || g->kind==4 || g->kind==9 || g->kind==25))riot_unlink(g);if(g->kind==2)dog_begin_attack(g); break; }
        if (++g->timer > (int)par[P_CHASECHK]) g->state = 3;
        if (!follow_target(g)) { decelerate(g); g->state = 3; } else {accelerate(g, par[P_MAXSPD]);if((g->kind==3 || g->kind==4 || g->kind==9 || g->kind==25)){riot_join(g,ppos);if(!g->formation_lead)g->cnt++;}}
        break;
    case 5:   /* FUN_8c0719f4 approach: speed law FUN_8c0720dc toward the target */
        if((g->kind==3 || g->kind==4 || g->kind==9 || g->kind==25)) {
            riot_join(g,ppos);riot_target(g);if(!g->formation_lead)g->cnt++;
            float distance=riot_approach(g,pspeed);
            if(distance<0 || graph_node_at(g->graph,g->target)!=g->node){g->state=3;g->flags&=~0x40;break;}
            if(g->formation_lead) {
                if(distance<=par[9])g->flags|=0x40;else g->flags&=~0x40;
                break;
            }
        } else if(g->kind==2 || g->type==3 || g->type==7 || g->kind==5 || g->kind==23)formation_leader_approach(g,pdist,pspeed);else {accelerate(g, par[P_CHASESPD]); follow_target(g);}
        if (pdist-g->speed*((g->kind==3 || g->kind==4 || g->kind==9 || g->kind==25)) < par[P_RANGE]) { g->state = 7; g->flags |= 2;if(g->kind==2)dog_begin_attack(g); }
        if (graph_node_at(g->graph, g->pos) < 0) g->state = 3;
        break;
    case 6:   /* FUN_8c071b4c go to node */
        accelerate(g, par[P_MAXSPD]); follow_target(g);
        if (graph_node_at(g->graph, g->pos) >= 0) { g->state = 3; g->target_player = 1; }
        break;
    case 7:   /* FUN_8c071bc8 chase */
        if(formation_route(g) && (!(g->flags&2) || police_attack)) {g->flags&=~2;g->state=8;break;}
        g->flags |= 0x20000; g->target_player = (g->kind==3 || g->kind==4 || g->kind==9 || g->kind==25) && g->formation_lead?0:1;
        if((g->kind==3 || g->kind==4 || g->kind==9 || g->kind==25)){if(!(g->flags&8))riot_approach(g,pspeed);}
        else if((g->kind!=0 && g->kind!=2 && g->type!=3 && g->type!=7 && g->kind!=5 && g->kind!=23) || !(g->flags&8)){if(g->kind==2 || g->type==3 || g->type==7 || g->kind==5 || g->kind==23)formation_leader_approach(g,pdist,pspeed);else {accelerate(g, par[P_CHASESPD]); follow_target(g);}}
        if (!(g->flags & 2) || police_attack) { g->flags &= ~2; g->state = 8; }
        break;
    case 8: g->cnt = 0; g->flags &= ~0x20801; g->state = 3; break;
    case 9: if(formation_route(g))riot_approach(g,pspeed);if (++g->cnt > (int)par[P_REGROUP]) { g->flags &= ~0x10000; g->cnt = 0; g->state = g->saved; } break;
    case 10:
        if(g->kind!=3 && g->kind!=9 && g->kind!=25)g->flags|=0x20;
        else if(!plan_departure(g,G,ppos))g->flags|=0x20;
        g->state=11;break;
    case 11: accelerate(g, par[P_MAXSPD] * 2.f); if (!follow_target(g)) { g->speed = 0.f; g->active = 0; } break;
    default: g->state = 3; break;
    }
    if(formation_route(g) && (g->state==4 || g->state==5) &&
       (g->formation_lead?(g->formation_lead->flags&0x10000):g->cnt>=(int)par[P_IDLE])) {
        g->flags|=0x10000;g->saved=g->state;g->state=9;g->cnt=0;
    }
    }
    if (g->state != g->log_state) { printf("gang%d: state %d->%d pos %.0f,%.0f,%.0f spd %.2f node %d path %d/%d pdist %.0f\n", g->kind, g->log_state, g->state, g->pos[0], g->pos[1], g->pos[2], g->speed, g->node, g->pathi, g->npath, pdist); g->log_state = g->state; }
    if(gang_source_member(g)) {
        if(g->kind==9 || g->kind==25)fire_attack_tick(g,player_off_graph,police_attack);
        else if(g->kind==6){}
        else if(g->kind==4)riot_attack_tick(g,police_attack);
        else if(g->type==7)para_attack_tick(g,pdist,player_off_graph,police_attack);else police_attack_tick(g,pdist,player_off_graph,police_attack);
        police_separate(g);
        for(int i=0;i<g->nmem;i++)police_member_tick(g,g->m+i,ppos,ground,gctx);
        formation_leader_floor(g,leader_before,ground,gctx);
        return;
    }
    formation_member_entry_tick(g);
    /* Remaining families use the legacy formation follower. Gas squads, Para, Keikan
       and Dog run their per-member source controllers above. */
    float cy = g->dir[2], sy = g->dir[0];
    for (int i = 0; i < g->nmem; i++) {
        GangMember *m = &g->m[i]; float tx = g->pos[0] + m->off[0] * cy + m->off[2] * sy, tz = g->pos[2] - m->off[0] * sy + m->off[2] * cy;
        float before[3];memcpy(before,m->pos,12);
        float dx = tx - m->pos[0], dz = tz - m->pos[2], d = sqrtf(dx * dx + dz * dz); float step = g->speed + 0.5f;
        if (d > step) { m->pos[0] += dx / d * step; m->pos[2] += dz / d * step; m->yaw = atan2f(dx, dz); }
        else { m->pos[0] = tx; m->pos[2] = tz; if (g->speed > 0.05f) m->yaw = atan2f(g->dir[0], g->dir[2]); }
        formation_floor(g,m,before,ground,gctx);
        m->frame += 1.f;
    }
    formation_leader_floor(g,leader_before,ground,gctx);
}
