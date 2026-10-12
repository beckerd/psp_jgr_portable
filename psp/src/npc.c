#include "diagnostics.h"
#include <pspkernel.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "npc.h"
#include "tank_orders.h"
#include "gang.h"
#include "asset_progress.h"
#include "player.h"
#include "group_data.h"
#include "npc_audio.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_npc_audio_tables.inc"
#else
#include "generated/source_data/npc_audio_tables.inc"
#endif
#include "npc_visibility.h"
#include "source_trs.h"

typedef char NpcProxyBudgetMatches[(NPC_OCCLUSION_MAX_PROXIES==STAGE_PED_PROXY_MAX) ? 1:-1];
static int pedestrian_occlusion_enabled=NPC_OCCLUSION_PROXIES;
static NpcOcclusionProfile occlusion_profile,occlusion_totals;
#define NPC_OCC_ADD(field,value) do{occlusion_profile.field+=(value);occlusion_totals.field+=(value);}while(0)
void npcs_occlusion_mode(int mode)
{pedestrian_occlusion_enabled=mode==NPC_OCCLUSION_PROXIES || mode==NPC_OCCLUSION_AGGRESSIVE ? mode:!!mode;}
int npcs_occlusion_enabled(void){return pedestrian_occlusion_enabled;}
void npcs_occlusion_totals(NpcOcclusionProfile *out){*out=occlusion_totals;}
void npcs_occlusion_profile(NpcOcclusionProfile *out)
{*out=occlusion_profile;memset(&occlusion_profile,0,sizeof occlusion_profile);}
void npcs_occlusion_preparation_us(unsigned elapsed)
{NPC_OCC_ADD(query_us,elapsed);}
void npcs_prepare_occlusion(Npcs *s,const Chars *people,const Stage *stage,
                           const Collision *collision,const float eye[3])
{
    if(!s)return;
    memset(s->occluded,0,sizeof s->occluded);
    s->occlusion_people=s->p;s->occlusion_bank=people;s->occlusion_stage=stage;s->occlusion_count=s->count;
    int aggressive=pedestrian_occlusion_enabled==NPC_OCCLUSION_AGGRESSIVE;
    int proxies=pedestrian_occlusion_enabled==NPC_OCCLUSION_PROXIES;
    NPC_OCC_ADD(frames,1);NPC_OCC_ADD(enabled_frames,pedestrian_occlusion_enabled!=0);
    NPC_OCC_ADD(aggressive_frames,aggressive);
    NPC_OCC_ADD(proxy_frames,proxies);
    unsigned started=0;
#if defined(__PSP__) && JGR_DIAGNOSTICS
    started=sceKernelGetSystemTimeLow();
#endif
    unsigned cones=proxies ? stage_ped_proxy_count(stage):stage_actor_occluders(stage);
    NPC_OCC_ADD(occluders,cones);
    int ready=pedestrian_occlusion_enabled==NPC_OCCLUSION_CONSERVATIVE && cones>0;
    for(int i=0;i<s->count;i++) {
        const Ped *p=s->p+i;
        if(!p->active || !p->near || p->list>=1000 || p->pmodel<0 ||
           (p->squad && !p->seen) || !people || !people->base)continue;
        NPC_OCC_ADD(candidates,1);
        if(proxies && i<MAX_PED && cones) {
            /* The selected blockers represent bus bodies and roof surfaces.
               A torso inside an actual blocker shadow skips preparation and
               draw together. Missing/ambiguous coverage always stays visible. */
            const float torso[3]={p->x,p->y+8.5f,p->z};unsigned tests=0;
            int hidden=stage_ped_proxy_occluded(stage,torso,&tests);
            NPC_OCC_ADD(tested,1);NPC_OCC_ADD(proxy_tests,tests);
            if(hidden) {
                s->occluded[i>>3]|=(unsigned char)(1u<<(i&7));
                NPC_OCC_ADD(proxy_blocked,1);NPC_OCC_ADD(hidden,1);continue;
            }
        }
        if(aggressive && i<MAX_PED) {
            /* Retained only for the historical feature13 diagnostic.
               This budget-dependent policy is never the gameplay default. */
            const float torso[3]={p->x,p->y+8.5f,p->z};
            ColRenderOcclusionStats work={0};
            int result=col_render_occlusion(collision,eye,torso,&work);
            NPC_OCC_ADD(tested,1);NPC_OCC_ADD(rays,1);
            NPC_OCC_ADD(ray_triangles,work.triangle_tests);NPC_OCC_ADD(ray_steps,work.steps);
            NPC_OCC_ADD(ray_blocked,result==COL_RENDER_BLOCKED);
            NPC_OCC_ADD(ray_uncertain,result==COL_RENDER_UNCERTAIN);
            NPC_OCC_ADD(ray_unavailable,result==COL_RENDER_UNAVAILABLE);
            if(result==COL_RENDER_BLOCKED || result==COL_RENDER_UNCERTAIN) {
                s->occluded[i>>3]|=(unsigned char)(1u<<(i&7));
                NPC_OCC_ADD(hidden,1);continue;
            }
        }
        /* The all-motion box contains its root. If even that point is
           exposed, avoid building a yaw matrix and testing a larger box. */
        const float origin[3]={p->x,p->y,p->z},zero[3]={0};
        if(ready && i<MAX_PED && stage_actor_occluded(stage,origin,zero)) {
            float co=cosf(p->yaw),si=sinf(p->yaw);
            float root[16]={co,0,si,p->x,0,1,0,p->y,-si,0,co,p->z,0,0,0,1};
            float center[3],extent[3];
            if(chars_actor_visible(people,p->pmodel,root) &&
               chars_actor_bounds(people,p->pmodel,root,center,extent)) {
                NPC_OCC_ADD(tested,1);
                if(stage_actor_occluded(stage,center,extent)) {
                    s->occluded[i>>3]|=(unsigned char)(1u<<(i&7));
                    NPC_OCC_ADD(hidden,1);continue;
                }
            }
        }
        NPC_OCC_ADD(kept,1);
    }
#if defined(__PSP__) && JGR_DIAGNOSTICS
    NPC_OCC_ADD(query_us,sceKernelGetSystemTimeLow()-started);
#else
    (void)started;
#endif
}
static int pedestrian_hidden(const Npcs *s,const Chars *people,int index)
{
    return pedestrian_occlusion_enabled && (unsigned)index<MAX_PED &&
        s->occlusion_people==s->p && s->occlusion_bank==people &&
        s->occlusion_count==s->count && (s->occluded[index>>3]&(1u<<(index&7)));
}

