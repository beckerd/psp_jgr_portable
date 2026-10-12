/* Four bounded scratch entries for interleaved main-thread actor queries. Resolve
   overlays and bind defaults before comparing: mutable/reloaded banks,
   multiple actors, same-tick movement and animation all remain exact.
   No stored Player/asset pointers and no lifetime invalidation protocol. */
int player_body_snapshot_capture(const Player *p,PlayerBodySnapshot *snapshot)
{
    if(!snapshot)return -1;
    snapshot->model=-1;
    if(!p || !p->pose_valid || (unsigned)p->pose_model>=16)return -1;
    player_root_matrix(p,snapshot->root);
    const Chars *c=p->spray_variant && p->spray_bank ? p->spray_bank:p->pose_bank;
    const JgcChar *ch=c ? c->chars+(c==p->pose_bank ? p->pose_model:0):NULL;
    int overlay=ch && p->overlay_motion>=0 && (unsigned)p->overlay_motion<ch->nmotions && p->overlay_blend>0;
    for(int j=0;j<20;j++){
        CharPose *pose=snapshot->pose+j;const PlayerBodyNode *b=&PLAYER_BODY[p->pose_model][j];
        *pose=p->pose[j];
        if(overlay)chars_overlay_pose(c,ch,ch->motion0+p->overlay_motion,p->overlay_frame,j,p->overlay_blend,pose);
        if(!(pose->keyed&1))memcpy(pose->pos,b->pos,12);
        if(!(pose->keyed&2))for(int k=0;k<3;k++)pose->ang[k]=b->ang[k];
        if(!(pose->keyed&4))memcpy(pose->scl,b->scale,12);
    }
    snapshot->model=p->pose_model;return 20;
}
int player_body_snapshot_origins(const PlayerBodySnapshot *snapshot,unsigned mask,float out[20][3])
{
    if(!snapshot || !out || (unsigned)snapshot->model>=16)return -1;
    mask&=0xfffffu;
    for(int j=19;j>=0;j--)if(mask&(1u<<j)){
        int parent=PLAYER_BODY[snapshot->model][j].parent;if(parent>=0)mask|=1u<<parent;
    }
    if(!mask)return 20;
    float matrices[20][16];RenderMathState math;render_math_begin(&math);
    unsigned stamp=vfpu_profile_transform_begin(),points=0,exact=0;
    for(int j=0;j<20;j++)if(mask&(1u<<j)){
        const CharPose *pose=snapshot->pose+j;int parent=PLAYER_BODY[snapshot->model][j].parent;
        memcpy(matrices[j],parent<0 ? snapshot->root:matrices[parent],64);
        int32_t angles[3]={pose->ang[0],pose->ang[1],pose->ang[2]};
        exact+=source_trs_apply_owned(pose->pos,angles,pose->scl,matrices[j],player_fsca_sin);points++;
        for(int k=0;k<3;k++)out[j][k]=matrices[j][k*4+3];
        player_profile.body_prepared_bones++;
    }
    render_math_end(&math);vfpu_profile_transform_end(stamp,points,exact);return 20;
}
#ifndef PLAYER_BODY_CACHE_SLOTS
#define PLAYER_BODY_CACHE_SLOTS 4
#endif
typedef struct {
    PlayerBodySnapshot source;
    PlayerBodyGeometry geometry;
    unsigned valid,sphere_valid;
} BodyCache;
static BodyCache *player_body_cache_prepare(const Player *p,unsigned mask,unsigned sphere_mask,unsigned *resolved)
{
    PlayerBodySnapshot source;
    if(player_body_snapshot_capture(p,&source)<0)return NULL;
    static BodyCache entries[PLAYER_BODY_CACHE_SLOTS];
    static unsigned next, recent;
    BodyCache *selected=NULL;
    /* Resolved values, not actor/asset addresses or a logic-tick stamp, are
       the key. Same-tick motion, overlays and bank edits remain exact. */
    for(unsigned i=0;i<PLAYER_BODY_CACHE_SLOTS;i++) {
        BodyCache *candidate=entries+(recent+i)%PLAYER_BODY_CACHE_SLOTS;
        if(candidate->valid && candidate->source.model==source.model &&
           !memcmp(candidate->source.root,source.root,sizeof source.root) &&
           !memcmp(candidate->source.pose,source.pose,sizeof source.pose)) {
            selected=candidate;recent=(unsigned)(candidate-entries);break;
        }
    }
    if(!selected) {
        selected=entries+next;recent=next;next=(next+1)%PLAYER_BODY_CACHE_SLOTS;
        selected->valid=selected->sphere_valid=0;selected->source=source;
    }
    for(int j=19;j>=0;j--)if(mask&(1u<<j)) {
        int parent=PLAYER_BODY[source.model][j].parent;if(parent>=0)mask|=1u<<parent;
    }
    unsigned missing=sphere_mask&~selected->sphere_valid;
    RenderMathState math;int borrow=(mask&~selected->valid)!=0 || missing!=0;
    unsigned stamp=0,points=0,exact=0;
    if(borrow){render_math_begin(&math);stamp=vfpu_profile_transform_begin();}
    for(int j=0;j<20;j++)if(mask&(1u<<j)) {
        if(!(selected->valid&(1u<<j))) {
            const CharPose *pose=source.pose+j;int parent=PLAYER_BODY[source.model][j].parent;
            float *matrix=selected->geometry.matrices[j];
            memcpy(matrix,parent<0 ? source.root:selected->geometry.matrices[parent],64);
            int32_t angles[3]={pose->ang[0],pose->ang[1],pose->ang[2]};
            exact+=source_trs_apply_owned(pose->pos,angles,pose->scl,matrix,player_fsca_sin);points++;
            selected->valid|=1u<<j;
            player_profile.body_prepared_bones++;
        } else player_profile.body_reused_bones++;
    }
    /* A prop's partial request can precede the traffic request. Transform
       only new sphere centres; reuse the same hierarchy and VFPU scope. */
    for(int j=0;j<20;j++)if(missing&(1u<<j)){
        const float *sphere=PLAYER_BODY[source.model][j].sphere;
        exact+=source_point_owned(selected->geometry.matrices[j],sphere,selected->geometry.spheres[j]);points++;
        selected->geometry.spheres[j][3]=sphere[3];selected->sphere_valid|=1u<<j;
    }
    if(borrow){render_math_end(&math);vfpu_profile_transform_end(stamp,points,exact);}
    if(resolved)*resolved=mask;
    return selected;
}
int player_body_matrices_masked(const Player *p,float matrices[20][16],unsigned mask)
{
    BodyCache *selected=player_body_cache_prepare(p,mask,0,&mask);if(!selected)return -1;
    for(int j=0;j<20;j++)if(mask&(1u<<j))memcpy(matrices[j],selected->geometry.matrices[j],64);
    return 20;
}
const PlayerBodyGeometry *player_body_geometry(const Player *p,unsigned sphere_mask)
{
    sphere_mask&=0xfffffu;
    BodyCache *selected=player_body_cache_prepare(p,sphere_mask,sphere_mask,NULL);
    return selected ? &selected->geometry:NULL;
}
