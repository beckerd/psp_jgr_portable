/* Final material draws: retain exact CPU skin/light results before clipping,
   fades and cel assembly. This is opportunistic CPU storage in the existing
   packet pool, never a GE submission address. The GE allocator may reclaim
   it at any time; this allocator never evicts a valid GE packet or a filled
   CPU result. Changing poses update small observations, not vertex buffers. */
#if CHAR_GE_SKINNING
#ifndef JGR_FINAL_VERTEX_METADATA_OBSERVATION
#ifdef __PSP__
#define JGR_FINAL_VERTEX_METADATA_OBSERVATION 1
#else
#define JGR_FINAL_VERTEX_METADATA_OBSERVATION 0
#endif
#endif
#define FINAL_VERTEX_MAX_OWNERS 8u
typedef struct {
    const void *header;
    const JgcChar *model;
    unsigned flags,vertices,owners;
    float direction[3],diffuse,ambient,rgb[3];
} FinalVertexKey;
typedef struct {unsigned owner;float matrix[16];} FinalVertexOwner;
static unsigned final_vertex_key_bytes(unsigned owners)
{return sizeof(FinalVertexKey)+owners*sizeof(FinalVertexOwner);}
static GePacket *final_vertex_page_owner(unsigned page)
{
    unsigned tag=ge_packet_owner[page];
    if(!tag)return NULL;
    GePacket *p=ge_packets+tag-1;
    return p->bank && p->pages && p->page<=page && page<p->page+p->pages ? p:NULL;
}
static void *final_vertex_memory(unsigned bytes,GePacket *slot,unsigned *page,unsigned *pages)
{
    unsigned needed=(bytes+255u)/256u;
    if(!ge_packets_active || !needed || needed>GE_PACKET_PAGES)return NULL;
    unsigned before=final_vertex_search_budget,virtual_steps=0;
    /* First reclaim stale tags. Only then displace small observations.
       Valid GE packets and admitted results are never displaced here. */
    for(unsigned replace=0;replace<2;replace++) {
#if JGR_FINAL_VERTEX_ADMISSION_RETRY && JGR_FINAL_VERTEX_METADATA_OBSERVATION
        if(replace && final_vertex_admission_retry_enabled) {
            /* Metadata observations own zero pages; every live CPU page
               belongs to an admitted count=1 result. Thus the second pair
               of sweeps has identical eligibility and cannot find a range.
               Debit its legacy budget exactly, preserving all later search
               limits/admission choices, but do not revisit those pages. */
            unsigned repeats=2u*GE_PACKET_PAGES-ge_packet_cursor;
            virtual_steps=repeats<final_vertex_search_budget ? repeats:final_vertex_search_budget;
            final_vertex_search_budget-=virtual_steps;
            final_vertex_profile.redundant_probes_avoided+=virtual_steps;
            goto exhausted;
        }
#endif
        for(unsigned pass=0;pass<2;pass++) {
        unsigned run=0;
        for(unsigned i=pass ? 0:ge_packet_cursor;i<GE_PACKET_PAGES;i++) {
            if(!final_vertex_search_budget)goto exhausted;
            final_vertex_search_budget--;
            GePacket *owner=final_vertex_page_owner(i);
            int usable=!ge_packet_pinned[i] && (!owner || owner==slot ||
                (replace && (owner->flags&GE_PACKET_FINAL) && !owner->count));
            run=usable ? run+1:0;if(run<needed)continue;
            unsigned first=i+1-needed;
            for(unsigned j=first;j<=i;j++) {
                GePacket *old=final_vertex_page_owner(j);
                if(old)old->bank=NULL;
                ge_packet_owner[j]=(unsigned short)(slot-ge_packets+1);
            }
            /* CPU-only: do not pin these pages against later GE demand. */
            ge_packet_cursor=(i+1)%GE_PACKET_PAGES;
            final_vertex_profile.search_steps+=before-final_vertex_search_budget;
            *page=first;*pages=needed;return ge_packet_memory+first*256u;
        }
        }
    }
exhausted:
    final_vertex_profile.search_steps+=before-final_vertex_search_budget-virtual_steps;
    return NULL;
}
static GePacket *final_vertex_slot(const Chars *c,const JgcChar *ch,const JgcBatch *b)
{
    size_t hash;unsigned ways;
    if(c->indexed_prepare){hash=final_packet_hash(c,b);ways=8;}
    else {hash=((size_t)c>>4)^((size_t)b>>4);hash^=hash>>9;hash^=0x53u;ways=4;}
    GePacket *set=ge_packets+(hash&(512u/ways-1u))*ways,*empty=NULL,*observation=NULL;
    for(unsigned i=0;i<ways;i++) {
        GePacket *p=set+i;
        if(p->bank==c && p->batch==b && (p->flags&GE_PACKET_FINAL) &&
           p->nbones==ch->nbones && p->end==(unsigned)(ch-c->chars)+1)return p;
        if(!p->bank)empty=p;
        else if((p->flags&GE_PACKET_FINAL) && !p->count)observation=p;
    }
    return empty ? empty:observation; /* never sacrifice a useful GE entry */
}
static unsigned final_vertex_owners(const Chars *c,const JgcChar *ch,const JgcBatch *b,unsigned out[FINAL_VERTEX_MAX_OWNERS])
{
    if(!c->batch_owner_offsets)return 0;
    unsigned index=(unsigned)(b-c->batch),first=c->batch_owner_offsets[index],end=c->batch_owner_offsets[index+1],count=0;
    const unsigned short *owners=(const unsigned short *)(c->batch_owner_offsets+c->h->n_batches+1);
    if(end-first>FINAL_VERTEX_MAX_OWNERS)return 0;
    for(unsigned i=first;i<end;i++) {
        unsigned owner=owners[i]<ch->nbones ? owners[i]:0,j;
        for(j=0;j<count && out[j]!=owner;j++);
        if(j==count)out[count++]=owner;
    }
    return count;
}
static FinalVertexKey final_vertex_key(const Chars *c,const JgcChar *ch,const JgcBatch *b,unsigned owners)
{
    unsigned cel=((b->flags>>12)&3)==2;
    FinalVertexKey key={0};key.header=c->h;key.model=ch;key.flags=b->flags;key.vertices=b->n_vert;key.owners=owners;
    key.direction[0]=cel ? -.01f:.09f;key.direction[1]=cel ? -.6f:.84f;key.direction[2]=cel ? .04f:-.01f;
    if(!cel) {float n=sqrtf((key.direction[0]*key.direction[0]+key.direction[1]*key.direction[1])+key.direction[2]*key.direction[2]);
        for(unsigned i=0;i<3;i++)key.direction[i]/=n;}
    key.diffuse=cel ? 1.f:.95f;key.ambient=cel ? 0.f:.73f;
    key.rgb[0]=.64f;key.rgb[1]=.83f;key.rgb[2]=1.f;return key;
}
static int final_vertex_matches(const GePacket *p,const FinalVertexKey *key,const unsigned *owners,float mats[][16])
{
    const FinalVertexKey *old=p->vertices;
    /* Compare named fields, avoiding host pointer-alignment padding. */
    if(old->header!=key->header || old->model!=key->model || old->flags!=key->flags ||
       old->vertices!=key->vertices || old->owners!=key->owners ||
       memcmp(old->direction,key->direction,sizeof key->direction) ||
       memcmp(&old->diffuse,&key->diffuse,2*sizeof(float)) || memcmp(old->rgb,key->rgb,sizeof key->rgb))return 0;
    const FinalVertexOwner *record=(const FinalVertexOwner *)(old+1);
    for(unsigned i=0;i<key->owners;i++)if(record[i].owner!=owners[i] || memcmp(record[i].matrix,mats[owners[i]],64))return 0;
    return 1;
}
static void final_vertex_write_key(void *memory,const FinalVertexKey *key,const unsigned *owners,float mats[][16])
{
    memcpy(memory,key,sizeof *key);FinalVertexOwner *record=(FinalVertexOwner *)((FinalVertexKey *)memory+1);
    for(unsigned i=0;i<key->owners;i++){record[i].owner=owners[i];memcpy(record[i].matrix,mats[owners[i]],64);}
}
#if JGR_FINAL_VERTEX_METADATA_OBSERVATION
/* Admission hint only. Never use a fingerprint as a geometry/cache hit:
   the next matching observation skins normally and stores its complete
   exact key. A collision therefore only admits a changing pose early. */
