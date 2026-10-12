#include "canplay.h"
#include "player_body.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void graffiti_can_drops_free(Graffiti *g)
{
    while(g->can_drops){CanDrop *next=g->can_drops->next;free(g->can_drops);g->can_drops=next;}
    g->ncan_drops=0;g->health_pickups[0]=g->health_pickups[1]=0;
}
int graffiti_can_drop(Graffiti *g,const float *origin,const float *target,unsigned kind)
{
    /* 05bba8 dispatches source 3/4 to 04d7cc, 5/6 to 0da358.
       Both families share the original 69d..79c task-id pool. */
    if(kind<3 || kind>6)return -1;
    for(int j=0;j<3;j++)if(!isfinite(origin[j]) || !isfinite(target[j]))return -1;
    unsigned count=g->ncan_drops;
    for(int i=0;i<g->ncans;i++)count+=g->cans[i].active!=0;
    if(count>=256)return -2;
    CanDrop *d=calloc(1,sizeof *d);if(!d)return -3;
    item_flight_start(&d->flight,origin,target);d->can.kind=kind-3;
    CanDrop **tail=&g->can_drops;while(*tail)tail=&(*tail)->next;*tail=d;
    g->ncan_drops++;return 0;
}
/* Both authored cans and landed drops use the same pickup/respawn logic.
   Flying effects never enter the player's collectible collision list. */