static int parked_police(const Ped *p){return (p->type==26 || p->type==27 || p->type==29) && p->pose!=0;}
static float ped_view_alpha(const Ped *p){return p->view_alpha<0.f ? 0.f:p->view_alpha>0.f ? p->view_alpha:1.f;}
static void parked_police_matrix(Ped *p,const float rotation[3])
{
    /* 092594 / 0af51c -> 093260: keep authored height, pitch/roll and yaw+8000.
       A one-position formation member has spare waypoint storage. Cache
       its fixed matrix there, as the original task does on its first tick. */
    int32_t angles[3];for(int i=0;i<3;i++)angles[i]=(int32_t)lrintf(rotation[i]*(65536.f/6.283185307179586f));
    angles[1]+=32768;
    source_trs((float[3]){p->x,p->y,p->z},angles,(float[3]){1,1,1},p->parked.matrix);
}
int npcs_load_vehicle_models(Npcs *s)
{
    for(int i=0;i<s->count;i++)if(parked_police(s->p+i)) {
        if(s->police_vehicles.h)return 0;
        int error=chars_load(&s->police_vehicles,"policevehicles.jgc");
        return error;
    }
    return 0;
}

static float ground_y(const Collision *col, float x, float y, float z)
{ ColHit h; return col_ground(col, x, y + 20.f, z, 0.f, 60.f, &h) ? h.p[1] : y; }

/* Runtime model tables populated by people1/2/3/5dat.  Stages 1, 2 and 5
   were read directly from 0x8c1b6718/68bc/6d74 after the original loader.
   Kogane/Benten retain the listed people1 entries in the global type table;
   their PSP banks append those exact shared roots after the local models. */
static int person_model(int stage, int type)
{
    static const signed char st1[24] = {
         0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,-1,-1,-1,14,15,16,17,18,19,20
    };
    static const signed char st2_type[18] = {24,25,26,27,28,29,30,31,37,38,45,46,47,49,52,53,54,55};
    static const signed char st3_type[23] = {17,18,19,20,21,22,23,30,31,32,33,34,35,36,39,40,41,48,50,51,56,57,58};
    if (stage == 1 && type >= 0 && type < 24) return st1[type];
    if (stage == 1 && type == 43) return 21;
    if (stage == 1 && type == 44) return 22;
    if (stage == 2) {
        static const signed char shared_type[5] = {0, 1, 4, 5, 6};
        for (int i = 0; i < 18; i++) if (st2_type[i] == type) return i;
        for (int i = 0; i < 5; i++) if (shared_type[i] == type) return 18 + i;
        if (type == 44) return 23;
    }
    if (stage == 3) {
        static const signed char shared_type[4] = {3, 4, 6, 12};
        for (int i = 0; i < 23; i++) if (st3_type[i] == type) return i;
        for (int i = 0; i < 4; i++) if (shared_type[i] == type) return 23 + i;
    }
    if ((stage == 5 || stage == 6) && type >= 59 && type <= 75) return type - 59;
    return -1;
}

/* people*.jgc stores the 31 motions reachable by an 11-node person:
   source normal[0..21,23,24], then special[0,1,2,5,6,7,8]. */
static int route_motion(int route)
{
    if (route >= 0 && route <= 21) return route;
    if (route == 23) return 22;
    if (route == 24) return 23;
    return 0;   /* source route 22 belongs to the separate three-node 0x2b/0x2c actors */
}

/* The common formation member has four animation substates selected by
   0x8c1ba190: idle, wind-up, attack, recover.  These are the exact local JGC
   slots obtained by relocating the source motion pointer tables used by the
   corresponding initializers (tools/enemy_motion_map.py). */
typedef struct { signed char motion[4], frames[4]; } SquadAnim;

#if JGR_PORTABLE_ASSETS
#include "source_data_npc_squad_anim.inc"
#else

#include "generated/source_data/npc_squad_anim.inc"

#endif

#define ANIM_KEIKAN SQUAD_ANIMS[0]
#define ANIM_KIDOUTAI SQUAD_ANIMS[1]
#define ANIM_PARA SQUAD_ANIMS[2]
#define ANIM_KILLER SQUAD_ANIMS[3]


static const SquadAnim *squad_anim(const Ped *p)
{
    if (p->ch == CH_POLICE) return &ANIM_KEIKAN;
    if (p->ch == CH_RIOT) return &ANIM_KIDOUTAI;
    if (p->ch == 27) return &ANIM_PARA;
    if (p->ch == 30) return &ANIM_KILLER;
    return 0;
}
static int squad_set_anim(Ped *p, int state)
{ if (p->anim == state) return 0; p->anim = state; p->frame = 0.f; return 1; }

/* FUN_8c092594 / 8c0af51c / 8c0af04c / 8c0ae97c / 8c0a1560 all consume
   entry zero as the leader anchor and create members from entries [1,count). */
static int rival_has_anchor(int type) { return type >= 26 && type <= 30; }
static int rival_is_common_member(const Ped *p)
{ return p->type == 28 || ((p->type == 26 || p->type == 27 || p->type == 29) && p->pose == 0); }

/* Exact model family selected by each creator.  Types 26/27/29 branch on the
   entry's final word: zero creates the shared animated member; non-zero
   creates that family's alternate object. */
static int rival_model(int type, unsigned pose)
{
    switch (type) {
    case 2: case 22: return CH_POLICE;
    case 8: return 46;                         /* 015_rocketdat root 2 */
    case 10: return 29;                        /* Tank, FUN_8c0a16f4 table 0x8c1b6160 */
    case 24: return 28;                        /* 015_rocketdat root 0 */
    case 26: return pose ? 26 : CH_POLICE;     /* patrol car / officer */
    case 27: return pose ? 26 : CH_RIOT;       /* 0af8b4: same parked car/bus roots as 0929bc */
    case 28: return 27;                        /* para, table 0x8c1b5e8c */
    case 29: return pose ? 36 : 30;            /* 0aed90 stationary Benz / Killer */
    case 30: return 29;                        /* tank, table 0x8c1b6160 */
    default: return CH_RIOT;
    }
}

typedef struct { int attacks, interval, initial, windup; } SquadPar;
static SquadPar squad_par(const Ped *p)
{
    /* Member blocks selected by the four source creators.  +0xc is the shot
       count, +0x14 the reload countdown, +0x1c the initial countdown and
       +0x20 the point at which the wind-up substate is requested. */
    if (p->type == 27) { SquadPar q = {3, 90, 0, 0}; return q; }       /* 0x8c1c9d00 */
    if (p->type == 28) { SquadPar q = {5, 3, 105, 45}; return q; }     /* 0x8c1c9c28 */
    if (p->type == 29) { SquadPar q = {5, 6, 105, 45}; return q; }     /* 0x8c1c9b88 */
    { SquadPar q = {2, 120, 105, 54}; return q; }                     /* 0x8c1ba120 */
}
static int squad_sound_kind(const Ped *p)
{return p->type==26?0:p->type==27?1:p->type==28?7:p->type==29?3:p->type==30?10:p->type;}

