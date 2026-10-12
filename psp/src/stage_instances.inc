/* Shared local meshes retain authored world-space culling units. The
   optional extension lives in immutable JGS metadata, not a sidecar. */
#define STAGE_PROP_DRAW_SLOTS 32u
typedef struct {
    unsigned object,valid;int mask;
    float delta[16],planes[6][4],guard[6][4];
    ScePspFMatrix4 model;
} StagePropPrepared;
typedef struct {
    float clip[16],view_key[7];unsigned clip_valid,view_valid;
    ObjectView view;
    StagePropPrepared entry[STAGE_PROP_DRAW_SLOTS];
} StagePropDrawCache;
static void stage_instance_point(const StageInstance *instance,const JgsVert *vertex,float out[3])
{
    const float p[3]={vertex->x,vertex->y,vertex->z};
    if(!instance){memcpy(out,p,12);return;}
    const float *m=instance->matrix;
    for(unsigned k=0;k<3;k++)out[k]=((m[k*4]*p[0]+m[k*4+1]*p[1])+m[k*4+2]*p[2])+m[k*4+3];
}
static int stage_bind_instances(Stage *s)
{
    unsigned end=s->h->off_batch+s->h->n_batch*sizeof(JgsBatch),offset=(end+15u)&~15u;
    unsigned instances=0;
    for(unsigned i=0;i<s->h->n_obj;i++)instances+=!!(s->obj[i].flags&JGS_O_INSTANCE);
    if(offset>s->h->off_vert || s->h->off_vert-offset<16 || memcmp(s->base+offset,"JGI1",4))
        return instances ? -4:0;
    const unsigned *header=(const void *)(s->base+offset);
    unsigned count=header[2],directory=offset+16,records=(directory+s->h->n_obj*2u+15u)&~15u;
    if(header[1]!=s->h->n_obj || header[3] || !count || count>s->h->n_obj ||
       records>s->h->off_vert || count>(s->h->off_vert-records)/sizeof(StageInstance))return -4;
    s->object_instance=(const void *)(s->base+directory);
    s->instances=(const void *)(s->base+records);s->instance_count=count;
    for(unsigned i=0;i<s->h->n_obj;i++) {
        unsigned slot=s->object_instance[i];
        if(slot>count || !!slot!=!!(s->obj[i].flags&JGS_O_INSTANCE))return -4;
    }
    for(unsigned i=0;i<count;i++) {
        const StageInstance *instance=s->instances+i;
        for(unsigned k=0;k<12;k++)if(!isfinite(instance->matrix[k]))return -4;
        const float *m=instance->matrix;
        float determinant=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
        if(!isfinite(determinant) || determinant==0.f)return -4;
        const float *bound=&instance->bound.cx;
        for(unsigned k=0;k<4;k++)if(!isfinite(bound[k]))return -4;
        if(bound[3]<0.f)return -4;
    }
    return 0;
}
static unsigned stage_object_pass_mask(const Stage *s,unsigned object)
{
    if(s->object_passes)return s->object_passes[object]&3u;
    const JgsObj *o=s->obj+object;unsigned mask=0;
    for(unsigned b=o->first_batch;b<o->first_batch+o->n_batch;b++)
        if(s->batch[b].n_idx)mask|=1u<<stage_batch_alpha(s,s->batch+b);
    return mask;
}
static void stage_prepare_object_passes(Stage *s)
{
    unsigned char *passes=malloc(s->h->n_obj);
    if(passes) {
        for(unsigned i=0;i<s->h->n_obj;i++) {
            unsigned city=0;const JgsObj *o=s->obj+i;
            for(unsigned b=o->first_batch;b<o->first_batch+o->n_batch;b++) {
                const JgsBatch *batch=s->batch+b;
                /* Static traversal also applies material/decal state for
                   zero-index batches. Kogane's verified tunnel override
                   changes tex59 membership after JRS binding; retain both
                   possible passes without changing the prop cache below. */
                city|=batch->tex==59 ? 3u:1u<<!!(batch->flags&JGS_B_ALPHA);
            }
            passes[i]=(unsigned char)(stage_object_pass_mask(s,i)|(city<<2));
        }
        s->object_passes=passes;
    }
    /* The cache contains CPU values only; no transient GE-list pointers.
       A failed optional allocation leaves the identical uncached path. */
    if(s->h->n_obj>128)s->prop_draw_cache=calloc(1,sizeof(StagePropDrawCache));
}
