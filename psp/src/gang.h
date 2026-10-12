#ifndef GANG_H
#define GANG_H
#include "object_graffiti.h"
#include "helidestruct.h"
#include "enemy_gun.h"
#include "police_grab.h"
#include "source_body.h"
#include "taban_attack.h"
#include "enemy_flame.h"
#include "sniper_ai.h"
/* 0e00a8 / 0e01d0: previous ray's first contact and subsequent floor. */
typedef struct {int hit,wall,floor;float point[3],normal[3],height;} RivalTerrain;
typedef struct GangCombat {
    void *context;
    unsigned (*shot)(void *,const float *,const float *,int,int);
    int (*shot_result)(void *,unsigned);
    void (*sound)(void *,int,const float *);
    void (*missile)(void *,const float *,const float *,int);
    int (*anchor)(void *,const void *,int,float *);
    int (*taban_anchor)(void *,const void *,float [2][3]);
    void (*taban_sound)(void *,const float *);
    void (*mine_sound)(void *,const float *);
    int (*mine_chain)(void *,const float *);
    void (*explosion)(void *,const float *,int);
    unsigned (*gun_sound)(void *,unsigned,int,const float *);
    unsigned (*voice_sound)(void *,unsigned,int,const float *);
    unsigned (*fire_stream)(void *,const void *,void *,int,int);
    void (*strong_attack)(void *,const void *,const void *);
    void (*burning_hands)(void *,const void *,const void *);
    unsigned (*random15)(void *);
    void (*gas)(void *,const float *,int);
    void (*shell)(void *,const void *,const void *);
    void (*tank_shell)(void *,const float *,float,float,int);
    int (*contact)(void *,const float *,const float *,float,int);
    void (*electric)(void *,const void *);
    PoliceGrips *grips;
    int grip_eligible,grip_anchors_valid;float grip_anchors[3][3];
    int (*police_contact)(void *,const void *,SourceActorContact *);
    int (*sniper_contact)(void *,const void *);
    int (*beam_contact)(void *,const float *,const float *,float);
    int (*rival_contact)(void *,const void *);
    void (*rival_terrain)(void *,const float *,const float *,RivalTerrain *);
    int (*police_obstacle)(void *,const float *,const float *,float *);
    int (*dog_obstacle)(void *,const float *,const float *,float *);
    int (*police_floor)(void *,const float *,const float *,unsigned,float *);
    void (*police_aim)(void *,int,float *);
    void *formation_groups;int formation_group_count; /* borrowed pool for 0742a0 arbitration */
    const void *dog_attacking_group;
    const void *police_attacking_group; /* borrowed only for this world tick */
    float target[3],velocity[3],history[30][3],player_direction[3];
    int history_at,history_valid,player_state;float player_speed;
    int vulnerable,warning,report,police_attack;
} GangCombat;
/* Rival gang route AI, transcribed from the Dreamcast route task 0xc8 (FUN_8c071260, states 0x8c1b6e90, params 0x8c1b7e0c) and the
   route graphs of the mission set (+0x10, tools/jgs_graph.py -> setN_graph.bin 'GRA1'). */
typedef struct { float min[3], max[3], pos[3]; int nlinks, link0; } GNode;
typedef struct { int n, has_xf; float xf[12]; GNode *nodes; } GGraph;
typedef struct { int ngraphs; GGraph *g; } GArea;
typedef struct { GArea area[6]; int nlinks; int *link; float *lpos; } Graphs;   /* lpos = 3 floats per link (entry point) */
int  graphs_load(Graphs *G, const char *path);
void graphs_free(Graphs *G);
/* FUN_8c06d91c: index of the node whose box holds pos (-1 none) */
int  graph_node_at(const GGraph *g, const float *pos);
/* FUN_8c03c254: the graph of `area` whose node holds the player (0 = none) */
const GGraph *graphs_pick(const Graphs *G, int area, const float *ppos);