static void knock_begin(Npcs *s,Ped *p, float px, float pz)
{
    float dx = px - p->x, dz = pz - p->z;
    int side = dx * cosf(p->yaw) - dz * sinf(p->yaw) < 0.f;
    p->knock_motion = (p->type == 43 || p->type == 44) ? 1 : p->type == 45 ? 27 + side : p->type == 46 ? 29 + side : 24 + side;
    p->knocked = 120.f; p->frame = 0.f;
    /* 08aa08/08acc8 -> 096248, on reaction entry only. This binds the
       source cue, not a claim that the legacy proximity collision is exact. */
    if(p->list<1000 && s->sound_event)s->sound_event(s->sound_context,p,NPC_SOUND_REACTION);
}

int npcs_load(Npcs *s, const char *path, const Collision *col, int all_active, int stage_id)
{
    memset(s, 0, sizeof *s);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777); if (fd < 0) return -1;
    char hdr[8];
    if(sceIoRead(fd,hdr,8)!=8){sceIoClose(fd);return -2;}
    int n;memcpy(&n,hdr+4,4);
    if (memcmp(hdr, "PED3", 4) || n < 0 || n > 4096) { sceIoClose(fd); return -2; }
    if(!n){sceIoClose(fd);return 0;}
    if (n > MAX_PED) n = MAX_PED;
    s->p = memalign(16, n * sizeof(Ped));if(!s->p){sceIoClose(fd);return -3;}memset(s->p, 0, n * sizeof(Ped));
    for (int i = 0; i < n; i++) {
        Ped *p = &s->p[i]; unsigned v[2], extra[2]; float f[2];
        sceIoRead(fd, v, 8);
        p->type = v[0]; int nwp = v[1];
        sceIoRead(fd, extra, 8); p->route = extra[0]; p->flags = extra[1];
        sceIoRead(fd, f, 8);
        p->yaw = f[0]; p->speed = f[1] >= 0.f ? f[1] : 0.278f;
        unsigned li; sceIoRead(fd, &li, 4); p->list = li;
        for (int k = 0; k < nwp; k++) { float w[3]; sceIoRead(fd, w, 12); if (k < MAX_WP) { p->wp[k][0] = w[0]; p->wp[k][1] = w[1]; p->wp[k][2] = w[2]; } }
        p->nwp = nwp < MAX_WP ? nwp : MAX_WP;
        p->x = p->wp[0][0]; p->y = p->wp[0][1]; p->z = p->wp[0][2]; p->cur = 0; p->dir = 1; p->active = !!all_active;
        p->pmodel = person_model(stage_id, p->type);
        p->ch = (p->type >= 43 && p->type <= 46) ? CH_POLICE : CH_PED0 + (p->type % N_PED);
        /* 08a814 copies the authored first waypoint, including height. */
        p->spawn_yaw=p->yaw;p->visible=1; /* wait for the first actual view test */
        asset_progress();
        s->count++;
    }
    sceIoClose(fd); return 0;
}
/* Rival / police squads (set +0x18 records): the common members stand at
   their record positions, turn toward the player, and run the source's
   wind-up / attack / recover controller around its hit-scan attack object. */
