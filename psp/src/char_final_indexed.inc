/* A source-vertex cache for one posed Final instance. Immutable load-time
   IDs share position/light work across strips; UVs and submission order stay
   with their original records. No runtime hashing or retained GE pointers. */
#define FINAL_INDEXED_SLOTS 512u
#define FINAL_INDEXED_NONE 65535u
#define FINAL_INDEXED_EXACT 0x80000000u
#define FINAL_INDEXED_GENERATION 0x7fffffffu
typedef struct {unsigned generation;float x,y,z;unsigned color;float cel;} FinalIndexedValue;
typedef struct {unsigned short batch,vertex;} FinalIndexedRef;
typedef struct {unsigned short offset,nvert,flags,reserved;} FinalIndexedBatch;
typedef struct {const void *header;unsigned bytes,nbatches,unlit_offset;
#if JGR_FINAL_GE_NORMAL
    unsigned normal_offset;
#endif
#if JGR_FINAL_FLOOR_BOUNDS
    unsigned floor_offset;
#endif
    FinalIndexedBatch batch[];} FinalIndexedPlan;
#if JGR_FINAL_GE_NORMAL
static void final_ge_normal_prepare(const Chars *,FinalIndexedPlan *);
#endif
static union {FinalIndexedValue value[FINAL_INDEXED_SLOTS];FinalIndexedRef ref[FINAL_INDEXED_SLOTS*6];} final_indexed_work;
static const Chars *final_indexed_bank,*final_indexed_sort_bank;
static const JgcChar *final_indexed_model,*final_indexed_sort_model;
static const unsigned short *final_indexed_current;
static unsigned final_indexed_generation,final_indexed_map_bytes;
static int final_indexed_enabled=1;
static CharFinalIndexedProfile final_indexed_profile;
#include "char_final_floor_bounds.inc"
#ifdef __PSP__
typedef char final_indexed_budget[(sizeof final_indexed_work+sizeof final_indexed_profile+
    5*sizeof(void *)+3*sizeof(unsigned)<=CHAR_FINAL_INDEXED_WORK_BYTES) ? 1:-1];
#endif
void chars_final_indexed_mode(int enabled){final_indexed_enabled=!!enabled;final_indexed_current=NULL;final_indexed_bank=NULL;}
void chars_final_indexed_profile(CharFinalIndexedProfile *out)
{*out=final_indexed_profile;out->map_bytes=final_indexed_map_bytes;memset(&final_indexed_profile,0,sizeof final_indexed_profile);}
static void final_indexed_reset(void)
{final_indexed_bank=NULL;final_indexed_model=NULL;final_indexed_current=NULL;}
static void final_indexed_forget(Chars *c)
{
    if(final_indexed_bank==c){final_indexed_bank=NULL;final_indexed_current=NULL;}
    FinalIndexedPlan *p=c->indexed_prepare;
    if(p){final_indexed_map_bytes-=p->bytes;free(p);c->indexed_prepare=NULL;}
}
static const SkinVert *final_indexed_source(const FinalIndexedRef *r)
{return (const SkinVert *)(chars_batch_vertices(final_indexed_sort_bank,final_indexed_sort_bank->batch+r->batch)+40u*r->vertex);}
static int final_indexed_compare(const void *aa,const void *bb)
{
    const FinalIndexedRef *a=aa,*b=bb;
    const SkinVert *va=final_indexed_source(a),*vb=final_indexed_source(b);
    unsigned ba=va->bone<final_indexed_sort_model->nbones ? va->bone:0;
    unsigned bb_=vb->bone<final_indexed_sort_model->nbones ? vb->bone:0;
    if(ba!=bb_)return ba<bb_ ? -1:1;
    int d=memcmp(&va->x,&vb->x,12);if(d)return d;
    d=memcmp((const unsigned char *)va+28,(const unsigned char *)vb+28,12);if(d)return d;
    if(va->pad!=vb->pad)return va->pad<vb->pad ? -1:1;
    unsigned la=(final_indexed_sort_bank->batch[a->batch].flags>>12)&3;
    unsigned lb=(final_indexed_sort_bank->batch[b->batch].flags>>12)&3;
    if(la!=lb)return la<lb ? -1:1;
    return va->color==vb->color ? 0:va->color<vb->color ? -1:1;
}
static int final_indexed_eligible_batch(const Chars *c,const JgcBatch *b)
{
    unsigned mode=(b->flags>>12)&3;
    if(!b->n_vert || b->n_vert>CHAR_CPU_SKIN_VERTICES || (b->flags&(8|2048)) || (mode!=1 && mode!=2))return 0;
    const unsigned char *source=chars_batch_vertices(c,b);unsigned normals=0;
    for(unsigned i=0;i<b->n_vert;i++) {
        const SkinVert *v=(const SkinVert *)(source+40u*i);
        const float *n=(const float *)(source+40u*i+28u);
        if(!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z) ||
           !isfinite(n[0]) || !isfinite(n[1]) || !isfinite(n[2]))return 0;
        normals|=v->pad&1;
    }
    return mode==2 || normals;
}
/* Mode 1 without a normal is copied unchanged by skin_batch. Classify once
   for explicitly prepared Final banks; never rewrite authored flags. */
