/* Value-checked hierarchy reuse across passes AND frames. Storage replaces GU arena
   space; it never owns animation/asset data or clipped vertices. */
#define PREP_BONES 256u
#define PREP_SLOTS 16u
typedef struct {
    const Chars *bank;
    unsigned ci,first,count,valid;
    float root[16];
} PreparedHierarchy;
static PreparedHierarchy prepared_hierarchies[PREP_SLOTS];
static float prepared_matrices[PREP_BONES][16] __attribute__((aligned(16)));
static CharPose prepared_poses[PREP_BONES];
static short prepared_parents[PREP_BONES];
static unsigned preparation_cursor,preparation_victim;
typedef char preparation_budget[(sizeof prepared_hierarchies+sizeof prepared_matrices+
    sizeof prepared_poses+sizeof prepared_parents<=CHAR_PREPARATION_BYTES) ? 1:-1];
#ifdef JGR_PREPARATION_REFERENCE
static int preparation_enabled;
#else
static int preparation_enabled=1;
#endif
void chars_preparation_mode(int enabled){preparation_enabled=!!enabled;}
static void preparation_begin(void)
{/* Every root, local pose and parent is checked by prepare_hierarchy.
    Bank release invalidates ownership. A new frame alone changes none of
    those values, so keep immutable prop transforms in the bounded pool. */}
static void forget_preparation(const Chars *c)
{for(unsigned i=0;i<PREP_SLOTS;i++)if(prepared_hierarchies[i].bank==c)prepared_hierarchies[i].bank=NULL;}
static PreparedHierarchy *preparation_slot(const Chars *c,unsigned ci,const float *root)
{
    const JgcChar *ch=c->chars+ci;
    if(!preparation_enabled || !ge_packets_active || !ch->nbones || ch->nbones>PREP_BONES)return NULL;
    PreparedHierarchy *empty=NULL;
    for(unsigned i=0;i<PREP_SLOTS;i++) {
        PreparedHierarchy *p=prepared_hierarchies+i;
        if(p->bank==c && p->ci==ci && p->count==ch->nbones && !memcmp(p->root,root,64))return p;
        if(!p->bank)empty=p;
    }
    unsigned first=preparation_cursor;
    if(first+ch->nbones>PREP_BONES)first=0;
    for(unsigned i=0;i<PREP_SLOTS;i++) {
        PreparedHierarchy *p=prepared_hierarchies+i;
        if(p->bank && first<p->first+p->count && p->first<first+ch->nbones){p->bank=NULL;empty=p;}
    }
    PreparedHierarchy *p=empty ? empty:prepared_hierarchies+(preparation_victim++%PREP_SLOTS);
    *p=(PreparedHierarchy){.bank=c,.ci=ci,.first=first,.count=ch->nbones};
    memcpy(p->root,root,64);preparation_cursor=first+ch->nbones;return p;
}
static float (*prepare_hierarchy(const Chars *c,unsigned ci,const float *root,
    const CharPose *poses,int mi,float frame,int mi2,float frame2,float weight,
    int overlay,float overlay_frame,float overlay_weight,float fallback[][16],int allow_cache))[16]
{
    const JgcChar *ch=c->chars+ci;
    PreparedHierarchy *entry=allow_cache ? preparation_slot(c,ci,root):NULL;
    float (*mats)[16]=entry ? prepared_matrices+entry->first:fallback;
    unsigned char changed[CHAR_RENDER_MAX_BONES];
    RenderMathState math;render_math_begin(&math);
    if(!entry) {
        for(unsigned bi=0;bi<ch->nbones;bi++) {
            CharPose pose;float local[16] __attribute__((aligned(16)));
            if(poses)pose=poses[bi];else chars_sample_pose(c,ch,mi,frame,bi,&pose);
            if(mi2>=0){CharPose other;chars_sample_pose(c,ch,mi2,frame2,bi,&other);chars_blend_pose(&pose,&other,weight);}
            if(overlay>=0)chars_overlay_pose(c,ch,overlay,overlay_frame,bi,overlay_weight,&pose);
            node_matrix(local,pose.ev,pose.pos,pose.ang,pose.scl);
            int parent=c->bones[ch->bone0+bi].parent-(int)ch->bone0;
            m_mul(mats[bi],parent>=0 ? mats[parent]:root,local);
        }
        posed_bones+=ch->nbones;preparation_profile.built_bones+=ch->nbones;
        render_math_end(&math);return mats;
    }
    for(unsigned bi=0;bi<ch->nbones;bi++) {
        const JgcBone *b=c->bones+ch->bone0+bi;CharPose pose;
        if(poses)pose=poses[bi];else chars_sample_pose(c,ch,mi,frame,bi,&pose);
        if(mi2>=0){CharPose other;chars_sample_pose(c,ch,mi2,frame2,bi,&other);chars_blend_pose(&pose,&other,weight);}
        if(overlay>=0)chars_overlay_pose(c,ch,overlay,overlay_frame,bi,overlay_weight,&pose);
        int parent=b->parent-(int)ch->bone0;
        unsigned at=entry ? entry->first+bi:0;
        int reuse=entry && entry->valid && prepared_parents[at]==parent &&
            !memcmp(prepared_poses+at,&pose,sizeof pose) && (parent<0 || !changed[parent]);
        changed[bi]=!reuse;
        if(reuse){preparation_profile.reused_bones++;continue;}
        float local[16] __attribute__((aligned(16)));
        node_matrix(local,pose.ev,pose.pos,pose.ang,pose.scl);
        m_mul(mats[bi],parent>=0 ? mats[parent]:root,local);
        if(entry){prepared_poses[at]=pose;prepared_parents[at]=(short)parent;}
        posed_bones++;preparation_profile.built_bones++;
    }
    render_math_end(&math);if(entry)entry->valid=1;
    return mats;
}