int npcs_load_rivals(Npcs *s, const char *path, const Collision *col)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777); if (fd < 0) return -1;
    char hdr[8]; sceIoRead(fd, hdr, 8); int n = *(int *)(hdr + 4);
    if (memcmp(hdr, "RIV1", 4) || n < 0 || n > 64) { sceIoClose(fd); return -2; }
    int total = 0; long pos0 = sceIoLseek(fd, 0, PSP_SEEK_CUR);
    for (int r = 0; r < n; r++) { unsigned v[2]; sceIoRead(fd, v, 8); total += v[1]; sceIoLseek(fd, v[1] * 20, PSP_SEEK_CUR); }
    Ped *np = memalign(16, (s->count + total) * sizeof(Ped)); if (!np) { sceIoClose(fd); return -3; }
    if (s->p) { memcpy(np, s->p, s->count * sizeof(Ped)); free(s->p); } s->p = np; s->rival_base = s->count;
    sceIoLseek(fd, pos0, PSP_SEEK_SET);
    for (int r = 0; r < n; r++) {
        unsigned v[2]; sceIoRead(fd, v, 8);
        for (unsigned k = 0; k < v[1]; k++) {
            float e[4]; unsigned pose; sceIoRead(fd, e, 16); sceIoRead(fd, &pose, 4);
            if (k == 0 && rival_has_anchor((int)v[0])) continue;
            if (s->count >= s->rival_base + total) continue;
            Ped *p = &s->p[s->count]; memset(p, 0, sizeof *p);
            p->type = v[0]; p->x = e[0]; p->y = ground_y(col, e[0], e[1], e[2]); p->z = e[2]; p->yaw = e[3]; p->nwp = 1; p->wp[0][0] = p->x; p->wp[0][1] = p->y; p->wp[0][2] = p->z;
            p->speed = 0.f; p->list = 1000 + r; p->active = 0; p->pmodel = -1; p->pose = (int)pose; p->ch = rival_model((int)v[0], pose);
            if(parked_police(p)){p->y=p->wp[0][1]=e[1];parked_police_matrix(p,(float[3]){0,p->yaw,0});}
            asset_progress();
            s->count++;
        }
    }
    sceIoClose(fd); return 0;
}
int npcs_load_groups(Npcs *s,const char *path,const Collision *col)
{
    FILE *f=fopen(path,"rb");if(!f)return -1;
    if(fseek(f,0,SEEK_END)){fclose(f);return -2;}
    long size=ftell(f);
    if(size<8 || size>1024*1024 || fseek(f,0,SEEK_SET)){fclose(f);return -2;}
    unsigned char *data=malloc((size_t)size);
    if(!data){fclose(f);return -3;}
    int complete=fread(data,1,(size_t)size,f)==(size_t)size;fclose(f);
    if(!complete || memcmp(data,"GRP1",4) || group_u32(data+4)>64){free(data);return -2;}
    unsigned records=group_u32(data+4),total=0,leaders=0;
    for(unsigned r=0;r<records;r++) {
        GroupRecord rec;
        if(group_record_read(data,(unsigned)size,(int)r,&rec)){free(data);return -2;}
        total+=rec.members;leaders+=rec.fallback;
    }
    /* Validate the entire bank before replacing any live NPC allocation. */
    if(!total && !leaders){free(data);s->rival_base=s->count;return 0;}
    if(s->count<0 || s->nsquads<0 || (unsigned)s->count+total>8192 || (unsigned)s->nsquads+leaders>1024){free(data);return -2;}
    NpcSquad *squads=calloc(s->nsquads+leaders,sizeof *squads);
    if(s->nsquads+leaders && !squads){free(data);return -3;}
    if(s->nsquads)memcpy(squads,s->squads,s->nsquads*sizeof *squads);
    Ped *np=memalign(16,((size_t)s->count+total+1)*sizeof *np);
    if(!np){free(squads);free(data);return -3;}
    if(s->count)memcpy(np,s->p,(size_t)s->count*sizeof *np);
    free(s->p);s->p=np;s->rival_base=s->count;
    free(s->squads);s->squads=squads;
    for(unsigned r=0;r<records;r++) {
        GroupRecord rec;group_record_read(data,(unsigned)size,(int)r,&rec);
        const unsigned char *q=rec.subs;
        for(unsigned sub=0;sub<rec.count;sub++) {
            unsigned type=group_u32(q),entries=group_u32(q+40);
            const unsigned char *ent=q+44;q=ent+entries*28;
            if(!rival_has_anchor((int)type))continue;
            NpcSquad *leader=s->squads+s->nsquads++;
            leader->record=(int)r;leader->type=(int)type;leader->trigger=(int)group_u32(ent-40);
            memcpy(leader->quad,ent-36,32);leader->first=s->count;leader->count=entries?entries-1:0;
            if(entries){memcpy(leader->pos,ent,12);memcpy(&leader->yaw,ent+16,4);}
            /* Each subrecord has its OWN non-rendered anchor at entry zero. */
            for(unsigned k=1;k<entries;k++) {
                const unsigned char *e=ent+k*28;float pos[3],yaw;
                memcpy(pos,e,12);memcpy(&yaw,e+16,4);
                Ped *p=&s->p[s->count++];memset(p,0,sizeof *p);
                p->type=(int)type;p->pose=(int)group_u32(e+24);
                p->squad=s->nsquads;
                p->x=pos[0];p->y=ground_y(col,pos[0],pos[1],pos[2]);p->z=pos[2];p->yaw=yaw;
                if(type==30)p->yaw+=3.14159265359f; /* 0a1560: authored yaw +8000 */
                p->spawn_yaw=p->yaw;
                asset_progress();
                p->nwp=1;p->wp[0][0]=p->x;p->wp[0][1]=p->y;p->wp[0][2]=p->z;
                p->list=1000+(int)r;p->pmodel=-1;p->ch=rival_model((int)type,(unsigned)p->pose);
                if(parked_police(p)){float rotation[3];memcpy(rotation,e+12,12);p->y=p->wp[0][1]=pos[1];parked_police_matrix(p,rotation);}
            }
        }
    }
    free(data);return 0;
}
int npcs_active(const Npcs *s) { int n = 0; for (int i = 0; i < s->count; i++) n += s->p[i].active != 0; return n; }
void npcs_set_list(Npcs *s, int list, int on)
{ for (int i = 0; i < s->count; i++) if (s->p[i].list == list) { Ped *p = &s->p[i]; if (on && !p->active) { p->x = p->wp[0][0]; p->y = p->wp[0][1]; p->z = p->wp[0][2]; p->cur = 0; p->dir = 1; if(p->list<2000)p->yaw=p->spawn_yaw; p->knocked = 0; p->armed = 0; p->anim = 0; p->frame = 0.f; p->ambient_clock=0;p->audio_depth=0; if(p->list<1000){p->seen=0;p->visible=1;p->near=0;p->opacity=0;} } p->active = on; } }
int npcs_group_spawn(Npcs *s,int record,unsigned owner,unsigned *parent_flags)
{
    int found=0;
    for(int i=0;i<s->nsquads;i++)if(s->squads[i].record==record) {
        NpcSquad *q=s->squads+i;if(q->owner)return -1;found++;
    }
    npcs_set_list(s,1000+record,1);
    for(int i=0;i<s->nsquads;i++)if(s->squads[i].record==record) {
        NpcSquad *q=s->squads+i;q->owner=owner;q->parent_flags=parent_flags;
        q->leader=(EnemySquadLeader){0};q->clock=q->cursor=q->spacing=q->budget=q->command_phase=0;
        for(int j=0;j<q->count;j++) {
            Ped *p=s->p+q->first+j;p->group_owner=owner;p->combat_flags=0;
            p->seen=p->visible=0;p->gun=(EnemyGun){0};
        }
    }
    return found;
}
void npcs_group_signal(Npcs *s,unsigned owner,int leave)
{for(int i=0;i<s->nsquads;i++)if(s->squads[i].owner==owner)s->squads[i].leader.flags|=leave?0x10:0x1000;}
void npcs_group_delete(Npcs *s,unsigned owner)
{
    for(int i=0;i<s->nsquads;i++)if(s->squads[i].owner==owner) {
        NpcSquad *q=s->squads+i;q->owner=0;q->parent_flags=NULL;
        for(int j=0;j<q->count;j++){Ped *p=s->p+q->first+j;p->active=0;p->group_owner=0;}
    }
}
int npcs_group_active(const Npcs *s,unsigned owner)
{int n=0;for(int i=0;i<s->nsquads;i++)n+=s->squads[i].owner==owner && !(s->squads[i].leader.flags&0x20);return n;}
static int squad_triangle(float x,float z,const float *a,const float *b,const float *c)
{
    float ab=(a[0]-x)*(b[1]-z)-(a[1]-z)*(b[0]-x);
    float bc=(b[0]-x)*(c[1]-z)-(b[1]-z)*(c[0]-x);
    float ca=(c[0]-x)*(a[1]-z)-(c[1]-z)*(a[0]-x);
    return (ab<=0 && bc<=0 && ca<=0)||(ab>=0 && bc>=0 && ca>=0);
}
static int squad_trigger(NpcSquad *q,float x,float z)
{
    int forced=!!(q->leader.flags&0x1000);q->leader.flags&=~0x1000u;
    if(q->trigger==5) { /* 06ef84, original local quad at the anchor transform */
        float dx=x-q->pos[0],dz=z-q->pos[2],si=sinf(q->yaw),co=cosf(q->yaw);
        x=dx*co-dz*si;z=dx*si+dz*co;
    }
    int accepted=forced || ((q->trigger==0 || q->trigger==5) &&
        (squad_triangle(x,z,q->quad,q->quad+2,q->quad+4)||squad_triangle(x,z,q->quad+6,q->quad+2,q->quad+4)));
    if(accepted && q->parent_flags)*q->parent_flags|=1;
    return accepted;
}
static void squads_tick(Npcs *s,float px,float pz)
{
    for(int i=0;i<s->nsquads;i++) {
        NpcSquad *q=s->squads+i;if(!q->owner || (q->leader.flags&0x20))continue;
        int ready=0,linked=0,remaining=0;
        for(int j=0;j<q->count;j++) {
            Ped *p=s->p+q->first+j;
            /* 092328 tests the member list's hidden bit, not whether the
               camera has seen it. Parked vehicles are separate children
               (092594), so a vehicle-only leader has an empty member list. */
            if(!parked_police(p)){linked++;ready|=p->seen;}
            /* 092fb4/0933ac retire on departure only after leaving view.
               Keep visible actors until then; never infer death from shots. */
            if((q->leader.flags&0x10) && (!p->seen || !p->visible))p->active=0;
            remaining+=p->active;
        }
        int trigger=0;
        if(q->leader.state==2 || q->leader.state==3)trigger=squad_trigger(q,px,pz);
        enemy_squad_leader_tick(&q->leader,trigger,s->police_attack,s->on_graph,ready || !linked,remaining);
        if(q->leader.flags&0x20) {
            for(int j=0;j<q->count;j++)s->p[q->first+j].active=0;
            continue;
        }
        if(q->type==30) {
            TankOrders orders={q->clock,q->cursor,q->spacing,q->budget,q->command_phase};
            unsigned members[32];int another=0;
            if(q->count>32)continue; /* GRP1 validates at most 32 source entries. */
            for(int j=0;j<q->count;j++)members[j]=s->p[q->first+j].combat_flags;
            if(orders.state==1 && orders.clock>=90 && (q->leader.flags&2))
                for(int j=0;j<s->nsquads;j++) {
                    const NpcSquad *other=s->squads+j;
                    if(other!=q && other->owner && other->type==30 && (other->leader.flags&2)){another=1;break;}
                }
            float dx=px-q->pos[0],dz=pz-q->pos[2];
            unsigned flags=q->leader.flags;
            tank_orders_tick(&orders,&flags,members,q->count,another,s->police_attack,
                s->on_graph && dx*dx+dz*dz<=400.f*400.f);
            q->leader.flags=flags;
            for(int j=0;j<q->count;j++)s->p[q->first+j].combat_flags=members[j];
            q->clock=orders.clock;q->cursor=orders.cursor;q->spacing=orders.delay;
            q->budget=orders.left;q->command_phase=orders.state;
            continue;
        }
        /* 074408: finite burst budget, staggered eligible members, and
           the source per-kind manager interval. */
        int interval=q->type==27?150:120,spacing=q->type==27?6:2,budget=q->type==27?5:3;
        if(q->type==28 || q->type==29){interval=120;spacing=0;budget=5;}
        q->clock++;
        if(!(q->leader.flags&2) || (q->leader.flags&0x10) || s->police_attack){q->budget=q->command_phase=0;continue;}
        if(q->clock>=interval){
            /* 074378 -> 096178(kind=27,event=2) -> 079af8: Ready/aim.
               The following manager tick enters 074408 (event=3: Fire).
               These are squad commands, not one voice per gas projectile.
               Keep the source's separate entry and repeating-burst cues. */
            if(q->type==27 && !q->command_phase) {
                if(s->combat && s->combat->sound)s->combat->sound(s->combat->context,0x10006,q->pos);
                q->command_phase=1;continue;
            }
            if(q->type==27) {
                if(s->combat && s->combat->sound)s->combat->sound(s->combat->context,0x10007,q->pos);
                q->command_phase=2;
            }
            q->clock=0;q->cursor=0;q->spacing=0;q->budget=budget;
        }
        if(q->budget>0 && ++q->spacing>=spacing) {
            q->spacing=0;
            while(q->cursor<q->count) {
                Ped *p=s->p+q->first+q->cursor++;
                if(p->active && rival_is_common_member(p) && !p->armed && p->anim==0) {
                    p->combat_flags|=0x80000002u;q->budget--;break;
                }
            }
            if(q->cursor>=q->count)q->budget=0;
        }
    }
}

