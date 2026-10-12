#include "street_props.h"
#include "release_report.h"
#include "player_body.h"
#include "source_body.h"
#include "audio.h"
#include "object_view.h"
#include <stdlib.h>

typedef struct {
    float vertices[24],center[3];unsigned attribute;
    float sphere[4],position[3];int32_t angles[3];float scale[3];int parent;unsigned kind;
} StreetPropBox;
typedef struct {
    unsigned address,type,sector,first_box,boxes,reaction;int sound;
    float position[3];int32_t angles[3];float scale[3];unsigned animation;
} StreetPropPlacement;
typedef struct {
    unsigned frames;const int16_t *angle;
    float position[3];int32_t angles[3];float scale[3];unsigned axis;
} StreetPropAnimation;
/* Source 0574c4 and 058e90 are different rigid reaction tasks. */
enum {STREET_REACTION_BOUNCE,STREET_REACTION_FIXED,STREET_REACTION_SINGLE_BOUNCE,
      STREET_REACTION_ANIMATED,STREET_REACTION_VIEW_FIXED,STREET_REACTION_FULL_SINGLE_BOUNCE,
      STREET_REACTION_LOW_FIXED,STREET_REACTION_ANIMATED_SINGLE_BOUNCE};
typedef struct {
    unsigned stage,sectors,count,box_count;
    const StreetPropPlacement *placements;const StreetPropBox *boxes;
} StreetPropStage;
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_street_props_data.inc"
#else
#include "generated/source_data/street_props_data.inc"
#endif
static const StreetPropStage *street_prop_stage(int stage)
{
    for(unsigned i=0;i<sizeof street_prop_stages/sizeof *street_prop_stages;i++)
        if(street_prop_stages[i].stage==(unsigned)stage)return street_prop_stages+i;
    return NULL;
}
int street_props_supported(int stage)
{const StreetPropStage *data=street_prop_stage(stage);return data && data->count!=0;}

typedef struct {float box[24],low[3],high[3],center[3];} StreetPropBody;
typedef struct {
    /* Idle contact geometry is never used after launch. Reuse its storage
       for the only two matrices needed by the rigid render path. */
    union {
        StreetPropBody body;
        struct {float box[24],low[3],high[3],center[3];};
        struct {float inverse[12],delta[16];} transform;
    };
    float position[3],velocity[3],normal[3];
    int32_t angles[3],spin[3];
    unsigned sound,age;int handle;unsigned short first_extra,animation_slot;
    unsigned char resident,hit,phase,bounce,bounced,render_dirty;
} StreetProp;
enum {STREET_IDLE,STREET_PENDING,STREET_FLYING,STREET_RETIRED};
typedef struct {float inverse[12],delta[16];unsigned owner,frame;} StreetPropAnimationState;
struct StreetProps {
    const StreetPropStage *data;
    StreetProp *props;StreetPropBody *extra;
    StreetPropAnimationState *animations;unsigned nanimations;
    unsigned short *resident,*nearby,*active;
    const Zones *zones;int sector;
    ObjectView view;
    const Collision *collision;Stage *stage;
    PlayerBodySnapshot previous;unsigned previous_valid,random,drawn,probes;
    unsigned nresident,nnearby,nactive,contacts,ticks,contact_enabled,view_gated;
};
static void street_prop_render_matrix(StreetProps *,unsigned);
static void street_prop_animation_init(StreetProps *,unsigned);

