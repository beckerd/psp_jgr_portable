#ifndef JGR_PLAYER_BODY_H
#define JGR_PLAYER_BODY_H
#include <math.h>
#include "player.h"
#include "source_trs.h"
#include "source_fmac.h"
typedef struct {int parent,attribute;float sphere[4],pos[3];int32_t ang[3];float scale[3];} PlayerBodyNode;
/* One immutable source table, owned by player.c. */
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_player_body.inc"
#else
extern const PlayerBodyNode PLAYER_BODY[16][20];
#endif
/* Deferred previous-pose contacts. Capture owns resolved overlay/default
   values and the root; no Player/bank pointer or hierarchy matrices survive.
   Resolve only the requested origins once a contact actually needs them. */
typedef struct {float root[16];CharPose pose[20];int model;} PlayerBodySnapshot;
int player_body_snapshot_capture(const Player *,PlayerBodySnapshot *);
/* Writes requested bones and their ancestors; other output bytes remain
   untouched. Both functions return20 on success, -1 for invalid inputs. */
int player_body_snapshot_origins(const PlayerBodySnapshot *,unsigned mask,float out[20][3]);
/* 0b5f90 -> 2711a4: strict summed-radius comparison, not squared
   distance or a world-triangle/capsule query. */
static inline int player_body_sphere_overlap(const float a[4],const float b[4])
{
    float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];
    /* Conservative adapter broadphase only. Keep the source FMA/sqrt test
       for all nearby pairs. A TWO-radius cube has ample rounding margin;
       restrict the shortcut to positive normal gameplay radii so subnormal,
       nonfinite and malformed inputs still follow the reference path. */
#ifndef JGR_NO_BODY_BROADPHASE
    float radius=a[3]+b[3];
    if(radius>=1.f && radius<=0x1p60f &&
       isfinite(x) && isfinite(y) && isfinite(z) &&
       (fabsf(x)>radius*2.f || fabsf(y)>radius*2.f || fabsf(z)>radius*2.f))return 0;
#endif
    float d=source_fmac(x,x,y*y);d=source_fmac(z,z,d);
    return a[3]+b[3]>sqrtf(d);
}
/* Original collision hierarchy applies every TRS in Z/Y/X order. It
   does not inherit the render mesh's eval flags, Y/X/Z flag or bind inverse.
   Its spheres' radii are copied unchanged by 0b70ca. */
static inline int player_body_pose(int model,const CharPose pose[20],const float root[16],float out[20][4])
{
    if((unsigned)model>=16 || !pose || !root || !out)return -1;
    float matrices[20][16];
    for(int j=0;j<20;j++) {
        const PlayerBodyNode *b=&PLAYER_BODY[model][j];
        memcpy(matrices[j],b->parent<0 ? root:matrices[b->parent],64);
        int32_t angles[3]={pose[j].ang[0],pose[j].ang[1],pose[j].ang[2]};
        source_trs_apply(pose[j].pos,angles,pose[j].scl,matrices[j],player_fsca_sin);
        source_point(matrices[j],b->sphere,out[j]);
        out[j][3]=b->sphere[3];
    }
    return 20;
}
static inline int player_body_matrices_uncached(const Player *p,float matrices[20][16],unsigned mask)
{
    if(!p || !p->pose_valid || (unsigned)p->pose_model>=16)return -1;
    float root[16];player_root_matrix(p,root);CharPose pose[20];
    const Chars *c=p->spray_variant && p->spray_bank ? p->spray_bank:p->pose_bank;
    const JgcChar *ch=c ? c->chars+(c==p->pose_bank ? p->pose_model:0):NULL;
    /* Sound needs two anchors, not the unrelated limbs. Retain every
       ancestor and the exact transform order along each requested path. */
    for(int j=19;j>=0;j--)if(mask&(1u<<j)) {
        int parent=PLAYER_BODY[p->pose_model][j].parent;if(parent>=0)mask|=1u<<parent;
    }
    for(int j=0;j<20;j++)if(mask&(1u<<j)) {
        pose[j]=p->pose[j];
        if(ch && p->overlay_motion>=0 && (unsigned)p->overlay_motion<ch->nmotions && p->overlay_blend>0)
            chars_overlay_pose(c,ch,ch->motion0+p->overlay_motion,p->overlay_frame,j,p->overlay_blend,pose+j);
        const PlayerBodyNode *b=&PLAYER_BODY[p->pose_model][j];
        if(!(pose[j].keyed&1))memcpy(pose[j].pos,b->pos,12);
        if(!(pose[j].keyed&2))for(int k=0;k<3;k++)pose[j].ang[k]=b->ang[k];
        if(!(pose[j].keyed&4))memcpy(pose[j].scl,b->scale,12);
        memcpy(matrices[j],b->parent<0 ? root:matrices[b->parent],64);
        int32_t angles[3]={pose[j].ang[0],pose[j].ang[1],pose[j].ang[2]};
        source_trs_apply(pose[j].pos,angles,pose[j].scl,matrices[j],player_fsca_sin);
    }
    return 20;
}
/* Shared by effects, spatial audio, traffic and pickup contacts. The cache
   compares resolved poses and root matrix bytes, never a frame/tick alone. */