void npcs_audio_view(Npcs *s,const float view[16])
{
    /* 08b608 -> 0c9d50 transforms (0,8.5,0) in the NPC's yaw-only
       root and stores view Z even when the frustum test rejects it.
       PSP view's forward is -Z; source Ninja's forward is +Z. */
    if(!s || !view)return;
    for(int i=0;i<s->count;i++){
        Ped *p=s->p+i;if(!p->active || p->list>=1000)continue;
        p->audio_depth=-(float)((double)view[8]*p->x+(double)view[9]*(p->y+8.5f)+
                               (double)view[10]*p->z+view[11]);
    }
}
void npcs_view(Npcs *s,const float view[16],int fov,float aspect)
{
    if(!s || !view)return;
    NpcVisibility v=npc_visibility_params(fov,player_fsca_sin);
    npc_visibility_aspect(&v,aspect);
    for(int i=0;i<s->count;i++) {
        Ped *p=s->p+i;if(!p->active)continue;
        if(p->list>=1000) {
            /* 092fb4 / 0933ac formation members: every parameter record
               reads 100/300/600 (detailed, low model, linear fade). The tank
               (0a243c) is a 500-unit cut; parked vehicles use their model
               bounds in npcs_render. */
            if(parked_police(p))continue;
            /* Types 26/27/29 with pose 0 are officers, not their parked
               vehicle alternatives. Only the tank uses a hard cut here. */
            int tank=p->type==30;
            float cy=p->y+(tank ? 13.6167002f:8.5f);
            float near=tank ? 500.f:100.f,fade=tank ? 500.f:300.f,far=tank ? 500.f:600.f;
            float depth=-(view[8]*p->x+view[9]*cy+view[10]*p->z+view[11]);
            int mode=source_fade_class(depth,near,fade,far);
            p->view_mode=(unsigned char)mode;
            float alpha=source_fade_opacity(mode,depth,fade,far);
            p->view_alpha=mode && alpha>0.f ? alpha:-1.f;
            continue;
        }
        float y=p->y+8.5f; /* 08b608's local test point, not model bounds */
        float cx=view[0]*p->x+view[1]*y+view[2]*p->z+view[3];
        float cy=view[4]*p->x+view[5]*y+view[6]*p->z+view[7];
        float depth=-(view[8]*p->x+view[9]*y+view[10]*p->z+view[11]);
        p->audio_depth=depth;p->visible=npc_visibility_class(&v,cx,cy,depth);
        p->opacity=npc_visibility_opacity(&v,p->visible,depth);
        p->near=p->seen && p->visible && p->opacity>0.f;
    }
}
static void ambient_tick(Npcs *s,Ped *p)
{
    if(!s->sound_event || (unsigned)p->type>=76 || (unsigned)p->route>=25)return;
    /* The existing actor controller represents source walking/standing
       (2/3); knocked actors represent the ineligible reaction states.
       Full NPC movement/render-visibility state recovery is separate. */
    unsigned state=p->knocked>0 ? 6:p->nwp==1 ? 3:2;
    if(source_npc_ambient(&p->ambient_clock,p->type,state,p->audio_depth,!!p->sound,
                         NPC_AUDIO_TYPES+p->type,NPC_AUDIO_ROUTES+p->route,&p->ambient_reference))
        s->sound_event(s->sound_context,p,NPC_SOUND_AMBIENT);
}

static int g_tick;
/* Legacy formation-member budget. Street people use the source view test. */
#define NPC_RADIUS 320.f
/* Wave lineups (set +0x28): list 2000+index, source route-segment index in
   atk_left. 0dd540 -> 0d820c selects manager +78, NOT the nearest position. */