static int street_prop_idle_inner(StreetProps *s,unsigned i)
{
    StreetProp *o=s->props+i;const StreetPropPlacement *p=s->data->placements+i;
    if(o->sound)audio_world_stop(o->sound);
    if(s->stage && o->handle)stage_prop_hide(s->stage,o->handle,0);
    o->sound=o->age=0;o->resident=o->hit=o->phase=o->bounce=o->bounced=0;
    if(p->animation)s->animations[o->animation_slot].frame=0;
    memcpy(o->position,p->position,12);memcpy(o->angles,p->angles,12);
    float root[16],matrices[STREET_PROP_MAX_SHAPES][16];source_trs(p->position,p->angles,p->scale,root);
    for(unsigned j=0;j<p->boxes;j++){
        const StreetPropBox *b=s->data->boxes+p->first_box+j;
        StreetPropBody *body=j ? s->extra+o->first_extra+j-1:&o->body;
        memcpy(matrices[j],b->parent<0 ? root:matrices[b->parent],64);
        source_trs_apply(b->position,b->angles,b->scale,matrices[j],player_fsca_sin);
        source_point(matrices[j],b->center,body->center);
        if(b->kind==2){
            source_point(matrices[j],b->sphere,body->box);body->box[3]=b->sphere[3];
            for(int k=0;k<3;k++){
                body->low[k]=body->box[k]-body->box[3];body->high[k]=body->box[k]+body->box[3];
            }
        } else {
            for(int k=0;k<3;k++)body->low[k]=INFINITY,body->high[k]=-INFINITY;
            for(int v=0;v<8;v++){
                source_point(matrices[j],b->vertices+v*3,body->box+v*3);
                for(int k=0;k<3;k++){
                    float x=body->box[v*3+k];if(!isfinite(x))return -3;
                    if(x<body->low[k])body->low[k]=x;
                    if(x>body->high[k])body->high[k]=x;
                }
            }
        }
        for(int k=0;k<3;k++){
            if(!isfinite(body->center[k]) || !isfinite(body->low[k]) || !isfinite(body->high[k]))return -3;
            if(j){if(body->low[k]<o->low[k])o->low[k]=body->low[k];if(body->high[k]>o->high[k])o->high[k]=body->high[k];}
        }
    }
    o->render_dirty=p->animation!=0;
    return 0;
}
static int street_prop_idle(StreetProps *s,unsigned i)
{
    const StreetPropPlacement *p=s->data->placements+i;
    unsigned report=release_report_prop_begin(s->data->stage,i,p->address,p->type,RELEASE_PROP_RESET);
    int result=street_prop_idle_inner(s,i);
    release_report_prop_end(report);
    return result;
}
void street_props_reset(StreetProps *s)
{
    if(!s)return;
    for(unsigned i=0;i<s->data->count;i++)street_prop_idle(s,i);
    s->zones=NULL;s->sector=-1;
    memset(&s->view,0,sizeof s->view);
    s->previous_valid=s->drawn=s->probes=0;s->random=1;
    s->nresident=s->nnearby=s->nactive=s->contacts=s->ticks=s->contact_enabled=0;
}
void street_props_free(StreetProps **owner)
{if(owner && *owner){street_props_reset(*owner);free(*owner);*owner=NULL;}}
void street_props_set_view(StreetProps *s,const float eye[3],const float forward[3],float tangent)
{
    if(!s || !s->view_gated)return;
    if(!eye || !forward){memset(&s->view,0,sizeof s->view);return;}
    object_view_setup(&s->view,s->data->stage,eye,forward,tangent,1.f);
}
int street_props_load(StreetProps **owner,int stage,const Collision *collision,Stage *static_stage)
{
    if(!owner)return -3;
    street_props_free(owner);const StreetPropStage *data=street_prop_stage(stage);
    if(!data || data->count>STREET_PROP_MAX_COUNT || data->count>65535)return -3;
    if(!data->count)return 0;
    if(!data->placements || !data->boxes || !data->sectors || data->sectors>256)return -3;
    /* Only the current stage owns mutable instances. All lists share this
       allocation and retain the original authored traversal order. */
    unsigned extras=0,animations=0;
    for(unsigned i=0;i<data->count;i++){
        const StreetPropPlacement *p=data->placements+i;
        if(!p->boxes || p->boxes>STREET_PROP_MAX_SHAPES || p->first_box>=data->box_count ||
           p->boxes>data->box_count-p->first_box || p->sector>=data->sectors || p->reaction>STREET_REACTION_ANIMATED_SINGLE_BOUNCE)return -3;
        if(p->animation){
            if(p->animation>sizeof street_prop_animations/sizeof *street_prop_animations ||
               (p->reaction!=STREET_REACTION_ANIMATED && p->reaction!=STREET_REACTION_ANIMATED_SINGLE_BOUNCE))return -3;
            const StreetPropAnimation *a=street_prop_animations+p->animation-1;
            if(a->frames<2 || !a->angle || a->axis>2)return -3;
            animations++;
        } else if(p->reaction==STREET_REACTION_ANIMATED || p->reaction==STREET_REACTION_ANIMATED_SINGLE_BOUNCE)return -3;
        for(int k=0;k<3;k++)if(!isfinite(p->position[k]) || !isfinite(p->scale[k]) || p->scale[k]==0.f)return -3;
        for(unsigned j=0;j<p->boxes;j++){
            const StreetPropBox *b=data->boxes+p->first_box+j;
            if((b->kind!=2 && b->kind!=4) || b->attribute!=25 || b->parent<-1 || b->parent>=(int)j)return -3;
            if(b->kind==2 && (!isfinite(b->sphere[3]) || b->sphere[3]<0.f))return -3;
        }
        extras+=p->boxes-1;if(extras>65535)return -3;
    }
    StreetProps *s=calloc(1,sizeof *s+data->count*(sizeof(StreetProp)+3*sizeof(unsigned short))+
                         extras*sizeof(StreetPropBody)+animations*sizeof(StreetPropAnimationState));
    if(!s)return -2;
    s->data=data;s->props=(StreetProp *)(s+1);s->extra=(StreetPropBody *)(s->props+data->count);
    s->animations=(StreetPropAnimationState *)(s->extra+extras);s->nanimations=animations;
    s->resident=(unsigned short *)(s->animations+animations);
    s->nearby=s->resident+data->count;s->active=s->nearby+data->count;
    s->collision=collision;s->stage=static_stage;
    extras=animations=0;
    for(unsigned i=0;i<data->count;i++){
        const StreetPropPlacement *p=data->placements+i;StreetProp *o=s->props+i;
        s->view_gated|=p->reaction==STREET_REACTION_VIEW_FIXED;
        o->first_extra=(unsigned short)extras;extras+=p->boxes-1;
        if(p->animation){
            o->animation_slot=(unsigned short)animations;s->animations[animations].owner=i;
            street_prop_animation_init(s,animations++);
        }
        if(street_prop_idle(s,i)){free(s);return -3;}
        if(static_stage){
            float root[16];source_trs(p->position,p->angles,p->scale,root);
            o->handle=stage_prop_bind(static_stage,p->address,root);
            if(!o->handle){free(s);return -3;}
        }
    }
    street_props_reset(s);*owner=s;return 0;
}
static unsigned street_prop_random(StreetProps *s)
{s->random=s->random*0x41c64e6du+0x3039u;return (s->random>>16)&32767u;}
static void street_matrix_inverse(const float a[16],float v[12])
{
    float d=1.f/(a[0]*(a[5]*a[10]-a[6]*a[9])-a[1]*(a[4]*a[10]-a[6]*a[8])+a[2]*(a[4]*a[9]-a[5]*a[8]));
    v[0]=(a[5]*a[10]-a[6]*a[9])*d;v[1]=(a[2]*a[9]-a[1]*a[10])*d;v[2]=(a[1]*a[6]-a[2]*a[5])*d;
    v[4]=(a[6]*a[8]-a[4]*a[10])*d;v[5]=(a[0]*a[10]-a[2]*a[8])*d;v[6]=(a[2]*a[4]-a[0]*a[6])*d;
    v[8]=(a[4]*a[9]-a[5]*a[8])*d;v[9]=(a[1]*a[8]-a[0]*a[9])*d;v[10]=(a[0]*a[5]-a[1]*a[4])*d;
    for(int r=0;r<3;r++)v[r*4+3]=-((v[r*4]*a[3]+v[r*4+1]*a[7])+v[r*4+2]*a[11]);
}
static void street_matrix_delta(const float a[16],const float b[12],float m[16])
{
    for(int r=0;r<3;r++)for(int c=0;c<4;c++)
        m[r*4+c]=((a[r*4]*b[c]+a[r*4+1]*b[4+c])+a[r*4+2]*b[8+c])+(c==3 ? a[r*4+3]:0.f);
    m[12]=m[13]=m[14]=0;m[15]=1;
}
static void street_prop_inverse(StreetProp *o,const StreetPropPlacement *p)
{float a[16];source_trs(o->position,o->angles,p->scale,a);street_matrix_inverse(a,o->transform.inverse);}
static void street_prop_animation_init(StreetProps *s,unsigned slot)
{
    StreetPropAnimationState *state=s->animations+slot;
    const StreetPropPlacement *p=s->data->placements+state->owner;
    const StreetPropAnimation *a=street_prop_animations+p->animation-1;
    float initial[16];source_trs(p->position,p->angles,p->scale,initial);
    source_trs_apply(a->position,a->angles,a->scale,initial,player_fsca_sin);
    street_matrix_inverse(initial,state->inverse);
}
static void street_prop_render_matrix(StreetProps *s,unsigned index)
{
    StreetProp *o=s->props+index;if(!o->render_dirty)return;
    const StreetPropPlacement *p=s->data->placements+index;
    /* Frame/lifetime/physics advance independently of visibility. Build
       their render-only matrices at the first visible draw, sharing the
       root TRS between the rigid root and animated child. The next pass
       and paused redraws reuse the same exact, ordered transform. */
    float current[16];source_trs(o->position,o->angles,p->scale,current);
    if(o->phase==STREET_FLYING)street_matrix_delta(current,o->transform.inverse,o->transform.delta);
    if(p->animation){
        StreetPropAnimationState *state=s->animations+o->animation_slot;
        const StreetPropAnimation *a=street_prop_animations+p->animation-1;
        int32_t angles[3];memcpy(angles,a->angles,sizeof angles);angles[a->axis]=a->angle[state->frame];
        source_trs_apply(a->position,angles,a->scale,current,player_fsca_sin);
        street_matrix_delta(current,state->inverse,state->delta);
    }
    o->render_dirty=0;
}
static void street_prop_floor(StreetProps *s,StreetProp *o,unsigned reaction)
{
    o->bounce=0;
    /* 057940 registers a .5 sphere, mask3, at the current root. Its result
       is consumed NEXT tick, only after vy-=gravity makes vy negative.
       Ascending queries whose results cannot be used are redundant. */
    if(s->collision && !o->bounced && o->velocity[1]<.098f){
        ColHit hit;s->probes++;
        if(col_sphere(s->collision,o->position,.5f,reaction==STREET_REACTION_BOUNCE ? 3:1,&hit)){
            memcpy(o->normal,hit.n,12);o->bounce=1;
        }
    }
}
static void street_prop_motion_inner(StreetProps *s,unsigned i,const Player *p)
{
    StreetProp *o=s->props+i;const StreetPropPlacement *place=s->data->placements+i;
    int fixed=place->reaction==STREET_REACTION_FIXED || place->reaction==STREET_REACTION_VIEW_FIXED ||
              place->reaction==STREET_REACTION_LOW_FIXED || place->reaction==STREET_REACTION_ANIMATED_SINGLE_BOUNCE;
    int has_floor=place->reaction==STREET_REACTION_BOUNCE || place->reaction==STREET_REACTION_SINGLE_BOUNCE ||
                  place->reaction==STREET_REACTION_FULL_SINGLE_BOUNCE || place->reaction==STREET_REACTION_ANIMATED_SINGLE_BOUNCE;
    if(o->phase==STREET_PENDING){
        /* 05ab1c alone gates the transition on the global sound block,
           despite creating no sound task itself. Retain idle geometry. */
        if(place->reaction==STREET_REACTION_VIEW_FIXED && p && p->sound_blocked){
            o->phase=STREET_IDLE;o->hit=0;return;
        }
        /* 0574c4: three positive rand15/32768 spin samples, not a random
           direction or a speed threshold. The contact supplied velocity. */
        if(fixed){o->spin[0]=300;o->spin[1]=400;o->spin[2]=500;}
        else {
            
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_street_props_c__spin_0.inc"
#else

#include "generated/source_data/street_props_c__spin_0.inc"

#endif

            for(int k=0;k<3;k++)o->spin[k]=(int32_t)((float)street_prop_random(s)/32768.f*spin[k]);
        }
        street_prop_inverse(o,place);o->render_dirty=1;
        o->phase=STREET_FLYING;o->age=0;
        if(s->stage && o->handle)stage_prop_hide(s->stage,o->handle,1);
        if(place->sound && (!p || !p->sound_blocked))o->sound=audio_world_sound(place->sound,o->position);
        if(has_floor)street_prop_floor(s,o,place->reaction);
    } else if(o->phase==STREET_FLYING){
        /* Basic props change state after age61; the other families after
           age31. Destruction is the next tick, preserving that last pose. */
        if(place->reaction!=STREET_REACTION_ANIMATED && o->age>(place->reaction==STREET_REACTION_BOUNCE ? 60u:30u)){
            o->phase=STREET_RETIRED;o->bounce=0;
            if(o->sound)audio_world_stop(o->sound);
            o->sound=0;return;
        }
        o->age++;o->velocity[1]-=.098f; /* source literal 3dc8b439 */
        for(int k=0;k<3;k++){
            o->position[k]+=o->velocity[k];
            o->angles[k]=(int32_t)((uint32_t)o->angles[k]+(uint32_t)o->spin[k]);
        }
        if(has_floor && o->velocity[1]<0 && o->bounce){
            if(place->reaction!=STREET_REACTION_BOUNCE){
                o->velocity[1]=-(o->velocity[1]*.6f);o->bounced=1;
                o->spin[0]=-200;o->spin[1]=200;o->spin[2]=-300;
            } else {
                /* Source uses separate component abs/mul/mul/add operations,
                   not a dot-product reflection or positional depenetration. */
                for(int k=0;k<3;k++)o->velocity[k]+=(o->normal[k]*fabsf(o->velocity[k]))*1.5f;
                o->spin[0]=o->spin[2]=-500;o->spin[1]=200;
            }
        }
        o->render_dirty=1;if(has_floor)street_prop_floor(s,o,place->reaction);
        /* 058858 increments twice, and deletes immediately at age90. */
        if(place->reaction==STREET_REACTION_ANIMATED && ++o->age>=90){
            o->phase=STREET_RETIRED;
            if(o->sound)audio_world_stop(o->sound);
            o->sound=0;return;
        }
    }
    if(o->sound){
        if(audio_world_finished(o->sound)){audio_world_stop(o->sound);o->sound=0;}
        else audio_world_position(o->sound,o->position);
    }
}
static void street_prop_motion(StreetProps *s,unsigned i,const Player *p)
{
    const StreetPropPlacement *place=s->data->placements+i;
    unsigned report=release_report_prop_begin(s->data->stage,i,place->address,place->type,RELEASE_PROP_MOTION);
    street_prop_motion_inner(s,i,p);
    release_report_prop_end(report);
}
void street_props_tick(StreetProps *s,const Player *p,const Zones *zones,int sector)
{
    if(!s)return;
    s->ticks++;s->nnearby=0;s->previous_valid=0;s->contact_enabled=p && !p->reset_query;
    /* 030934/030c94/030d3c only create/retire when sector ownership changes.
       Keep a compact resident list rather than retesting all distant props
       against the neighbour table on every player tick. */
    if(s->zones!=zones || s->sector!=sector){
        s->zones=zones;s->sector=sector;s->nresident=s->nactive=0;
        for(unsigned i=0;i<s->data->count;i++){
            StreetProp *o=s->props+i;
            if(!zones || !zone_neighbour(zones,sector,s->data->placements[i].sector)){
                if(o->resident)street_prop_idle(s,i);
                continue;
            }
            o->resident=1;s->resident[s->nresident++]=(unsigned short)i;
            if(s->data->placements[i].animation && s->stage && o->handle)stage_prop_hide(s->stage,o->handle,1);
            if(o->phase==STREET_PENDING || o->phase==STREET_FLYING)
                s->active[s->nactive++]=(unsigned short)i;
        }
    }
    /* Preserve placement order (and hence launch RNG order), but visit
       only live reactions for motion and both render passes. */
    unsigned kept=0;
    for(unsigned j=0;j<s->nactive;j++){
        unsigned i=s->active[j];street_prop_motion(s,i,p);
        if(s->props[i].phase==STREET_FLYING)s->active[kept++]=(unsigned short)i;
    }
    s->nactive=kept;
    for(unsigned j=0;j<s->nanimations;j++){
        StreetPropAnimationState *state=s->animations+j;StreetProp *o=s->props+state->owner;
        if(!o->resident || o->phase==STREET_RETIRED)continue;
        const StreetPropPlacement *place=s->data->placements+state->owner;
        const StreetPropAnimation *a=street_prop_animations+place->animation-1;
        unsigned period=a->frames-(place->reaction==STREET_REACTION_ANIMATED);
        if(++state->frame>=period)state->frame=0;
        o->render_dirty=1;
    }
    for(unsigned j=0;j<s->nresident;j++){
        unsigned i=s->resident[j];const StreetPropPlacement *place=s->data->placements+i;
        StreetProp *o=s->props+i;
        if(!p || o->hit)continue;
        if(place->reaction==STREET_REACTION_VIEW_FIXED &&
           !object_pickup_visible(&s->view,place->position[0],place->position[1],place->position[2]))continue;
        /* Leaving only the 30-unit range is NOT a respawn and must not
           resurrect an already contacted prop. */
        float x=p->x-place->position[0],y=p->y-place->position[1],z=p->z-place->position[2];
        /* 057940 -> 0c284c, literal057af4=900. The caller samples the
           player anchor before simulation, as the original stage task. */
        if(place->reaction==STREET_REACTION_FULL_SINGLE_BOUNCE || (x*x+y*y)+z*z<900.f)
            s->nearby[s->nnearby++]=(unsigned short)i;
    }
    if(s->contact_enabled && s->nnearby){
        /* Freeze the inputs, not a full world-space body hierarchy. Only
           an actual hit needs the previous NODE origin; a nearby miss
           must not build twenty matrices just to discard all of them. */
        s->previous_valid=player_body_snapshot_capture(p,&s->previous)==20;
    }
}
static int street_prop_body_broadphase(const StreetPropBody *o,const float center[3],float radius)
{
    /* Conservative only: the original strict sphere/box decision remains
       in source_body_box_sphere. Slack avoids changing boundary rounding. */
    for(int k=0;k<3;k++){
        float slack=.001f+(fabsf(center[k])+fabsf(radius)+fabsf(o->low[k])+fabsf(o->high[k]))*1e-5f;
        if(isfinite(slack) && radius>=0 &&
           (center[k]+radius+slack<o->low[k] || center[k]-radius-slack>o->high[k]))return 0;
    }
    return 1;
}
static int street_prop_broadphase(const StreetProp *o,const float center[3],float radius)
{return street_prop_body_broadphase(&o->body,center,radius);}
void street_props_contacts(StreetProps *s,const Player *p)
{
    if(!s || !p || !s->contact_enabled || !s->nnearby || p->reset_query || !p->pose_valid ||
       (unsigned)p->pose_model>=16)return;
    /* 057280 registers layer0x10, accepts1; 018e.. registers the player
       layer1, accepts0x7ef. 0b5f90 accepts either cross-mask, so the prop's
       accepts1 matches the player even though 0x7ef excludes0x10. Node25
       is a recipient attribute, NOT a solid surface or collision mask. */
    float position[3]={p->x,p->y,p->z},bounds[20],reach=player_body_reach_bounds(p,bounds);
    unsigned short candidates[STREET_PROP_MAX_COUNT];unsigned masks[STREET_PROP_MAX_COUNT],count=0,sphere_mask=0;
    for(unsigned j=0;j<s->nnearby;j++){
        unsigned i=s->nearby[j];StreetProp *o=s->props+i;
        if(!o->resident || o->hit || !street_prop_broadphase(o,position,reach))continue;
        /* The same triangle-inequality bound already computed for the
           actor also bounds each individual limb. A far hand must not
           force transformations/tests for nineteen unrelated spheres. */
        unsigned mask=0;
        for(unsigned b=0;b<20;b++)if(PLAYER_BODY[p->pose_model][b].sphere[3]>0 &&
           street_prop_broadphase(o,position,bounds[b]))mask|=1u<<b;
        if(mask){candidates[count]=(unsigned short)i;masks[count++]=mask;sphere_mask|=mask;}
    }
    if(!count)return;
    /* Traffic and pickups consume this same completed pose. Borrow its
       matrices and sphere centers; later callers only fill missing limbs.
       No other body-cache query occurs while these pointers are in use. */
    const PlayerBodyGeometry *body=player_body_geometry(p,sphere_mask);if(!body)return;
    const float (*matrices)[16]=body->matrices,(*spheres)[4]=body->spheres;
    unsigned char selected_bones[STREET_PROP_MAX_COUNT],selected_shapes[STREET_PROP_MAX_COUNT];unsigned hits=0,previous_mask=0;
    for(unsigned j=0;j<count;j++){
        StreetProp *o=s->props+candidates[j];const StreetPropPlacement *place=s->data->placements+candidates[j];
        unsigned report=release_report_prop_begin(s->data->stage,candidates[j],place->address,place->type,RELEASE_PROP_CONTACT);
        float nearest=INFINITY;int selected=-1;unsigned selected_shape=0;
        for(unsigned shape=0;shape<place->boxes;shape++){
            const StreetPropBox *descriptor=s->data->boxes+place->first_box+shape;
            const StreetPropBody *volume=shape ? s->extra+o->first_extra+shape-1:&o->body;
            SourceBodyBoxPrepared box;if(descriptor->kind==4)source_body_box_prepare(&box,volume->box);
            for(unsigned bone=0;bone<20;bone++){
                if(!(masks[j]&(1u<<bone)))continue;
                const float *sphere=spheres[bone];
                if(!(sphere[3]>0) || !street_prop_body_broadphase(volume,sphere,sphere[3]) ||
                   !(descriptor->kind==2 ? player_body_sphere_overlap(volume->box,sphere):source_body_box_sphere_prepared(&box,sphere)))continue;
                float d[3];for(int k=0;k<3;k++)d[k]=matrices[bone][k*4+3]-volume->center[k];
                float distance=sqrtf(probe_fipr(d,d));
                if(distance<nearest){nearest=distance;selected=(int)bone;selected_shape=shape;}
            }
        }
        release_report_prop_end(report);
        if(selected<0)continue;
        candidates[hits]=candidates[j];selected_shapes[hits]=(unsigned char)selected_shape;
        selected_bones[hits++]=(unsigned char)selected;
        previous_mask|=1u<<selected;
    }
    if(!hits)return;
    float previous_origins[20][3];
    int previous_valid=s->previous_valid &&
        player_body_snapshot_origins(&s->previous,previous_mask,previous_origins)==20;
    for(unsigned j=0;j<hits;j++){
        unsigned i=candidates[j],selected=selected_bones[j];StreetProp *o=s->props+i;
        const StreetPropPlacement *place=s->data->placements+i;
        unsigned report=release_report_prop_begin(s->data->stage,i,place->address,place->type,RELEASE_PROP_CONTACT);
        const StreetPropBody *volume=selected_shapes[j] ? s->extra+o->first_extra+selected_shapes[j]-1:&o->body;
        /* 0b5e7c orders contacts by node-anchor distance. 0574c4 takes
           the first partner's direction/magnitude even for zero speed. */
        float origin[3],previous[3];
        for(int k=0;k<3;k++){
            origin[k]=matrices[selected][k*4+3];
            previous[k]=previous_valid ? previous_origins[selected][k]+p->portal_translation[k]:origin[k];
        }
        SourceActorContact contact;
        source_actor_contact(&contact,volume->center,volume->center,25,origin,previous,PLAYER_BODY[p->pose_model][selected].attribute);
        o->velocity[0]=contact.other_speed*contact.other_direction[0];
        o->velocity[2]=contact.other_speed*contact.other_direction[2];
        if(place->reaction==STREET_REACTION_FIXED || place->reaction==STREET_REACTION_ANIMATED || place->reaction==STREET_REACTION_VIEW_FIXED){
            /* 058e90 retains the two separate multiply roundings. */
            o->velocity[0]*=1.2f;o->velocity[2]*=1.2f;
            o->velocity[1]=contact.other_speed*.6f;
        } else if(place->reaction==STREET_REACTION_LOW_FIXED){
            o->velocity[0]*=1.2f;o->velocity[2]*=1.2f;o->velocity[1]=0.f;
        } else if(place->reaction==STREET_REACTION_SINGLE_BOUNCE){
            o->velocity[0]*=.6f;o->velocity[2]*=.6f;o->velocity[1]=contact.other_speed*.3f;
        } else o->velocity[1]=contact.other_speed*.5f;
        o->hit=1;o->phase=STREET_PENDING;s->contacts++;
        unsigned at=s->nactive++;
        while(at && s->active[at-1]>i){s->active[at]=s->active[at-1];at--;}
        s->active[at]=(unsigned short)i;
        release_report_event(RELEASE_EVENT_PROP_IMPACT,place->address,place->type,i);
        release_report_prop_end(report);
    }
}
void street_props_draw(StreetProps *s,const float eye[3],const float forward[3],float tangent,const float clip[16],int pass)
{
    if(!s || !s->stage)return;
    for(unsigned j=0;j<s->nactive;j++){
        StreetProp *o=s->props+s->active[j];
        if(o->phase==STREET_FLYING && o->handle && !s->data->placements[s->active[j]].animation &&
           stage_prop_visible(s->stage,o->handle,o->position,eye,forward,tangent)){
            const StreetPropPlacement *place=s->data->placements+s->active[j];
            unsigned report=release_report_prop_begin(s->data->stage,s->active[j],place->address,place->type,RELEASE_PROP_DRAW);
            street_prop_render_matrix(s,s->active[j]);
            s->drawn+=stage_draw_prop(s->stage,o->handle,o->transform.delta,o->position,eye,forward,tangent,clip,pass);
            release_report_prop_end(report);
        }
    }
    static const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for(unsigned j=0;j<s->nanimations;j++){
        StreetPropAnimationState *state=s->animations+j;StreetProp *o=s->props+state->owner;
        if(!o->resident || o->phase==STREET_RETIRED || !o->handle)continue;
        if(!stage_prop_visible(s->stage,o->handle,o->position,eye,forward,tangent))continue;
        const StreetPropPlacement *place=s->data->placements+state->owner;
        unsigned report=release_report_prop_begin(s->data->stage,state->owner,place->address,place->type,RELEASE_PROP_DRAW);
        street_prop_render_matrix(s,state->owner);
        const float *root=o->phase==STREET_FLYING ? o->transform.delta:identity;
        s->drawn+=stage_draw_prop_part(s->stage,o->handle,0,root,o->position,eye,forward,tangent,clip,pass);
        s->drawn+=stage_draw_prop_part(s->stage,o->handle,1,state->delta,o->position,eye,forward,tangent,clip,pass);
        release_report_prop_end(report);
    }
}
void street_props_info(const StreetProps *s,unsigned out[5])
{
    if(!out)return;
    memset(out,0,5*sizeof *out);if(!s)return;
    for(unsigned i=0;i<s->data->count;i++){
        const StreetProp *o=s->props+i;out[0]+=o->resident;out[2]+=o->resident && o->hit;
    }
    for(unsigned j=0;j<s->nnearby;j++){const StreetProp *o=s->props+s->nearby[j];out[1]+=o->resident && !o->hit;}
    out[3]=s->contacts;out[4]=s->ticks;
}
void street_props_effect_info(const StreetProps *s,unsigned out[4])
{
    if(!out)return;
    memset(out,0,4*sizeof *out);if(!s)return;
    for(unsigned j=0;j<s->nresident;j++){
        const StreetProp *o=s->props+s->resident[j];
        out[0]+=o->phase==STREET_FLYING;out[1]+=o->phase==STREET_RETIRED;
    }
    out[2]=s->drawn;out[3]=s->probes;
}
