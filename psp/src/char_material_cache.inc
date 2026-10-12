/* Scoped normal-lit props only. Cache the EXACT pre-clip CPU output, then
   copy it to the ordinary CPU scratch. Sorting, clipping, fades and GE
   publication still run every draw. Eviction cannot overwrite live GE
   vertices, and a camera change needs no invalidation. */
#define MATERIAL_CACHE_SLOTS 256u
#define MATERIAL_CACHE_VERTICES 1024u
typedef struct {
    const Chars *bank;
    const void *instance;
    const JgcBatch *batch;
    unsigned revision;
    unsigned short first,count,next,bucket;
} MaterialCacheEntry;
static MaterialCacheEntry material_cache_entries[MATERIAL_CACHE_SLOTS];
static unsigned short material_cache_buckets[64]; /* index + 1, zero = end */
static DrawVert material_cache_vertices[MATERIAL_CACHE_VERTICES];
static const void *material_cache_instance;
static unsigned material_cache_revision,material_cache_cursor,material_cache_victim;
static CharMaterialCacheProfile material_cache_profile;
static int material_cache_enabled=1;
#ifdef __PSP__
typedef char material_cache_budget[(sizeof material_cache_entries+sizeof material_cache_vertices+
    sizeof material_cache_buckets+sizeof material_cache_profile+32<=CHAR_MATERIAL_CACHE_BYTES) ? 1:-1];
#endif
void chars_material_cache_scope(const void *instance,unsigned revision)
{material_cache_instance=instance;material_cache_revision=revision;}
void chars_material_cache_mode(int enabled){material_cache_enabled=!!enabled;}
void chars_material_cache_profile(CharMaterialCacheProfile *out)
{*out=material_cache_profile;memset(&material_cache_profile,0,sizeof material_cache_profile);}
static void material_cache_remove(MaterialCacheEntry *entry)
{
    if(!entry->bank)return;
    unsigned short *link=material_cache_buckets+entry->bucket;
    while(*link) {
        MaterialCacheEntry *p=material_cache_entries+*link-1;
        if(p==entry){*link=p->next;break;}
        link=&p->next;
    }
    entry->bank=NULL;entry->next=0;
}
static void forget_material_cache(const Chars *c)
{for(unsigned i=0;i<MATERIAL_CACHE_SLOTS;i++)if(material_cache_entries[i].bank==c)material_cache_remove(material_cache_entries+i);}
static MaterialCacheEntry *material_cache_lookup(const Chars *c,const JgcBatch *b)
{
    /* Recovery pulses, environment UVs, cel splits and retained player
       lighting have extra mutable inputs and keep the original path. */
    if(!material_cache_enabled || !material_cache_instance || !scene_light[0] ||
       c->vert_stride!=40 || c->lighting || ((b->flags>>12)&3)!=1 ||
       (b->flags&(8|2048)) || recovery_active || event_actor_material ||
       spray_hud_lighting || hud_reciprocal_scale!=1.f || hud_depth_shift!=0.f ||
       !b->n_vert || b->n_vert>MATERIAL_CACHE_VERTICES || b->n_vert>CHAR_CPU_SKIN_VERTICES)return NULL;
    size_t hash=((size_t)material_cache_instance>>4)^((size_t)c>>4);
    hash^=(size_t)(b-c->batch)*2654435761u;hash^=hash>>16;
    unsigned bucket=hash&63u;
    MaterialCacheEntry *slot=NULL;
    for(unsigned at=material_cache_buckets[bucket];at;at=material_cache_entries[at-1].next) {
        MaterialCacheEntry *p=material_cache_entries+at-1;
        if(p->bank==c && p->instance==material_cache_instance && p->batch==b) {
            if(p->revision==material_cache_revision && p->count==b->n_vert) {
                material_cache_profile.hits++;material_cache_profile.vertices+=b->n_vert;return p;
            }
            slot=p;break;
        }
    }
    if(!slot) {
        unsigned i=0;
        while(i<MATERIAL_CACHE_SLOTS && material_cache_entries[material_cache_victim].bank) {
            material_cache_victim=(material_cache_victim+1)%MATERIAL_CACHE_SLOTS;i++;
        }
        slot=material_cache_entries+material_cache_victim;
        material_cache_victim=(material_cache_victim+1)%MATERIAL_CACHE_SLOTS;
    }
    if(slot->bank)material_cache_profile.evictions++;
    material_cache_remove(slot);slot->bucket=bucket;
    material_cache_profile.misses++;return slot;
}
static void material_cache_store(MaterialCacheEntry *slot,const Chars *c,const JgcBatch *b,const DrawVert *v)
{
    unsigned first=material_cache_cursor;
    if(first+b->n_vert>MATERIAL_CACHE_VERTICES)first=0;
    for(unsigned i=0;i<MATERIAL_CACHE_SLOTS;i++) {
        MaterialCacheEntry *p=material_cache_entries+i;
        if(p->bank && first<p->first+p->count && p->first<first+b->n_vert) {
            material_cache_remove(p);material_cache_profile.evictions++;
        }
    }
    memcpy(material_cache_vertices+first,v,b->n_vert*sizeof *v);
    unsigned bucket=slot->bucket;
    *slot=(MaterialCacheEntry){c,material_cache_instance,b,material_cache_revision,first,b->n_vert,
        material_cache_buckets[bucket],bucket};
    material_cache_buckets[bucket]=(unsigned short)(slot-material_cache_entries+1);
    material_cache_cursor=first+b->n_vert;
}
