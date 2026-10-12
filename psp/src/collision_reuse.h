#ifndef JGR_COLLISION_REUSE_H
#define JGR_COLLISION_REUSE_H
/* Exact input reuse on the game thread. Cache static geometry only; moving
   providers are tested on every ray. Replay ALL geometric hits to filters,
   including duplicate logical contacts, never a cached callback decision.
   Local copies keep callback-issued nested queries from evicting our data.
   Storage is bounded, independent of stage/actor counts, and heap-free. */
#define COL_REUSE_RAYS 32
#define COL_REUSE_BODY 16
#define COL_REUSE_HITS 8
typedef struct {
    const Collision *world;
    float ends[6];unsigned mask;
    int count,nearest_only;
    struct {SourceProbeHit hit;int id;} contact[COL_REUSE_HITS];
} ColRayReuse;
typedef struct {
    const Collision *world;
    float points[9],radius;unsigned mask;int capacity,count;
    ColBodyHit contact[COL_REUSE_HITS];
} ColBodyReuse;
#ifndef JGR_NO_RESULT_REUSE
static ColRayReuse ray_reuse[COL_REUSE_RAYS];
static ColBodyReuse body_reuse[COL_REUSE_BODY];
static void reuse_forget_moving(const Collision *c)
{
    /* Rays cache STATIC contacts and always retest moving providers. Body
       results currently contain both, so only those must be invalidated. */
    for(unsigned i=0;i<COL_REUSE_BODY;i++)if(body_reuse[i].world==c)body_reuse[i].world=NULL;
}
static void reuse_forget(const Collision *c)
{
    for(unsigned i=0;i<COL_REUSE_RAYS;i++)if(ray_reuse[i].world==c)ray_reuse[i].world=NULL;
    for(unsigned i=0;i<COL_REUSE_BODY;i++)if(body_reuse[i].world==c)body_reuse[i].world=NULL;
}
static unsigned reuse_hash(const void *data,unsigned words,unsigned hash)
{
    const unsigned char *p=data;
    for(unsigned i=0;i<words;i++){unsigned word;memcpy(&word,p+i*4,4);hash=(hash^word)*16777619u;hash^=hash>>16;}
    return hash;
}
static unsigned ray_reuse_slot(const float *a,const float *b,unsigned mask)
{return reuse_hash(b,3,reuse_hash(a,3,mask))&(COL_REUSE_RAYS-1);}
static int ray_reuse_read(const Collision *c,const float *a,const float *b,unsigned mask,int filtered,ColRayReuse *out)
{
    const ColRayReuse *q=ray_reuse+ray_reuse_slot(a,b,mask);
    if(q->world!=c || q->mask!=mask || (filtered && q->nearest_only) ||
       memcmp(q->ends,a,12) || memcmp(q->ends+3,b,12))return 0;
    *out=*q;return 1;
}
static void ray_reuse_write(const Collision *c,const float *a,const float *b,unsigned mask,ColRayReuse *q)
{
    q->world=c;q->mask=mask;memcpy(q->ends,a,12);memcpy(q->ends+3,b,12);
    ray_reuse[ray_reuse_slot(a,b,mask)]=*q;
}
static unsigned body_reuse_slot(const float *old,const float *a,const float *b,float radius,unsigned mask,int capacity)
{
    unsigned hash=reuse_hash(old,3,mask^(unsigned)capacity);
    hash=reuse_hash(b,3,reuse_hash(a,3,hash));
    return reuse_hash(&radius,1,hash)&(COL_REUSE_BODY-1);
}
#else
static void reuse_forget(const Collision *c){(void)c;}
static void reuse_forget_moving(const Collision *c){(void)c;}
#endif
#endif