static int final_indexed_normal_free(const Chars *c,const JgcBatch *b)
{
    if(!b->n_vert || ((b->flags>>12)&3)!=1 || (b->flags&(8|2048)))return 0;
    const unsigned char *source=chars_batch_vertices(c,b);
    for(unsigned i=0;i<b->n_vert;i++) {
        const SkinVert *v=(const SkinVert *)(source+40u*i);
        if((v->pad&1) || !isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z))return 0;
    }
    return 1;
}
static int final_indexed_ge_unlit(const Chars *c,const JgcBatch *b)
{
    const FinalIndexedPlan *p=c->indexed_prepare;
    if(!p || p->header!=c->h || c->vert_stride!=40 || c->lighting ||
       !source_lighting_enabled || recovery_active || event_actor_material ||
       spray_hud_lighting || scene_light[0] || scene_light[1] ||
       hud_reciprocal_scale!=1.f || hud_depth_shift!=0.f)return 0;
    unsigned i=(unsigned)(b-c->batch);
    if(i>=p->nbatches || p->batch[i].nvert!=b->n_vert || p->batch[i].flags!=b->flags)return 0;
    const unsigned *bits=(const unsigned *)((const unsigned char *)p+p->unlit_offset);
    return !!(bits[i/32]&(1u<<(i%32)));
}
int chars_final_indexed_prepare(Chars *c)
{
    if(!c)return -1;
    final_indexed_forget(c);final_indexed_bank=NULL;final_indexed_current=NULL;
    if(!c->h || c->vert_stride!=40 || c->lighting || c->h->n_batches>65535u)return -1;
    unsigned unlit_offset=sizeof(FinalIndexedPlan)+c->h->n_batches*sizeof(FinalIndexedBatch);
    unsigned bytes=unlit_offset+((c->h->n_batches+31u)/32u)*sizeof(unsigned);
#if JGR_FINAL_GE_NORMAL
    unsigned normal_offset=bytes;
    bytes+=((c->h->n_batches+31u)/32u)*sizeof(unsigned);
#endif
    if(bytes>CHAR_FINAL_INDEXED_MAP_BYTES)return -2;
    for(unsigned i=0;i<c->h->n_batches;i++)if(final_indexed_eligible_batch(c,c->batch+i)) {
        if(2u*c->batch[i].n_vert>CHAR_FINAL_INDEXED_MAP_BYTES-bytes)return -2;
        bytes+=2u*c->batch[i].n_vert;
    }
    if(bytes>CHAR_FINAL_INDEXED_MAP_BYTES-final_indexed_map_bytes)return -2;
    unsigned base_bytes=bytes;
#if JGR_FINAL_FLOOR_BOUNDS
    unsigned floor_bytes=final_floor_bounds_bytes(c),floor_offset=(bytes+3u)&~3u;
    if(floor_bytes && floor_offset<=CHAR_FINAL_INDEXED_MAP_BYTES-final_indexed_map_bytes &&
       floor_bytes<=CHAR_FINAL_INDEXED_MAP_BYTES-final_indexed_map_bytes-floor_offset)bytes=floor_offset+floor_bytes;
    else floor_offset=0;
#endif
    FinalIndexedPlan *p=calloc(1,bytes);
    if(!p && bytes!=base_bytes){bytes=base_bytes;p=calloc(1,bytes);}
    if(!p)return -3;
    p->header=c->h;p->bytes=bytes;p->nbatches=c->h->n_batches;p->unlit_offset=unlit_offset;
    unsigned *unlit=(unsigned *)((unsigned char *)p+unlit_offset);
    unsigned cursor=unlit_offset+((p->nbatches+31u)/32u)*sizeof(unsigned);
#if JGR_FINAL_GE_NORMAL
    p->normal_offset=normal_offset;cursor+=((p->nbatches+31u)/32u)*sizeof(unsigned);
#endif
    for(unsigned i=0;i<p->nbatches;i++) {
        p->batch[i].nvert=c->batch[i].n_vert;p->batch[i].flags=c->batch[i].flags;
        if(final_indexed_normal_free(c,c->batch+i))unlit[i/32]|=1u<<(i%32);
        if(final_indexed_eligible_batch(c,c->batch+i)) {
            p->batch[i].offset=cursor;
            memset((unsigned char *)p+cursor,255,2u*c->batch[i].n_vert);cursor+=2u*c->batch[i].n_vert;
        }
    }
    final_indexed_sort_bank=c;
    for(unsigned model=0;model<c->h->n_chars;model++) {
        const JgcChar *ch=c->chars+model;unsigned count=0,overflow=0;
        if(model>=65535u || !ch->nbones || ch->nbones>CHAR_RENDER_MAX_BONES || ch->bone0>c->h->n_bones || ch->nbones>c->h->n_bones-ch->bone0)continue;
        for(unsigned bone=0;bone<ch->nbones;bone++) {
            const JgcBone *b=c->bones+ch->bone0+bone;
            if(b->batch0>p->nbatches || b->nbatch>p->nbatches-b->batch0){overflow=1;break;}
            for(unsigned j=b->batch0;j<b->batch0+b->nbatch;j++)if(p->batch[j].offset) {
                if(p->batch[j].reserved && p->batch[j].reserved!=model+1){p->batch[j].offset=0;continue;}
                p->batch[j].reserved=model+1;
                if(c->batch[j].n_vert>FINAL_INDEXED_SLOTS*6u-count){overflow=1;break;}
                for(unsigned v=0;v<c->batch[j].n_vert;v++)final_indexed_work.ref[count++]=(FinalIndexedRef){j,v};
            }
            if(overflow)break;
        }
        if(overflow)continue;
        final_indexed_sort_model=ch;
        qsort(final_indexed_work.ref,count,sizeof(FinalIndexedRef),final_indexed_compare);
        unsigned slots=0;
        for(unsigned i=0;i<count;) {
            unsigned end=i+1;while(end<count && !final_indexed_compare(final_indexed_work.ref+i,final_indexed_work.ref+end))end++;
            if(end-i>1) {
                if(slots==FINAL_INDEXED_SLOTS){overflow=1;break;}
                for(unsigned j=i;j<end;j++) {
                    const FinalIndexedRef *r=final_indexed_work.ref+j;
                    ((unsigned short *)((unsigned char *)p+p->batch[r->batch].offset))[r->vertex]=slots;
                }
                slots++;
            }
            i=end;
        }
        if(overflow)for(unsigned i=0;i<count;i++) {
            const FinalIndexedRef *r=final_indexed_work.ref+i;
            ((unsigned short *)((unsigned char *)p+p->batch[r->batch].offset))[r->vertex]=FINAL_INDEXED_NONE;
        }
        asset_progress();
    }
    for(unsigned i=0;i<p->nbatches;i++)if(p->batch[i].offset) {
        const unsigned short *map=(const unsigned short *)((const unsigned char *)p+p->batch[i].offset);unsigned j=0;
        for(;j<p->batch[i].nvert && map[j]==FINAL_INDEXED_NONE;j++);
        if(j==p->batch[i].nvert)p->batch[i].offset=0;
    }
    memset(&final_indexed_work,0,sizeof final_indexed_work);final_indexed_generation=0;
    final_indexed_sort_bank=NULL;final_indexed_sort_model=NULL;
#if JGR_FINAL_GE_NORMAL
    final_ge_normal_prepare(c,p);
#endif
#if JGR_FINAL_FLOOR_BOUNDS
    if(bytes!=base_bytes)final_floor_bounds_prepare(c,p,floor_offset);
#endif
    c->indexed_prepare=p;final_indexed_map_bytes+=bytes;return 0;
}
static void final_indexed_begin(const Chars *c,const JgcChar *ch)
{
    final_indexed_current=NULL;final_indexed_bank=NULL;final_indexed_model=NULL;
    if(!final_indexed_enabled || !source_lighting_enabled || !c->indexed_prepare)return;
    if(++final_indexed_generation>=FINAL_INDEXED_EXACT) {
        memset(&final_indexed_work,0,sizeof final_indexed_work);final_indexed_generation=1;
    }
    final_indexed_bank=c;final_indexed_model=ch;final_indexed_profile.actors++;
}
static const unsigned short *final_indexed_batch(const Chars *c,const JgcChar *ch,const JgcBatch *b)
{
    if(final_indexed_bank!=c || final_indexed_model!=ch)return NULL;
    const FinalIndexedPlan *p=c->indexed_prepare;unsigned i=(unsigned)(b-c->batch);
    if(!p || p->header!=c->h || i>=p->nbatches || c->vert_stride!=40 || c->lighting ||
       recovery_active || event_actor_material || spray_hud_lighting || scene_light[0] || scene_light[1] ||
       hud_reciprocal_scale!=1.f || hud_depth_shift!=0.f || !source_lighting_enabled ||
       !p->batch[i].offset || p->batch[i].reserved!=(unsigned)(ch-c->chars)+1 || p->batch[i].nvert!=b->n_vert || p->batch[i].flags!=b->flags) {
        final_indexed_profile.fallback_batches++;return NULL;
    }
    final_indexed_profile.batches++;final_indexed_profile.source_vertices+=b->n_vert;
    return (const unsigned short *)((const unsigned char *)p+p->batch[i].offset);
}
static float final_indexed_resolved(unsigned index,float value)
{
    if(!final_indexed_current)return value;
    unsigned slot=final_indexed_current[index];if(slot==FINAL_INDEXED_NONE)return value;
    FinalIndexedValue *p=final_indexed_work.value+slot;
    if((p->generation&FINAL_INDEXED_GENERATION)==final_indexed_generation){p->cel=value;p->generation|=FINAL_INDEXED_EXACT;}
    return value;
}