int npcs_load_waves(Npcs *s, const char *path, const Collision *col)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777); if (fd < 0) return -1;
    char hdr[8]; sceIoRead(fd, hdr, 8); int nl = *(int *)(hdr + 4); if (memcmp(hdr, "WAV1", 4) || nl < 0 || nl > 16) { sceIoClose(fd); return -2; }
    long pos0 = sceIoLseek(fd, 0, PSP_SEEK_CUR); int total = 0;
    for (int l = 0; l < nl; l++) { unsigned n; sceIoRead(fd, &n, 4); total += n * 4; sceIoLseek(fd, n * (64 + 4), PSP_SEEK_CUR); }
    if(!total){sceIoClose(fd);return 0;}
    Ped *np = memalign(16, (s->count + total) * sizeof(Ped)); if (!np) { sceIoClose(fd); return -3; }
    if (s->p) { memcpy(np, s->p, s->count * sizeof(Ped)); free(s->p); } s->p = np;
    sceIoLseek(fd, pos0, PSP_SEEK_SET);
    for (int l = 0; l < nl; l++) {
        unsigned n; sceIoRead(fd, &n, 4);
        for (unsigned e = 0; e < n; e++) {
            float pts[16]; unsigned dur; sceIoRead(fd, pts, 64); sceIoRead(fd, &dur, 4);
            for (int k = 0; k < 4; k++) {
                Ped *p = &s->p[s->count++]; memset(p, 0, sizeof *p);
                p->type = 0x2c; p->x = pts[k * 4]; p->y = ground_y(col, pts[k * 4], pts[k * 4 + 1], pts[k * 4 + 2]); p->z = pts[k * 4 + 2]; p->yaw = pts[k * 4 + 3];
                asset_progress();
                p->nwp = 1; p->wp[0][0] = p->x; p->wp[0][1] = p->y; p->wp[0][2] = p->z; p->list = 2000 + l; p->atk_left = e; p->life = (float)dur; p->pmodel = -1; p->ch = CH_POLICE; p->active = 0;
            }
        }
    }
    sceIoClose(fd); return 0;
}
void npcs_wave(Npcs *s, int list, int segment)
{
    if(segment<0)return;
    for (int i = 0; i < s->count; i++) { Ped *p = &s->p[i]; if (p->list == list && p->atk_left == segment) { p->active = 1; p->knocked = 0; p->frame = 0; p->anim = 0; p->armed = 0; p->atk_timer = (int)p->life; } }
}
void npcs_update(Npcs *s, const Collision *col, float dt, float px, float py, float pz, float pspeed)
{
    unsigned audio_ticks=0;
    if(isfinite(dt) && dt>0){s->audio_accumulator+=dt;while(s->audio_accumulator>=1.f/30.f){s->audio_accumulator-=1.f/30.f;audio_ticks++;}}
    g_tick++; int police_attack = s->police_attack; s->hits = 0; s->px = px; s->pz = pz;
    squads_tick(s,px,pz);
    for (int i = 0; i < s->count; i++) {
        Ped *p = &s->p[i];
        if(p->sound && s->sound_event)s->sound_event(s->sound_context,p,p->active?NPC_SOUND_UPDATE:NPC_SOUND_DELETE);
        if (!p->active) continue;
        if (p->list >= 2000) {   /* wave lineup: shown for the record's duration */
            p->atk_timer -= dt * 30.f; if (p->atk_timer <= 0.f) p->active = 0; p->frame += dt * 30.f; p->near = 1; continue;
        }
        if (p->list >= 1000) {
            if(parked_police(p)) {
                /* 093260 advances only the stationary beacon motion.
                   No pedestrian turning, ground snap or suspension loop. */
                float dx=px-p->x,dz=pz-p->z;p->near=p->type==29 || dx*dx+dz*dz<NPC_RADIUS*NPC_RADIUS;
                if(p->type!=29)p->frame=fmodf(p->frame+dt*30.f,p->pose==1 ? 10.f:9.f);
                continue;
            }
            /* Shared formation-member controller: main states 3/4/5 are
               FUN_8c093878/0938d4/093b70 and animation substates 0..3 are
               FUN_8c092d34/093c70/093cfc/093ea8.  The countdown continues
               while attack/recovery clips play, exactly as in state 4. */
            float ddx = px - p->x, ddz = pz - p->z, d2 = ddx * ddx + ddz * ddz; p->near = d2 < NPC_RADIUS * NPC_RADIUS;
            if(p->type==30) {
                NpcSquad *q=p->squad>0 && p->squad<=s->nsquads?s->squads+p->squad-1:NULL;
                int yaw=(int)(p->yaw*(65536.f/6.28318530718f));
                int target=(int)(atan2f(ddx,ddz)*(65536.f/6.28318530718f));
                if(police_attack)p->combat_flags&=~2u;
                int fire=enemy_tank_tick(&p->tank,&p->combat_flags,
                    q && (q->leader.flags&2) && !police_attack,sqrtf(d2),target,&yaw,&p->anim,&p->frame);
                p->yaw=yaw*(6.28318530718f/65536.f);
                if(fire && s->combat && s->combat->tank_shell) {
                    float pos[3]={p->x,p->y,p->z};
                    s->combat->tank_shell(s->combat->context,pos,p->yaw,p->frame,p->tank.aim);
                }
                p->frame+=dt*30.f;continue;
            }
            int common = rival_is_common_member(p), anim_changed = 0;
            const SquadAnim *aset = common ? squad_anim(p) : 0;
            SquadPar par = squad_par(p);
            /* 0x8c2fc8b0 (police attack in progress): the leader does not arm (FUN_8c09238c) and armed members reset their chase (FUN_8c073530) */
            if (police_attack) { p->combat_flags&=~2u; }
            else if (!p->armed && common && (p->combat_flags&2)) {
                p->armed = 1; p->atk_timer = common ? (float)par.initial : 0.f;
                p->atk_left = common ? par.attacks : 2; p->life = 0.f;
                p->gun=(EnemyGun){.first=1};
                if(p->type==27) {
                    p->atk_timer=1; /* 0afa20 */
                    p->gun.first=s->combat && s->combat->random15 ? (s->combat->random15(s->combat->context)*3u)/32768u:0;
                }
                p->combat_flags&=~0x60040000u;
                if (aset && p->atk_timer <= (float)par.windup) anim_changed |= squad_set_anim(p, 1);
            }
            if (p->armed) {
                int burst_active=p->atk_left>0;
                float ty = atan2f(ddx, ddz), d = ty - p->yaw; while (d > 3.14159f) d -= 6.28318f; while (d < -3.14159f) d += 6.28318f;
                float mx = 0x1555 * (6.2831853f / 65536.f) * dt * 30.f; if (d > mx) d = mx; if (d < -mx) d = -mx; p->yaw += d;
                if (p->knocked > 0) { p->knocked -= dt * 30.f; if (p->knocked < 0) p->knocked = 0; }
                else if (p->atk_left > 0) {
                    p->atk_timer -= dt * 30.f;
                    if (aset && p->anim == 0 && p->atk_timer <= (float)par.windup)
                        anim_changed |= squad_set_anim(p, 1);
                    /* State 4 refuses to emit the attack object until the
                       wind-up handler sets flag 0x40000 at clip completion. */
                    if(aset && p->anim==1 && p->frame>=aset->frames[1])p->combat_flags|=0x40000;
                    if ((!aset || (p->combat_flags&0x40000)) && p->atk_timer <= 0.f) {
                        p->atk_left--; if (p->atk_left > 0) p->atk_timer = (float)par.interval;
                        if (aset) anim_changed |= squad_set_anim(p, 2);
                        if(p->type==27) {
                            /* 0afaac uses 07a2d0 gas, not 0938d4 bullets. */
                            float a[3]={p->x,p->y+10.f,p->z};
                            if(s->combat && s->combat->gas)s->combat->gas(s->combat->context,a,p->gun.first);
                            p->gun.first=(p->gun.first+1)%3;
                        } else if(s->combat && s->combat->shot) {
                            float a[3]={p->x,p->y+10.f,p->z};
                            /* 0938d4: the first shot of a burst deliberately misses. */
                            int mode=p->gun.first || !(p->combat_flags&0x20000000u) ? 0:1;
                            int kind=squad_sound_kind(p);
                            s->combat->shot(s->combat->context,a,s->combat->target,kind,mode);p->gun.first=0;
                        }
                        if(!p->atk_left)p->combat_flags&=~0xe0040002u;
                    } else if(p->type!=27 && p->atk_timer>0 && p->atk_timer<=par.windup && s->combat) {
                        GangCombat *c=s->combat;
                        if(!(p->combat_flags&0x40000000u) && c->shot) {
                            float a[3]={p->x,p->y+10.f,p->z};
                            p->gun.probe=c->shot(c->context,a,c->target,squad_sound_kind(p),2);p->combat_flags|=0x60000000u;
                        } else if(p->combat_flags&0x20000000u) {
                            int blocked=c->shot_result && c->shot_result(c->context,p->gun.probe)>0;
                            if(!blocked && c->vulnerable)c->warning=1;
                            else p->combat_flags&=~0x20000000u;
                        }
                    }
                } else if (!aset || p->anim == 0) {p->armed=0;p->combat_flags&=~2u;}

                /* 0938d4/093b70 retain the action-4 cue from the first shot
                   through the final firing tick, then release it on recovery.
                   096178 resolves an action through each actor's selector. */
                if(p->type!=27 && burst_active && !p->gun.first && s->combat && s->combat->gun_sound) {
                    int report=p->type==29?0x30008:p->type==28?0x7000d:0xb;
                    float a[3]={p->x,p->y,p->z};
                    p->gun.audio_token=s->combat->gun_sound(s->combat->context,p->gun.audio_token,report,a);
                }
                if (aset && !anim_changed) {
                    if (p->anim == 2 && p->frame >= aset->frames[2]) anim_changed |= squad_set_anim(p, 3);
                    else if (p->anim == 3 && p->frame >= aset->frames[3]) {
                        anim_changed |= squad_set_anim(p, 0);
                        if (p->atk_left > 0 && p->atk_timer <= (float)par.windup) anim_changed |= squad_set_anim(p, 1);
                    }
                }
            }
            /* Formation members own their collision reaction; do not feed
               them the street-pedestrian's unrelated fall controller. */
            if (!anim_changed) p->frame += dt * 30.f;
            continue;
        }
        /* 08a814/08a930: stay hidden until a rendered view rejects the
           spawn point. Render/pose preparation never advances this state. */
        if(!p->seen){if(!p->visible)p->seen=1;continue;}
        float ddx = p->x - px, ddz = p->z - pz;
        if (p->knocked > 0) { p->knocked -= dt * 30.f; p->frame += dt * 30.f; if (p->knocked <= 0.f) p->knocked = 0.f; continue; }
        /* FUN_8c08b2ac: a moving player within 12 units knocks the pedestrian down (state 5/6) */
        if (p->visible && pspeed > 0.f && ddx * ddx + ddz * ddz < 12.f * 12.f) { knock_begin(s,p, px, pz); continue; }
        if (p->nwp < 2) { p->frame += dt * 30.f;for(unsigned tick=0;tick<audio_ticks;tick++)ambient_tick(s,p);continue; }
        if(!p->visible)continue; /* 08a97c: walking pauses outside the view test */
        int nxt = p->cur + p->dir; if (nxt >= p->nwp || nxt < 0) { p->dir = -p->dir; nxt = p->cur + p->dir; }
        float dx = p->wp[nxt][0] - p->x, dz = p->wp[nxt][2] - p->z, d = sqrtf(dx * dx + dz * dz);
        float step = p->speed * 30.f * dt;
        if (d <= step) { p->x = p->wp[nxt][0]; p->z = p->wp[nxt][2]; p->cur = nxt; }
        else { p->x += dx / d * step; p->z += dz / d * step; p->yaw = atan2f(dx, dz); }
        if (((i + g_tick) & 7) == 0) p->y = ground_y(col, p->x, p->y, p->z);
        p->frame += dt * 30.f;
        for(unsigned tick=0;tick<audio_ticks;tick++)ambient_tick(s,p);
    }
}