static unsigned final_vertex_observation_word(unsigned hash,unsigned word)
{return (hash^word)*16777619u;}
static unsigned final_vertex_observation_hash(const FinalVertexKey *key,const unsigned *owners,float mats[][16])
{
    unsigned hash=2166136261u;
    hash=final_vertex_observation_word(hash,key->flags);
    hash=final_vertex_observation_word(hash,key->vertices);
    hash=final_vertex_observation_word(hash,key->owners);
    for(unsigned i=0;i<key->owners;i++) {
        hash=final_vertex_observation_word(hash,owners[i]);
        for(unsigned k=0;k<16;k++) {
            unsigned bits;memcpy(&bits,mats[owners[i]]+k,4);
            hash=final_vertex_observation_word(hash,bits);
        }
    }
    /* Bank/model identity is compared by final_vertex_slot. Light inputs
       are fixed in this eligible context; all custom contexts bypass it. */
    return hash;
}
#endif
static GePacket *final_vertex_lookup(const Chars *c,const JgcChar *ch,const JgcBatch *b,float mats[][16])
{
    if(!final_vertex_enabled || !source_lighting_enabled)return NULL;
    unsigned lighting=(b->flags>>12)&3;
    if(!ge_packets_active || c->vert_stride!=40 || c->lighting || (b->flags&(8|2048)) ||
       (lighting!=1 && lighting!=2) || recovery_active || event_actor_material || spray_hud_lighting ||
       scene_light[0] || scene_light[1] || hud_reciprocal_scale!=1.f || hud_depth_shift!=0.f ||
       !b->n_vert || b->n_vert>CHAR_CPU_SKIN_VERTICES || !ch->nbones || ch->nbones>CHAR_RENDER_MAX_BONES) {
        final_vertex_profile.rejections++;return NULL;
    }
    GePacket *slot=final_vertex_slot(c,ch,b);
    if(!slot){final_vertex_profile.rejections++;return NULL;}
    int existing=slot->bank==c && slot->batch==b && (slot->flags&GE_PACKET_FINAL);
    if(existing && !slot->pages && slot->source_nidx==b->flags) {
        final_vertex_profile.rejections++;return NULL;
    }
    if(!existing && lighting==1) {
        /* Many Final scenery batches carry a normal-light material code
           but no normal-present vertices. They do no expensive lighting;
           preserve the pool for real lit/cel results. Cache this immutable
           classification using metadata alone, without a vertex buffer. */
        const unsigned char *source=chars_batch_vertices(c,b);unsigned i=0;
        for(;i<b->n_vert;i++)if(((const SkinVert *)(source+i*c->vert_stride))->pad&1)break;
        if(i==b->n_vert) {
            *slot=(GePacket){.bank=c,.batch=b,.nbones=ch->nbones,.flags=GE_PACKET_FINAL,
                .end=(unsigned)(ch-c->chars)+1,.source_nidx=b->flags};
            final_vertex_profile.rejections++;return NULL;
        }
    }
    unsigned owners[FINAL_VERTEX_MAX_OWNERS],count=final_vertex_owners(c,ch,b,owners);
    if(!count){final_vertex_profile.rejections++;return NULL;}
    FinalVertexKey key=final_vertex_key(c,ch,b,count);
    int same=existing && slot->pages &&
        final_vertex_matches(slot,&key,owners,mats);
    if(same && slot->count) {
        unsigned bytes=b->n_vert*sizeof(DrawVert),offset=final_vertex_key_bytes(count);
        memcpy(cached_skin_vertices,(const unsigned char *)slot->vertices+offset,bytes);
        cel_values=NULL;
        if(lighting==2) {
            memcpy(cpu_cel_values,(const unsigned char *)slot->vertices+offset+bytes,b->n_vert*sizeof(float));
            memset(cpu_cel_exact,255,(b->n_vert+7)/8);cel_values=cpu_cel_values;cel_exact=cpu_cel_exact;
            cel_source.bank=c;cel_source.ch=ch;cel_source.batch=b;cel_source.diffuse=key.diffuse;cel_source.ambient=key.ambient;
        }
        final_vertex_profile.hits++;final_vertex_profile.vertices+=b->n_vert;return slot;
    }
    if(same)return slot; /* second unchanged observation: fill after skinning */
#if JGR_FINAL_VERTEX_METADATA_OBSERVATION
    unsigned fingerprint=final_vertex_observation_hash(&key,owners,mats),previous;
    memcpy(&previous,slot->bones,sizeof previous);
    if(existing && !slot->pages && !slot->count && slot->nvert==b->n_vert &&
       previous==fingerprint)return slot; /* Skin first; final_vertex_store writes the exact current key. */
    if(slot->bank && (slot->flags&GE_PACKET_FINAL) && slot->count)final_vertex_profile.evictions++;
    /* A zero-page observation has nvert>0 and source_nidx=0. Immutable
       no-normal negatives retain nvert=0/source_nidx=the nonzero flags.
       Dropping old CPU page metadata releases its ownership; independent
       GPU page pins remain untouched even if a slot has been replaced. */
    *slot=(GePacket){.bank=c,.batch=b,.nbones=ch->nbones,.flags=GE_PACKET_FINAL,
        .end=(unsigned)(ch-c->chars)+1,.nvert=b->n_vert};
    memcpy(slot->bones,&fingerprint,sizeof fingerprint);
    final_vertex_profile.observations++;return NULL;
#else
    unsigned page=0,pages=0,key_bytes=final_vertex_key_bytes(count);void *memory=NULL;
    if(slot->bank && (slot->flags&GE_PACKET_FINAL) && slot->pages*256u>=key_bytes) {
        /* A changing full result becomes a small observation; relinquish
           excess pages rather than holding a useless vertex allocation.
           A hash collision may replace another observation in place too;
           allocating new pages for that case caused repeated pool scans. */
        if(slot->count)final_vertex_profile.evictions++;
        page=slot->page;pages=(key_bytes+255u)/256u;memory=slot->vertices;
    } else memory=final_vertex_memory(key_bytes,slot,&page,&pages);
    if(!memory){final_vertex_profile.rejections++;return NULL;}
    final_vertex_write_key(memory,&key,owners,mats);
    *slot=(GePacket){.bank=c,.batch=b,.vertices=memory,.nbones=ch->nbones,.flags=GE_PACKET_FINAL,
        .page=page,.pages=pages,.end=(unsigned)(ch-c->chars)+1,.nvert=b->n_vert};
    final_vertex_profile.observations++;return NULL;
#endif
}
static void final_vertex_store(GePacket *slot,const Chars *c,const JgcChar *ch,const JgcBatch *b,float mats[][16],const DrawVert *vertices)
{
    unsigned owners[FINAL_VERTEX_MAX_OWNERS],count=final_vertex_owners(c,ch,b,owners);
    if(!count)return;
    FinalVertexKey key=final_vertex_key(c,ch,b,count);
    unsigned key_bytes=final_vertex_key_bytes(count),vertex_bytes=b->n_vert*sizeof(DrawVert);
    unsigned value_bytes=cel_values ? b->n_vert*sizeof(float):0,bytes=key_bytes+vertex_bytes+value_bytes,page=0,pages=0;
    final_vertex_profile.admission_attempts++;
    void *memory=final_vertex_memory(bytes,slot,&page,&pages);
    if(!memory){
        final_vertex_profile.admission_failures++;final_vertex_profile.rejections++;
        return;
    }
    final_vertex_write_key(memory,&key,owners,mats);
    memcpy((unsigned char *)memory+key_bytes,vertices,vertex_bytes);
    if(cel_values) {
        /* Preserve the platform's existing boundary policy, not just the
           +/- side markers. Every hit can then consume exact saved bits. */
        for(unsigned i=0;i<b->n_vert;i++)cel_boundary_value(i);
        memcpy((unsigned char *)memory+key_bytes+vertex_bytes,cel_values,value_bytes);
    }
    *slot=(GePacket){.bank=c,.batch=b,.vertices=memory,.nbones=ch->nbones,.flags=GE_PACKET_FINAL,.count=1,
        .page=page,.pages=pages,.end=(unsigned)(ch-c->chars)+1,.nvert=b->n_vert};
    final_vertex_profile.fills++;final_vertex_profile.bytes+=vertex_bytes+value_bytes;
}
#else
static GePacket *final_vertex_lookup(const Chars *c,const JgcChar *ch,const JgcBatch *b,float mats[][16])
{(void)c;(void)ch;(void)b;(void)mats;return NULL;}
static void final_vertex_store(GePacket *p,const Chars *c,const JgcChar *ch,const JgcBatch *b,float mats[][16],const DrawVert *v)
{(void)p;(void)c;(void)ch;(void)b;(void)mats;(void)v;}
#endif
