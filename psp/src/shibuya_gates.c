#include "shibuya_gates.h"
#include "source_trs.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
/* Original dynamic type 0: 030688 -> 057b88. These are construction
   barriers which flex on impact, not passage-opening doors. 057fbc keeps
   their bind collision registered throughout the 50-frame animation. */
typedef struct {unsigned sector,object;float position[3];int32_t angles[3];JgsObj original;} BarrierPlacement;
#if JGR_PORTABLE_ASSETS
#include "source_data_shibuya_gate_data.inc"
#else
#include "generated/source_data/shibuya_gate_data.inc"
#endif
#define BARRIER_COUNT (sizeof barrier_placements/sizeof *barrier_placements)
#define BARRIER_TRIANGLES (sizeof barrier_collision/sizeof *barrier_collision)
typedef struct {float root[16],frame;unsigned active,phase,contact;int handle;ColTri triangles[BARRIER_TRIANGLES];} Barrier;
struct ShibuyaGates {Chars model;Barrier barriers[BARRIER_COUNT];unsigned reactions,contacts,ticks;Stage *stage;Collision *collision;};
static int barrier_tick(Barrier *b,float speed,float perpendicular,float threshold)
{
    int reacted=0;
    if(!b->phase){
        /* 057d98 / 057dfa: both comparisons are strict; the axis is
           transformed (-1,0,0), and 2712d0(1555) returns FSCA cosine. */
        if(b->contact && speed>.75f && fabsf(perpendicular)>threshold){b->phase=1;b->frame=0;reacted=1;}
    } else if(++b->frame>49.f){b->frame=0;b->phase=0;}
    b->contact=0;return reacted;
}
void shibuya_gates_reset(ShibuyaGates *g)
{
    if(!g)return;
    for(unsigned i=0;i<BARRIER_COUNT;i++){Barrier *b=g->barriers+i;
        if(b->handle)col_surface_remove(g->collision,b->handle);
        b->handle=0;b->active=b->phase=b->contact=0;b->frame=0;}
    g->reactions=g->contacts=g->ticks=0;
}
void shibuya_gates_free(ShibuyaGates **owner)
{
    if(!owner || !*owner)return;
    ShibuyaGates *g=*owner;
    shibuya_gates_reset(g);
    /* The level frees these owners before freeing its Stage. Restore the
       static fallback when ownership ends; no stage file is modified. */
    if(g->stage)for(unsigned i=0;i<BARRIER_COUNT;i++)g->stage->obj[barrier_placements[i].object]=barrier_placements[i].original;
    chars_free(&g->model);free(g);*owner=NULL;
}
int shibuya_gates_load(ShibuyaGates **owner,Stage *stage,Collision *collision)
{
    shibuya_gates_free(owner);
    if(!stage || !stage->h || !collision)return -3;
    for(unsigned i=0;i<BARRIER_COUNT;i++){
        const BarrierPlacement *p=barrier_placements+i;
        if(p->object>=stage->h->n_obj || memcmp(stage->obj+p->object,&p->original,sizeof p->original))return -3;
    }
    ShibuyaGates *g=calloc(1,sizeof *g);if(!g)return -2;
    int error=chars_load(&g->model,"shibuya_gates.jgc");
    if(!error && (g->model.h->n_chars!=1 || g->model.h->n_motions!=1 || g->model.chars[0].nbones!=1 || g->model.motions[0].nframes!=50))error=-3;
    if(error){chars_free(&g->model);free(g);return error;}
    for(unsigned i=0;i<BARRIER_COUNT;i++){
        const BarrierPlacement *p=barrier_placements+i;Barrier *b=g->barriers+i;const float scale[3]={1,1,1};
        for(int k=0;k<16;k++)b->root[k]=(k%5)==0;
        source_trs_apply(p->position,p->angles,scale,b->root,player_fsca_sin);
        memcpy(b->triangles,barrier_collision,sizeof b->triangles);
        for(unsigned t=0;t<BARRIER_TRIANGLES;t++)for(int v=0;v<3;v++)
            source_point(b->root,barrier_collision[t].v+v*3,b->triangles[t].v+v*3);
    }
    g->collision=collision;
    /* Reserve the initial surface storage before hiding the static fallback.
       Subsequent sector entry uses the already allocated owner table. */
    for(unsigned i=0;i<BARRIER_COUNT;i++){
        Barrier *b=g->barriers+i;b->handle=col_surface_add(collision,b->triangles,NULL,BARRIER_TRIANGLES);
        if(!b->handle){shibuya_gates_free(&g);return -2;}
    }
    shibuya_gates_reset(g);
    for(unsigned i=0;i<BARRIER_COUNT;i++)stage->obj[barrier_placements[i].object].n_batch=0;
    g->stage=stage;*owner=g;return 0;
}
void shibuya_gates_contact(void *owner,int triangle)
{
    ShibuyaGates *g=owner;if(!g || triangle<0)return;
    for(unsigned i=0;i<BARRIER_COUNT;i++){
        Barrier *b=g->barriers+i;
        if(b->active && b->handle && (unsigned)triangle>=(unsigned)b->handle && (unsigned)triangle<(unsigned)b->handle+BARRIER_TRIANGLES){
            if(!b->contact)g->contacts++;
            b->contact=1;
        }
    }
}
int shibuya_gates_tick(ShibuyaGates *g,const Player *p,const Zones *zones,int sector)
{
    if(!g)return 0;
    g->ticks++;
    int yaw=(int)(p->yaw*(65536.f/(2.f*3.14159265358979323846f)));
    float dx=player_fsca_sin(yaw),dz=player_fsca_sin(yaw+16384),threshold=player_fsca_sin(0x1555+16384);
    for(unsigned i=0;i<BARRIER_COUNT;i++){
        Barrier *b=g->barriers+i;
        if(!zones || !zone_neighbour(zones,sector,barrier_placements[i].sector)){
            if(b->handle)col_surface_remove(g->collision,b->handle);
            b->handle=0;
            b->active=b->phase=b->contact=0;b->frame=0;continue;
        }
        if(!b->handle){b->handle=col_surface_add(g->collision,b->triangles,NULL,BARRIER_TRIANGLES);if(!b->handle)return -2;}
        b->active=1;if(p->reset_query)b->contact=0;
        g->reactions+=barrier_tick(b,p->speed,dx*b->root[8]-dz*b->root[0],threshold);
        /* 030664/030718 supplies sound program -1 for this type. */
    }
    return 0;
}
void shibuya_gates_draw(ShibuyaGates *g,const float eye[3],const float forward[3],const float planes[6][4],int pass)
{
    if(!g)return;
    unsigned order[BARRIER_COUNT],count=0;float depth[BARRIER_COUNT];
    for(unsigned i=0;i<BARRIER_COUNT;i++)if(g->barriers[i].active){
        const float *p=barrier_placements[i].position;float d=0;for(int k=0;k<3;k++)d+=(p[k]-eye[k])*forward[k];
        unsigned at=count++;
        if(pass==1)while(at && depth[at-1]<d){order[at]=order[at-1];depth[at]=depth[at-1];at--;}
        order[at]=i;depth[at]=d;
    }
    /* 02f748..02f764 -> 0c3828: original Shibuya normal light. */
    const CharLight light={{-1.f,-.71f,-1.f},.95f,.47f,{0xffffffffu,0},0};
    chars_scene_lighting(&light,NULL);chars_world_frustum(planes);
    for(unsigned j=0;j<count;j++){
        Barrier *b=g->barriers+order[j];
        if(b->phase)chars_draw_material_pass_clipped(&g->model,0,b->root,0,b->frame,pass,planes);
        else {CharPose bind;chars_sample_pose(&g->model,g->model.chars,-1,0,0,&bind);
            chars_draw_material_pose_clipped(&g->model,0,b->root,&bind,pass,planes);}
    }
    chars_world_frustum(NULL);chars_scene_lighting(NULL,NULL);
}
void shibuya_gates_info(const ShibuyaGates *g,unsigned out[3])
{out[0]=g?g->reactions:0;out[1]=g?g->contacts:0;out[2]=g?g->ticks:0;}