typedef struct {int index;CanDrop *drop;} CanWalk;
static Can *can_next(Graffiti *g,CanWalk *walk)
{
    if(walk->index<g->ncans)return g->cans+walk->index++;
    while(walk->drop){CanDrop *d=walk->drop;walk->drop=d->next;if(d->flight.state==3)return &d->can;}
    return NULL;
}
int graffiti_load_cans(Graffiti *g,const char *path)
{
    FILE *f=fopen(path,"rb");if(!f)return -1;
    unsigned h[3];Can *c=NULL;int error=-2;
    if(fread(h,1,12,f)!=12 || memcmp(h,"CAN3",4) || h[1]>32768 || h[2]>64 || (!h[2] && h[1]))goto done;
    c=calloc(h[1] ? h[1]:1,sizeof *c);if(!c){error=-3;goto done;}
    for(unsigned i=0;i<h[1];i++) {
        unsigned row[7];if(fread(row,1,28,f)!=28)goto done;
        c[i].set=row[0];c[i].area=row[1];c[i].source=row[2];
        memcpy(&c[i].x,row+3,12);c[i].kind=row[6];
        if(c[i].set>=h[2] || c[i].area>=5 || c[i].kind>3 ||
           !isfinite(c[i].x) || !isfinite(c[i].y) || !isfinite(c[i].z))goto done;
    }
    if(fgetc(f)!=EOF)goto done;
    graffiti_can_drops_free(g);free(g->cans);g->cans=c;c=NULL;g->ncans=h[1];g->can_sets=h[2];
    g->can_set=g->can_areas=0;g->can_acc=0;error=0;
done:
    free(c);fclose(f);return error;
}
int graffiti_cans_select(Graffiti *g,unsigned set,unsigned areas)
{
    if(set>=g->can_sets || (areas&~31u))return -1;
    if(g->can_set==set && g->can_areas==areas)return 0;
    for(int i=0;i<g->ncans;i++) {
        Can *c=g->cans+i;int active=c->set==set && (areas&(1u<<c->area));
        /* 04d9d8 destroys, 04d8a0 recreates paint tasks, discarding their
           taken/respawn state on an actual area unload/reload. */
        if(!!active!=c->active){
            /* 0da42c skips collected health records on area re-entry. */
            if(c->kind<2)c->taken=0;
            c->contact=0;c->respawn=0;c->frame=0;c->scale=c->kind>=2 ? 1.f:0.f;
        }
        c->active=!!active;
    }
    CanDrop **link=&g->can_drops;
    while(*link) {
        CanDrop *d=*link;
        /* Flying tasks belong to the level. After landing, 04d7cc attaches
           the normal can to the CURRENT area; unloading destroys it. Unlike
           authored entries a dropped can is not recreated on area re-entry. */
        if(set!=g->can_set || (d->flight.state==3 && !(areas&(1u<<d->can.area)))) {
            *link=d->next;free(d);g->ncan_drops--;
        }else link=&d->next;
    }
    g->can_set=set;g->can_areas=areas;return 0;
}
void graffiti_cans_reset(Graffiti *g,int capacity,int initially_full)
{
    graffiti_can_drops_free(g);
    g->can_capacity=capacity;g->cancount=initially_full ? capacity:0;g->can_acc=0;
    for(int i=0;i<g->ncans;i++) {
        Can *c=g->cans+i;c->taken=c->contact=0;c->respawn=0;c->frame=0;c->scale=c->kind>=2 ? 1.f:0.f;
    }
}
void can_task_tick(Can *c,int contact,float frames)
{
    if(!c->active)return;
    if(!c->taken) {
        c->frame+=1.f;if(c->frame>=frames)c->frame=0;
        c->scale+=.1f;if(c->scale>=1.f)c->scale=1.f;
        if(contact){c->scale=c->frame=0;c->taken=1;}
    }else if(++c->respawn>=3600){c->respawn=0;c->contact=0;c->taken=0;}
}
void canplay_refresh_contacts(Graffiti *g,const Player *p)
{
    if(!g || !p)return;
    float spheres[20][4];int count=-1;float reach=player_body_reach(p)+7.5f;
    CanWalk walk={0,g->can_drops};Can *c;
    while((c=can_next(g,&walk))){
        c->contact=0;if(!c->active || c->taken)continue;
        if(fabsf(c->x-p->x)>reach || fabsf(c->y-p->y)>reach || fabsf(c->z-p->z)>reach)continue;
        if(count<0)count=player_body_spheres(p,spheres);
        const float sphere[4]={c->x,c->y,c->z,(c->kind&1)?7.5f:5.f};
        for(int j=0;j<count;j++)if(spheres[j][3]>0 && player_body_sphere_overlap(spheres[j],sphere)){c->contact=1;break;}
    }
}
int canplay_update(Graffiti *g,Player *p,const Chars *bank,float dt)
{
    if(!g || !p || !isfinite(dt) || dt<=0)return 0;
    g->can_acc+=fminf(dt,.25f);int awarded=0;
    /* The player pose does not change inside this function. Catch-up can
       ticks advance only the can tasks, so evaluate the same 20-node
       collision hierarchy once, not once for each elapsed source tick. */
    float spheres[20][4];int n=-1;
    float reach=player_body_reach(p)+7.5f;
    while(g->can_acc+1e-7f>=1.f/30.f) {
        /* 0db638 destroys a collected health task on its following tick.
           It does NOT use the paint task's 3600-tick respawn. */
        for(CanDrop **link=&g->can_drops;*link;) {
            CanDrop *d=*link;
            if(d->can.kind>=2 && d->can.taken){*link=d->next;free(d);g->ncan_drops--;}
            else link=&d->next;
        }
        Can *picked=NULL,*healed=NULL,*c;CanWalk walk={0,g->can_drops};
        while((c=can_next(g,&walk))) {
            if(!c->active)continue;
            if(c->kind>=2) {
                /* 051f0c has independent first-can / first-health passes.
                   All contacted item tasks still retire, even at full HP. */
                if(!c->taken && c->contact && !healed && !p->reset_query && p->health>0)healed=c;
                c->frame+=1.f;
                if(c->contact && c->source) {
                    /* Aliased area entries share the source +10 word. */
                    for(int i=0;i<g->ncans;i++)if(g->cans[i].kind>=2 && g->cans[i].set==c->set && g->cans[i].source==c->source)g->cans[i].taken=1;
                }
                c->taken|=c->contact;c->contact=0;continue;
            }
            /* Collision list dispatcher 051f0c stops on its first can.
               All contacted can tasks still observe the player's id. */
            if(!c->taken && c->contact && !picked && !p->reset_query && p->health>0)picked=c;
            float frames=0;
            if(bank && bank->h && bank->h->n_chars>=4) {
                const JgcChar *ch=bank->chars+(c->kind ? 2:0);
                if(ch->nmotions)frames=bank->motions[ch->motion0].nframes;
            }
            can_task_tick(c,c->contact,frames);c->contact=0;
        }
        if(picked){int n=picked->kind ? 5:1;g->cancount+=n;awarded+=n;
            /* 051f0c -> 05cdd4 -> 05d000: the shared PLAY effect bank. */
            if(!p->sound_blocked && p->sound_event)p->sound_event(p->sound_context,PLAYER_SOUND_EFFECT,11);
        }
        if(healed) {
            unsigned kind=healed->kind-2;
            p->health+=p->health_max*(kind ? 50:20)/100;
            if(p->health>p->health_max)p->health=p->health_max;
            p->recovery_flash_ticks=30; /* 051f0c, including pickups at full HP */
            /* 05cde0 shares the paint pickup's effect producer. */
            if(!p->sound_blocked && p->sound_event)p->sound_event(p->sound_context,PLAYER_SOUND_EFFECT,11);
            g->health_pickups[kind]++;
        }
        /* 04e04c clamps the actor inventory even when its draw is hidden. */
        if(g->cancount>g->can_capacity)g->cancount=g->can_capacity;
        walk=(CanWalk){0,g->can_drops};
        while((c=can_next(g,&walk))) {
            if(!c->active || c->taken)continue;
#ifndef JGR_NO_CAN_BROADPHASE
            if(fabsf(c->x-p->x)>reach || fabsf(c->y-p->y)>reach || fabsf(c->z-p->z)>reach)continue;
#endif
            if(n<0)n=player_body_spheres(p,spheres);
            /* Original 1828d8 / 18292c: centre (0,0,0), radius 5 / 7.5.
               0b70ca copies the radius without applying spawn scale. */
            /* Health descriptors 1cf3e8/1cf43c use the same 5/7.5 radii. */
            float sphere[4]={c->x,c->y,c->z,(c->kind&1) ? 7.5f:5.f};
            for(int j=0;j<n;j++)if(spheres[j][3]>0 && player_body_sphere_overlap(spheres[j],sphere)) {
                c->contact=1;break;
            }
        }
        for(CanDrop *d=g->can_drops;d;d=d->next) {
            if(d->flight.state==2) {
                memcpy(&d->can.x,d->flight.target,12);d->can.set=g->can_set;d->can.area=g->can_area;
                d->can.active=g->can_area<5 && (g->can_areas&(1u<<g->can_area));d->flight.state=3;
                if(d->can.kind>=2)d->can.scale=1.f; /* 0db430: no can grow-in */
            }else item_flight_tick(&d->flight);
        }
        g->can_acc-=1.f/30.f;if(g->can_acc<0)g->can_acc=0;
    }
    return awarded;
}
