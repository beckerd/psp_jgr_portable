#ifndef NPC_H
#define NPC_H
#include "collide.h"
#include "charanim.h"
#include "stage.h"
#include "npc_occlusion.h"
#include "enemy_squad.h"
#include "enemy_gun.h"
#include "enemy_tank.h"
/* Pedestrians from the mission-set spawn list (FUN_8c0899b4 records, tools/jgs_peds.py -> pedsN.bin):
   each walks its waypoint list back and forth at the route-class speed (units per 30 Hz tick). */
#define MAX_PED 320
#define MAX_WP 32
#define CH_POLICE 16
#define CH_RIOT 17
#define CH_DOG 18
#define CH_PED0 19
#define N_PED 3
typedef struct {
    float x, y, z, yaw, speed;
    int type, route, flags, nwp, cur, dir, ch, pmodel, knock_motion;
    union {
        float wp[MAX_WP][3];
        struct {float reserved[6],matrix[16];} parked; /* no extra per-actor storage */
    };
    float frame, knocked,spawn_yaw;
    int active, near, list;
    unsigned group_owner; /* source event's parent; pedestrians/waves remain unowned */
    unsigned combat_flags;union {EnemyGun gun;EnemyTank tank;};
    int squad,seen,visible; /* seen = passed initial offscreen wait, NOT ever drawn */
    int armed; float atk_timer; int atk_left;
    int anim, pose;                 /* group member +0x14c animation substate / JMSc entry +0x18 */
    union { float life; float opacity; }; /* wave lifetime / street draw alpha */
    unsigned sound; /* common sound task token, survives Ped array relocation */
    uint32_t ambient_clock;float audio_depth;int ambient_reference;
    float view_alpha; /* formation members: 0 untested, <0 hidden by the 06dd7c gate, else draw alpha */
    unsigned char view_mode; /* same 06dd7c result for preparation and drawing */
} Ped;   /* pmodel indexes the stage peopleN.jgc bank; squad anim = FUN_8c092d34/093c70/093cfc/093ea8; life = wave lineup ticks left */
struct GangCombat;
typedef struct {
    EnemySquadLeader leader;unsigned owner,*parent_flags;
    int record,type,trigger,first,count,clock,cursor,spacing,budget,command_phase;
    float pos[3],yaw,quad[8];
} NpcSquad;
typedef struct { struct GangCombat *combat; Ped *p; int count; int rival_base; int hits; float hit_dx, hit_dz; float px, pz; int police_attack;
    Chars police_vehicles; /* stationary car/van/Benz and source textures; loaded only when used */
    void (*sound_event)(void *,Ped *,int);void *sound_context;
    float audio_accumulator;
    NpcSquad *squads;int nsquads,on_graph;
    /* Current draw only, independent of gameplay visibility/audio state. */
    unsigned char occluded[(MAX_PED+7)/8];
    const Ped *occlusion_people;const Chars *occlusion_bank;const Stage *occlusion_stage;int occlusion_count;
} Npcs; /* police_attack = 0x8c2fc8b0; list>=1000 is not a street-person actor */
enum {NPC_SOUND_UPDATE,NPC_SOUND_REACTION,NPC_SOUND_DELETE,NPC_SOUND_AMBIENT};
void npcs_audio_view(Npcs *,const float view[16]); /* previous rendered view depth */
void npcs_view(Npcs *,const float view[16],int fov,float aspect); /* street visibility, once before pose preparation */
int  npcs_load(Npcs *s, const char *path, const Collision *col, int all_active, int stage_id);   /* PED3; all_active=0 leaves lists to npcs_set_list (mission events 6/7) */
void npcs_set_list(Npcs *s, int list, int on);
int npcs_group_spawn(Npcs *,int record,unsigned owner,unsigned *parent_flags);
void npcs_group_signal(Npcs *,unsigned owner,int leave);
void npcs_group_delete(Npcs *,unsigned owner);
int npcs_group_active(const Npcs *,unsigned owner);
int  npcs_load_rivals(Npcs *s, const char *path, const Collision *col);
int  npcs_load_groups(Npcs *s, const char *path, const Collision *col); /* all GRP1 subgroups, anchor/member types 26..30 */
int npcs_load_vehicle_models(Npcs *s);
int  npcs_load_waves(Npcs *s, const char *path, const Collision *col);   /* WAV1: police arrival lineups, spawned by event 0x21 (list 2000+idx) */
void npcs_wave(Npcs *s, int list, int segment);   /* 0dd540: current route segment, never nearest position */
int  npcs_active(const Npcs *s);
void npcs_update(Npcs *s, const Collision *col, float dt, float px, float py, float pz, float pspeed);   /* sets s->hits (attacks that connected) and the push direction */
void npcs_prepare_poses(Npcs *s,Chars *actors,Chars *people);
void npcs_prepare_occlusion(Npcs *s,const Chars *people,const Stage *stage,
                           const Collision *collision,const float eye[3]);
void npcs_draw(Npcs *s, Chars *actors, Chars *people,const float view[16],float focal);
void npcs_prepare_detail(Npcs *s,Chars *actors,Chars *people);
void npcs_free(Npcs *s);
#endif