#define GANG_MAX_MEMBERS 8
#define GANG_MAX_WAYPOINTS 32   /* type 8/24 FUN_8c0997b4 routes contain as many as 28 source entries */
typedef struct { float fraction; unsigned pressed; } GangClock;
int gang_clock_steps(GangClock *clock,float dt,unsigned pressed);
unsigned gang_clock_pressed(GangClock *clock);
typedef struct {
    union { struct {
        unsigned token;int slot,jumps,player_state,old_anim,old_motion,turn_wait,grounded;
        float near,vy,distance,blend,gait,old_frame,previous[3],recoil[3];
        int dog,para,riot,killer,bomb,fire,kairiki,pace_end,pace_yaw,head;float pace_origin[3];
        union {
            struct {float walk_frame,pose_frame,gas_pose_frame;}; /* Kidoutai gait/throw clocks */
            EnemyHandFlames flames; /* Bomb-B children, no larger actor pool */
        };
    } police;
    struct {
        int old_state,old_motion,turn_wait,variant;
        float blend,gait,old_frame,cycle_frame,old_yaw;
    } chief;
    struct {int initialized;float vy;} floor;
    struct {
        int angles[3],target,choice,old_anim;
        float pending[3],normal[3],walk_frame;
        RivalTerrain terrain;
    } solo;
    struct {
        int angles[3],target,beam,body;
        float pending[3],normal[3],blend;
        RivalTerrain terrain;
    } sniper;
    struct {int delay,arc[3],yaw[3];float scale,frame;unsigned texture;} denki;
    }; /* Mutually exclusive actor controllers; no larger member pool. */
    float off[3], pos[3], yaw, frame;
    int flags, motion;
    /* Formation-member state.  Rocket uses primary AI +0x114, primary
       animation +0x116 and overlay +0x118/+0x119 (FUN_8c099adc).
       Tank uses AI +0xdc, animation +0xe2, speed +0x84, torso aim +0x100
       and aim/body turn ramps +0x104/+0x170 (FUN_8c0a181c). */
    int state, anim, overlay, secondary_anim, timer;
    float secondary_frame;
    EnemyGun gun;
    float speed;
    int aim, aim_step, turn_step;
    float pitch,roll,visual_yaw; /* source +78/+80/+a4 (Rocket +f0/+f8); radians at renderer boundary */
} GangMember;   /* 0x278 skater: +0x3c flags, formation offset, pose */
typedef struct Gang {
    struct Gang *formation_lead,*formation_next; /* original route group +ec/+f0 */
    int floor_initialized; /* route proxy: perform initial placement once */
    int active, kind, type;             /* record kind (character bank) / record type (4 or 0x17: parameter block) */
    int solo_contact,solo_drops; /* previous body pass / pending 0e0580 emission */
    unsigned parent_handle, *parent_flags; /* source record parent +18 / +6e; not inferred from AI state numbers */
    unsigned graffiti_handle; const GfoProgress *graffiti; /* source group +108, separate from parent +6e */
    float arrow_frame; /* owner +110; rival skaters use actor +270 */
    const float *par;                   /* 0x8c1b7e0c / 0x8c1b7e80 */
    float pos[3], dir[3], speed, vy;    /* group object +0x6c pos, +0x9c heading, rec +0x84 speed, +0x88 vertical */
    float target[3]; int target_player; /* +0xd4 target (player position or node waypoint) */
    int state, saved, timer, cnt;       /* task +0x70, +0x74, +0x78, rec +0x98 */
    int node, flags;                    /* rec +0x8c, +0xd0 */
    float lastnode[3];                  /* task +0xac */
    const GGraph *graph;                /* 0x8c2c54f4 at spawn */
    int path[16], npath, pathi, log_state;
    const Graphs *routes; int route_phase; /* borrowed link portals; centre/portal cursor */
    int trig; float quad[8]; float pprev[3]; int have_prev;
    int hit; float hit_dx, hit_dz;
    int mode; int burst_left; union { struct { float wp[GANG_MAX_WAYPOINTS][3], wp_speed[GANG_MAX_WAYPOINTS], wp_aux[GANG_MAX_WAYPOINTS]; int wp_action[GANG_MAX_WAYPOINTS]; }; TabanAttack taban; struct {const unsigned char *entries;unsigned count;unsigned char state[256],timer[256];} mines; }; int nwp, wpi, action, next_action, unit_state; float tgt_yaw, route_brake; int tagprog, tag_ticks, tagged;   /* modes: 1 solo skater, 2 Onishima patrol, 3 ground mover, 4 helicopter, 5 sniper, 6 Denki, 7 Rocket route formation, 8 Tank route, 9 Taban */      /* attacks that connected this tick (consumed by main.c like the squads') */            /* +0xc4 trigger kind, +0xc8 quad (4 xz points) */         /* rec +0x90 descriptor chain: nodes to visit */
    int nmem; GangMember m[GANG_MAX_MEMBERS];
    /* Helicopter task fields kept separate from the generic patrol shell. */
    int heli_angles[3],heli_yaw_target,heli_yaw_old,heli_bank,heli_turn;
    int heli_crash_entry,heli_brake_steps,heli_contact;
    float heli_candidate[3],heli_hit[3];
    float heli_body_previous[3];int heli_body_valid;
    HeliDestruction wreck;
    HeliDestructionEvents wreck_events;
    GangCombat *combat; /* borrowed for one world tick; never a member address */
    int fire_clock,fire_cursor,fire_left,fire_delay,fire_state;
    int heli_missile_cooldown,heli_missile_index,heli_missile_left,heli_missile_delay;
} Gang;
/* Original route renderers 07fd8c (PatCar), 083698 (Bike), 0acf98 (Benz).
   Use before pose preparation as well as drawing; waiting owners stay live. */
