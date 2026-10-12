/* Keep the exact original grouping for a controlled on-device comparison.
   Refine large batches first, then medium batches, within a 64 KiB cap.
   Directory words contain a bound offset and a refinement bit. */
#define STAGE_CLIP_GROUP_TRIS 64u
#define STAGE_CLIP_GROUP_INDICES (STAGE_CLIP_GROUP_TRIS*3u)
#define STAGE_CLIP_REFINED 0x80000000u
#define STAGE_CLIP_GROUP_BUDGET 65536u
#ifdef JGR_CITY_CLIP_REFERENCE
static int refined_clipping;
#else
static int refined_clipping=1;
#endif
void stage_clip_mode(int enabled){refined_clipping=!!enabled;}
static int group_batch_indices_valid(const Stage *s,const JgsBatch *b)
{
    return !(b->n_idx%3) && b->vert_off<=s->h->n_vert && b->n_vert<=s->h->n_vert-b->vert_off &&
        b->idx_off<=s->h->n_idx && b->n_idx<=s->h->n_idx-b->idx_off;
}
static int group_batch_valid(const Stage *s,const JgsBatch *b)
{return b->n_idx>=2*STAGE_CLIP_GROUP_INDICES && group_batch_indices_valid(s,b);}
static unsigned reference_group_count(const Stage *s,const JgsBatch *b)
{return group_batch_valid(s,b) ? (b->n_idx+STAGE_CLIP_GROUP_INDICES-1)/STAGE_CLIP_GROUP_INDICES:0;}
static StageBatchBound clip_group_bound(const Stage *s,const JgsBatch *b,unsigned start,unsigned end)
{
    const JgsVert *vertices=stage_batch_vertices(s,b);const unsigned short *indices=stage_batch_indices(s,b);
    StageBatchBound unknown={0,0,0,INFINITY};
    float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
    for(unsigned j=start;j<end;j++) {
        unsigned index=indices[j];if(index>=b->n_vert)return unknown;
        const JgsVert *v=vertices+index;float p[3]={v->x,v->y,v->z};
        for(int k=0;k<3;k++){if(!isfinite(p[k]))return unknown;if(p[k]<low[k])low[k]=p[k];if(p[k]>high[k])high[k]=p[k];}
    }
    float cx=(low[0]+high[0])*.5f,cy=(low[1]+high[1])*.5f,cz=(low[2]+high[2])*.5f;
    float dx=high[0]-low[0],dy=high[1]-low[1],dz=high[2]-low[2];
    float r=sqrtf(dx*dx+dy*dy+dz*dz)*.5f;
    r+=.05f+(fabsf(cx)+fabsf(cy)+fabsf(cz)+r)*1e-5f;
    if(!isfinite(cx) || !isfinite(cy) || !isfinite(cz) || !isfinite(r))return unknown;
    return (StageBatchBound){cx,cy,cz,r};
}
static unsigned select_refined_groups(Stage *s,unsigned count,int store)
{
    unsigned table_bytes=s->h->n_batch*sizeof(unsigned);
    for(int priority=0;priority<2;priority++)for(unsigned i=0;i<s->h->n_batch;i++) {
        const JgsBatch *b=s->batch+i;
        if(b->n_idx<192 || !group_batch_indices_valid(s,b) || !!reference_group_count(s,b)!=(priority==0))continue;
        unsigned extra=(b->n_idx+95)/96;
        if(table_bytes+(count+extra)*sizeof(StageBatchBound)<=STAGE_CLIP_GROUP_BUDGET) {
            count+=extra;if(store)s->clip_groups[i]=STAGE_CLIP_REFINED;
        }
    }
    return count;
}
static void prepare_clip_groups(Stage *s)
{
    if(!s->h->n_batch || s->h->n_batch>16384)return;
    unsigned table_bytes=s->h->n_batch*sizeof(unsigned),base_count=0;
    for(unsigned i=0;i<s->h->n_batch;i++)base_count+=reference_group_count(s,s->batch+i);
    if(table_bytes+base_count*sizeof(StageBatchBound)>STAGE_CLIP_GROUP_BUDGET)return;
    unsigned count=select_refined_groups(s,base_count,0);
    unsigned bytes=table_bytes+count*sizeof(StageBatchBound);
    if(!count || !(s->clip_groups=malloc(bytes)))return;
    s->clip_group_bytes=bytes;s->group_bounds=(StageBatchBound *)(s->clip_groups+s->h->n_batch);
    for(unsigned i=0;i<s->h->n_batch;i++)s->clip_groups[i]=~0u;
    select_refined_groups(s,base_count,1);
    count=0;
    for(unsigned i=0;i<s->h->n_batch;i++) {
        const JgsBatch *b=s->batch+i;unsigned ref=reference_group_count(s,b);
        int finer=s->clip_groups[i]==STAGE_CLIP_REFINED;
        if(!ref && !finer)continue;
        s->clip_groups[i]=count|(finer ? STAGE_CLIP_REFINED:0);
        for(int level=0;level<2;level++) {
            if(level ? !finer:!ref)continue;
            unsigned step=level ? 96:STAGE_CLIP_GROUP_INDICES;
            for(unsigned start=0;start<b->n_idx;start+=step) {
                unsigned end=start+step;if(end>b->n_idx)end=b->n_idx;
                s->group_bounds[count++]=clip_group_bound(s,b,start,end);
            }
        }
    }
}