void npcs_prepare_detail(Npcs *s,Chars *actors,Chars *people)
{
    for(int i=0;i<s->count;i++) {
        const Ped *p=s->p+i;
        if(p->pmodel>=0 && people && people->base)chars_prepare_lod(people,p->pmodel);
        else if(p->ch>=0 && p->ch<=25)chars_prepare_lod(actors,p->ch);
    }
}
static void npcs_render(Npcs *s, Chars *actors, Chars *people,int prepare,const float view[16],float focal)
{
    for (int i = 0; i < s->count; i++) {
        Ped *p = &s->p[i];
        if(parked_police(p)) {
            if(prepare)continue;
            Chars *bank=&s->police_vehicles;
            int model=p->type==29 ? 2:(p->pose==1 ? 0:1);
            p->visible=p->active && p->near && bank->h && chars_actor_visible(bank,model,p->parked.matrix);
            if(p->type==29 && view){
                /* 0aed90 / 1c9b78: original 100-unit detailed model,
                   600-unit draw limit, transformed sphere center. */
                
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_npc_c__local_0.inc"
#else

#include "generated/source_data/npc_c__local_0.inc"

#endif
float center[3];
                source_point(p->parked.matrix,local,center);
                float depth=-(view[8]*center[0]+view[9]*center[1]+view[10]*center[2]+view[11]);
                if(depth>=600.f)p->visible=0;
                if(depth>=100.f)model=3;
            } else if(view) {
                /* 0936ac: stationary beacon vehicles stop at 500 (0c9d50). */
                float depth=-(view[8]*p->parked.matrix[3]+view[9]*p->parked.matrix[7]+view[10]*p->parked.matrix[11]+view[11]);
                if(depth>500.f)p->visible=0;
            }
            if(p->active && !p->visible)p->seen=1;
            /* This bank contains only the stationary clips selected by
               0929bc and the matching source car/van/beacon texture lists. */
            if(p->visible && p->seen){
                CHAR_WORK_BEGIN(work);
                chars_draw_vehicle(bank,model,p->parked.matrix,p->type==29?-1:0,p->frame);
                CHAR_WORK_END(CHAR_WORK_OTHER,work);
            }
            continue;
        }
        if(!prepare && p->squad && p->active) {
            float root[16]={1,0,0,p->x,0,1,0,p->y,0,0,1,p->z,0,0,0,1};
            p->visible=p->near && ped_view_alpha(p)>0.f && chars_actor_visible(actors,p->ch,root);
            /* 092fb4 / 0a20fc clear initial hidden bit 100 on REJECTION.
               S3M02 records 12..15 use off-map members to gate arrivals;
               waiting for them to be visible deadlocks both escalations. */
            if(!p->visible)p->seen=1;
        }
        if (!p->active || !p->near) continue;
        if(p->squad && !p->seen)continue;
        if(p->list>=1000 && !(ped_view_alpha(p)>0.f))continue; /* beyond the 06dd7c gate */
        if(p->list<1000 && p->pmodel>=0 && pedestrian_hidden(s,people,i)) {
            if(prepare)NPC_OCC_ADD(prepare_skipped,1);else NPC_OCC_ADD(draw_skipped,1);
            continue;
        }
        if(prepare) {
            float co=cosf(p->yaw),si=sinf(p->yaw);
            float root[16]={co,0,si,p->x,0,1,0,p->y,-si,0,co,p->z,0,0,0,1};
            const Chars *bank=p->pmodel>=0 && people && people->base ? people:actors;
            int ci=bank==people ? p->pmodel:chars_source_lod_model(actors,p->ch,p->list>=1000 ? p->view_mode:1);
            if(!bank || !bank->h || ci<0 || !chars_actor_visible(bank,ci,root))continue;
        }
        if (p->pmodel >= 0 && people && people->base) {
            int motion = route_motion(p->route); float frame = p->frame;
            if (p->type == 43 || p->type == 44) {
                /* The source's compact street actors use normal[22] while
                   standing and special[4] for the collision reaction. */
                motion = p->knocked > 0.f ? 1 : 0;
                frame = p->knocked > 0.f ? 120.f - p->knocked : p->frame;
            } else if (p->knocked > 0.f) {
                float elapsed = 120.f - p->knocked;
                if (elapsed < 16.f) { motion = p->knock_motion; frame = elapsed; }
                else if (p->type == 45) { motion = 22; frame = elapsed - 16.f; }
                else if (p->type == 46) { motion = 23; frame = elapsed - 16.f; }
                else if (p->flags && elapsed < 43.f) { motion = 26; frame = elapsed - 16.f; }
                else { motion = 9; frame = elapsed - (p->flags ? 43.f : 16.f); }
            }
            if(prepare){chars_pose_prefetch(people,p->pmodel,motion,frame);continue;}
            CHAR_WORK_BEGIN(work);
            chars_draw_pedestrian(people,p->pmodel,p->x,p->y,p->z,p->yaw,motion,frame,p->opacity,view,focal);
            CHAR_WORK_END(CHAR_WORK_PEDESTRIANS,work);
        } else {
            int motion = 0; float frame = p->frame;
            const SquadAnim *aset = squad_anim(p);
            if (p->ch < 0) continue;   /* source alternate object not yet represented by a character bank */
            if (aset && (p->list >= 2000 || rival_is_common_member(p))) {
                int state = p->list >= 2000 ? 0 : p->anim;
                if (state < 0 || state > 3) state = 0;
                motion = aset->motion[state];
                /* FUN_8c0bdf30 mode 1 reports completion and leaves the
                   wind-up on its terminal pose while state 4 counts down. */
                if (state == 1 && frame >= aset->frames[1]) frame = aset->frames[1] - 1.f;
            } else if (p->ch <= 15 || (p->ch >= 22 && p->ch <= 25)) {
                motion = player_anim(0);
            }
            int draw_model=chars_source_lod_model(actors,p->ch,p->list>=1000 ? p->view_mode:1);
            if(prepare){if(p->type!=30)chars_pose_prefetch(actors,draw_model,motion,frame);continue;}
            CHAR_WORK_BEGIN(work);
            if(p->type==30 && actors->base && (unsigned)p->ch<actors->h->n_chars) {
                const JgcChar *ch=actors->chars+p->ch;CharPose pose[64];
                float co=cosf(p->yaw),si=sinf(p->yaw);
                float root[16]={co,0,si,p->x,0,1,0,p->y,-si,0,co,p->z,0,0,0,1};
                if(ch->nbones>=3 && ch->nbones<=64 && chars_actor_visible(actors,p->ch,root)) {
                    for(unsigned b=0;b<ch->nbones;b++)chars_sample_pose(actors,ch,ch->motion0+(p->anim==7?0:18),frame,b,pose+b);
                    pose[2].ang[1]+=p->tank.aim;chars_draw_one_pose(actors,p->ch,root,pose,-1,0,0);
                }
            } else if(p->ch<=25)chars_draw_npc(actors,draw_model,p->x,p->y,p->z,p->yaw,motion,frame,ped_view_alpha(p),view,focal);
            else chars_draw_one_faded(actors,draw_model,p->x,p->y,p->z,p->yaw,motion,frame,ped_view_alpha(p));
            CHAR_WORK_END(CHAR_WORK_OTHER,work);
        }
    }
}
/* This callback runs inside the render math scope: scalar geometry only,
   with no GU/GUM calls, VFPU use or per-actor clock reads. */