static inline int gang_mover_hidden(const Gang *g)
{return g->mode==3 && (g->flags&0x120);}
/* Original draw gates on the camera depth of the actor's bound centre
   (actor space). 06dd7c: detailed model below near, low model to fade,
   linear additive fade to far, hidden beyond. Every parameter record in the
   executable reads 100/300/600 (route police 1b7e0c/1b7e80, Onishima 1b918c,
   Taban 21b524, formation members) except the solo skaters' 100/250/400
   (0dfa7c). 0c9d50 hard cuts, written as near==fade==far: PatCar 0800ac,
   Bike 08389c, Benz 0ad290, helicopter 0a63bc, tank 0a243c and Denki stop
   at 500. far<=0 means no source gate: the sniper (09b77c) uses the small
   task circle and mines have no actor. */
typedef struct {float near,fade,far,centre[3];} GangViewGate;
static inline GangViewGate gang_view_gate(const Gang *g)
{
    switch(g->mode) {
    case 1:return (GangViewGate){100.f,250.f,400.f,{0.f,10.342f,0.f}};
    case 6:return (GangViewGate){500.f,500.f,500.f,{0.f,10.342f,0.f}};
    case 9:return (GangViewGate){100.f,300.f,600.f,{0.f,8.68270016f,0.f}};
    case 2:return (GangViewGate){100.f,300.f,600.f,{0.f,7.89650011f,0.944500029f}};
    case 7:return (GangViewGate){150.f,300.f,700.f,{0.f,8.682700157f,-.4147999883f}}; /*099ed4 /1c777c,1c7714*/
    case 5:return (GangViewGate){400.f,400.f,500.f,{0.f,10.3420000076f,0.f}}; /*09c2d0 ->06dcf0 /1c8568*/
    case 3:
        if(g->kind==11)return (GangViewGate){500.f,500.f,500.f,{0.f,5.22945976f,-14.7595596f}};
        if(g->kind==12)return (GangViewGate){500.f,500.f,500.f,{0.f,4.73820019f,1.10000002f}};
        return (GangViewGate){500.f,500.f,500.f,{0.f,5.60960007f,-13.9448004f}};
    case 4:return (GangViewGate){500.f,500.f,500.f,{0.f,0.f,0.f}};
    case 8:return (GangViewGate){500.f,500.f,500.f,{0.f,13.6167002f,0.83099997f}};
    case 10:return (GangViewGate){0.f,0.f,0.f,{0.f,0.f,0.f}};
    default:return (GangViewGate){100.f,300.f,600.f,{0.f,8.5f,0.f}};
    }
}
#if JGR_PORTABLE_ASSETS
extern float GANG_PAR_A[24] __asm__("jgr_ptr_bomb_source_GANG_PAR_A");
extern float GANG_PAR_B[24] __asm__("jgr_ptr_bomb_source_GANG_PAR_B");
#else
extern const float GANG_PAR_A[24], GANG_PAR_B[24];
#endif
void gang_spawn(Gang *g, int kind, int type, const float *pos, float yaw, int nmem, const GGraph *graph, int trig, const float *quad);
void gang_spawn_solo(Gang *g, int kind, const float *pos, float yaw, int entries, int trig, const float *quad);   /* types 18/19/20 */
void gang_solo_view(Gang *,int,const float view[16],int fov,float aspect);
void gang_spawn_patrol(Gang *g, int kind, const float (*wp)[3], int nwp, float yaw, int trig, const float *quad);   /* type 17: Onishima patrolling source 16-byte waypoints (FUN_8c085ba0) */
void gang_spawn_mover(Gang *g, int kind, const float (*wp)[3], const float *speed, const int *action, int nwp, int trig, const float *quad);   /* types 11/12/14: FUN_8c072adc; source 20-byte waypoint loop */
void gang_spawn_heli(Gang *g, int kind, const float (*wp)[3], const float *speed, const float *route_attr, const int *flags, int nwp, int trig, const float *quad);   /* types 13/15/32: FUN_8c0a5da4; source 24-byte 3D route */
void gang_spawn_sniper(Gang *g, const float *pos, float yaw, int rank, int trig, const float *quad);   /* type 16: FUN_8c09beb8/FUN_8c09c158 */
void gang_spawn_denki(Gang *g, const float *pos, float yaw, int trig, const float *quad);   /* type 22: FUN_8c0fd0c4/FUN_8c0fd374 */
void gang_spawn_rocket(Gang *g, int kind, const float (*wp)[3], const float *speed, const float *route_attr, const int *flags, int nwp, int trig, const float *quad);   /* types 8/24: FUN_8c0997b4/FUN_8c0970b4 */
void gang_spawn_tank(Gang *g, const float (*wp)[3], const float *speed, const float *route_attr, const int *flags, int nwp, int trig, const float *quad);   /* type 10: FUN_8c0a135c/FUN_8c0a3844 */
void gang_spawn_taban(Gang *g, const float *pos, float yaw, const GGraph *graph, int trig, const float *quad);   /* type 21: FUN_8c0f92b8/FUN_8c0f9580 */
void gang_spawn_mines(Gang *,const unsigned char *,unsigned);
void gang_backtag(Gang *g, const float *ppos, float pyaw, int spray_pressed);   /* R presses close behind a mode-1 skater (0x1d per press, done at 0x120) */
int gang_take_legacy_tag(Gang *g); /* one-shot shell tally; not original graffiti-object completion */
void gang_tick(Gang *g, const Graphs *G, int area, const float *ppos, float pspeed, int player_off_graph, int police_attack, float (*ground)(void *, float, float, float), void *gctx);
extern int (*gang_wall)(void *ctx, const float *from, const float *to);   /* optional: 1 when the segment hits a collider (FUN_8c06df9c avoidance stand-in) */
/* 0b648c adapter: queue result is consumed by the NEXT helicopter update.
   mask is the original job +24 word (1 landing, 3 approach). */