int player_body_matrices_masked(const Player *p,float matrices[20][16],unsigned mask);
typedef struct {float matrices[20][16],spheres[20][4];} PlayerBodyGeometry;
/* Borrowed, read-only cache storage, valid only until the next body query.
   Requested spheres and their ancestor matrices are valid; consume them
   before querying another actor. NULL means the player pose is invalid. */
const PlayerBodyGeometry *player_body_geometry(const Player *,unsigned sphere_mask);
static inline int player_body_matrices(const Player *p,float matrices[20][16])
{return player_body_matrices_masked(p,matrices,0xfffff);}
/* Conservative distance from the actor origin to any collision sphere.
   Triangle inequalities avoid posing distant pickup candidates. Rotations
   preserve length; max(abs(scale)) bounds each ancestor's stretch. Use the
   original collision bind defaults, and fall back for blended overlays.
   Optional bounds are conservative per-bone radii about the actor origin;
   every entry becomes infinite if the pose cannot be bounded safely. */
static inline float player_body_reach_bounds(const Player *p,float bounds[20])
{
    if(!p || !p->pose_valid || (unsigned)p->pose_model>=16 || p->overlay_blend>0)
        goto unbounded;
    float reach[20],stretch[20],result=0;
    for(int j=0;j<20;j++) {
        const PlayerBodyNode *b=&PLAYER_BODY[p->pose_model][j];
        const CharPose *pose=p->pose+j;
        const float *v=pose->keyed&1 ? pose->pos:b->pos;
        const float *s=pose->keyed&4 ? pose->scl:b->scale;
        float parent=b->parent<0 ? 1.f:stretch[b->parent];
        float offset=fabsf(v[0])+fabsf(v[1])+fabsf(v[2]);
        float scale=fmaxf(fabsf(s[0]),fmaxf(fabsf(s[1]),fabsf(s[2])));
        if(!isfinite(offset) || !isfinite(s[0]) || !isfinite(s[1]) || !isfinite(s[2]))goto unbounded;
        reach[j]=(b->parent<0 ? 0.f:reach[b->parent])+parent*offset;
        stretch[j]=parent*scale;
        float r=reach[j]+stretch[j]*(fabsf(b->sphere[0])+fabsf(b->sphere[1])+fabsf(b->sphere[2]))+fabsf(b->sphere[3]);
        if(!isfinite(r))goto unbounded;
        if(bounds)bounds[j]=r*1.01f+1.f;
        if(r>result)result=r;
    }
    /* Margin covers FSCA/TRS rounding and the root's unit rotations. */
    result=result*1.01f+1.f;
    if(!isfinite(result))goto unbounded;
    return result;
unbounded:
    if(bounds)for(int j=0;j<20;j++)bounds[j]=INFINITY;
    return INFINITY;
}
static inline float player_body_reach(const Player *p)
{return player_body_reach_bounds(p,NULL);}
static inline int player_body_spheres(const Player *p,float out[20][4])
{
    const PlayerBodyGeometry *geometry=player_body_geometry(p,0xfffff);
    if(!geometry)return -1;
    memcpy(out,geometry->spheres,sizeof geometry->spheres);return 20;
}
#endif