static int pedestrian_posed_hidden(void *context,const float center[3],const float extent[3])
{
    NPC_OCC_ADD(late_tested,1);
    if(!stage_actor_occluded((const Stage *)context,center,extent))return 0;
    NPC_OCC_ADD(late_hidden,1);return 1;
}
void npcs_draw(Npcs *s,Chars *actors,Chars *people,const float view[16],float focal)
{
    int ready=pedestrian_occlusion_enabled==NPC_OCCLUSION_CONSERVATIVE && s->occlusion_people==s->p &&
        s->occlusion_bank==people && s->occlusion_count==s->count && stage_actor_occluders(s->occlusion_stage)>0;
    chars_ped_occlusion_query(ready ? pedestrian_posed_hidden:NULL,(void *)s->occlusion_stage);
    npcs_render(s,actors,people,0,view,focal);
    chars_ped_occlusion_query(NULL,NULL);s->occlusion_count=0;
}
void npcs_prepare_poses(Npcs *s,Chars *actors,Chars *people){npcs_render(s,actors,people,1,NULL,0);}
void npcs_free(Npcs *s) {
    if(s->sound_event)for(int i=0;i<s->count;i++)if(s->p[i].sound)s->sound_event(s->sound_context,s->p+i,NPC_SOUND_DELETE);
    chars_free(&s->police_vehicles);free(s->squads);if (s->p) free(s->p); memset(s, 0, sizeof *s);
}