extern int (*gang_heli_probe)(void *,const float *,const float *,unsigned,float *);
/* 0c4d0c point query: 0 none, 1 unpainted, 2 revealing/completed. */
extern int (*gang_landing_tag)(void *,const float *,float *);
int  gang_reports_police(const Gang *g);   /* state 7 without a leader: FUN_8c0d8080 report */
void gang_parent_finish(unsigned *flags,int state,int children); /* 070454: completion once the child chain is empty */
enum { GANG_TAG_REGISTER=1,GANG_TAG_DRAW=2 };
unsigned gang_graffiti_policy(const Gang *,const GangMember *);
unsigned gang_graffiti_member_tick(const Gang *,GangMember *);
int gang_graffiti_arrow(const Gang *,const GangMember *); /* -1 absent, 0 green, 1 red */
/* Families with the original per-member controller and channel poses. */
static inline int gang_source_member(const Gang *g)
{return g->kind==6 || g->kind==9 || g->kind==25 || g->kind==5 || g->kind==23 || g->kind==0 || g->kind==2 || g->kind==3 || g->kind==29 || g->kind==4 || g->type==7;}
int gang_taban_motion(const Gang *);
int gang_taban_gait_motion(void);
/* Taban/Kidoutai consult the live player graph, not their spawn graph. */
static inline void gang_player_graph(Gang *g,const GGraph *current)
{
    int routed=g->kind==3 || g->kind==4 || g->kind==6 || g->kind==9 || g->kind==25 || g->kind==21;
    if(routed && current!=g->graph) {
        /* The source stores linked routes; our route stores node indices.
           Invalidate them when the player crosses into another graph. */
        g->npath=g->pathi=g->route_phase=0;g->node=-1;
        if(g->kind==6){if(g->state==3 || g->state==4)g->state=2;}
        else if(g->state>=4 && g->state<=6)g->state=3;
    }
    if(!g->graph || routed)g->graph=current;
}
int gang_riot_overlay(const GangMember *);
float gang_riot_frame(const GangMember *);
float gang_riot_overlay_frame(const GangMember *);
int gang_police_motion(const GangMember *);
void gang_fire_overlay(const GangMember *,int *,float *,int *,float *);
int gang_kairiki_body(const GangMember *);
int gang_police_gait_motion(const GangMember *);
float gang_dog_turn_frame(const GangMember *);
void gang_member_matrix(const Gang *,const GangMember *,float root[16]); /* row-major T*Y*-X*-Z */
#endif
