/* Experimental post-cel packets for the immobile Final satellite surface.
   Payload uses the existing GPU page arena; no private vertex allocation.
   Unlike the pre-cut CPU cache, every submitted page remains pinned. */
#if JGR_FINAL_CEL_PACKET && CHAR_GE_SKINNING
#define FINAL_CEL_PACKET_LIMIT (64u*1024u)
typedef struct {
    FinalVertexKey skin;
    FinalVertexOwner owner;
    const unsigned short *source_indices;
    unsigned source_count;
} FinalCelPacketKey;
static struct {
    const Chars *bank;
    const JgcBatch *batch;
    const JgcChar *model;
    FinalCelPacketKey key;
    unsigned ready;
} final_cel_pending;
/* Conservatively retain bits for orphaned same-frame submissions and pages
   reused by another packet type. The frame fence permits rebuilding this
   accounting from live metadata next frame. Thus metadata replacement never
   hides live post-cel storage from the 64 KiB budget. */
static unsigned char final_cel_pages[(GE_PACKET_PAGES+7u)/8u];
static int final_cel_enabled=1;
static CharFinalCelPacketProfile final_cel_profile;
#ifdef __PSP__
typedef char final_cel_metadata_budget[(sizeof final_cel_pending+sizeof final_cel_pages+
    sizeof final_cel_enabled+sizeof final_cel_profile<=CHAR_FINAL_CEL_PACKET_BYTES) ? 1:-1];
#endif
static unsigned final_cel_key_bytes(void)
{return (sizeof(FinalCelPacketKey)+15u)&~15u;}
static unsigned final_cel_resident_pages(void)
{
    unsigned total=0;
    for(unsigned i=0;i<GE_PACKET_PAGES;i++)total+=!!(final_cel_pages[i>>3]&(1u<<(i&7)));
    return total;
}
static void final_cel_mark(unsigned page,unsigned count,int set)
{
    for(unsigned i=page;i<page+count;i++) {
        if(set)final_cel_pages[i>>3]|=1u<<(i&7);
        else final_cel_pages[i>>3]&=~(1u<<(i&7));
    }
}
static void final_cel_packet_end(void){final_cel_pending.ready=0;}
static void final_cel_packet_frame_begin(void)
{
    final_cel_packet_end();memset(final_cel_pages,0,sizeof final_cel_pages);
    for(unsigned i=0;i<512;i++) {
        const GePacket *p=ge_packets+i;
        if(p->bank && p->flags==GE_PACKET_FINAL_CEL && p->pages)final_cel_mark(p->page,p->pages,1);
    }
}
void chars_final_cel_packet_mode(int enabled)
{final_cel_enabled=!!enabled;final_cel_packet_end();}
void chars_final_cel_packet_profile(CharFinalCelPacketProfile *out)
{*out=final_cel_profile;out->resident_bytes=final_cel_resident_pages()*256u;memset(&final_cel_profile,0,sizeof final_cel_profile);}
static int final_cel_packet_pending(const Chars *c,const JgcBatch *b)
{return final_cel_pending.ready && final_cel_pending.bank==c && final_cel_pending.batch==b;}
static GePacket *final_cel_slot(const Chars *c,const JgcChar *ch,const JgcBatch *b)
{
    unsigned hash=final_packet_hash(c,b);GePacket *set=ge_packets+(hash&63u)*8u,*empty=NULL,*cpu=NULL;
    for(unsigned i=0;i<8;i++) {
        GePacket *p=set+i;
        if(p->bank==c && p->batch==b && p->flags==GE_PACKET_FINAL_CEL && p->nbones==ch->nbones)return p;
        if(!p->bank)empty=p;
        else if(p->bank==c && p->batch==b && (p->flags&GE_PACKET_FINAL) &&
                (!p->pages || !ge_packet_pinned[p->page]))cpu=p;
    }
    return cpu ? cpu:empty; /* promote a pre-cut result instead of duplicating it */
}
/* Called only after the current batch passed visibility and material setup.
   A miss arms capture of this draw's unchanged existing mixed-cel emitter. */
static int final_cel_packet_draw(Chars *c,const JgcChar *ch,const JgcBatch *b,float mats[][16],float alpha,int sorted)
{
    final_cel_packet_end();
    if(!final_cel_enabled || !ge_packets_active || !c->indexed_prepare || c->h->n_chars<=4 || ch!=c->chars+4 ||
       ch->nmotions || ch->nbones!=19 || memcmp(ch->name,"satellite_0",12) || c->vert_stride!=40 || c->lighting ||
       !source_lighting_enabled || ((b->flags>>12)&3)!=2 || (b->flags&(8|2048)) ||
       !b->n_vert || b->n_vert>CHAR_CPU_SKIN_VERTICES || !b->n_idx || alpha!=1.f || sorted ||
       material_planes || background_planes || recovery_active || event_actor_material || spray_hud_lighting ||
       scene_light[0] || scene_light[1] || hud_reciprocal_scale!=1.f || hud_depth_shift!=0.f)return 0;
    unsigned owners[FINAL_VERTEX_MAX_OWNERS];
    if(final_vertex_owners(c,ch,b,owners)!=1){final_cel_profile.rejected++;return 0;}
    GePacket *slot=final_cel_slot(c,ch,b);
    if(!slot){final_cel_profile.rejected++;return 0;}
    FinalVertexKey key=final_vertex_key(c,ch,b,1);
    const unsigned short *indices=chars_batch_indices(c,b);
    if(slot->bank==c && slot->batch==b && slot->flags==GE_PACKET_FINAL_CEL && slot->pages &&
       slot->source_nvert==b->n_vert && slot->source_nidx==b->n_idx &&
       final_vertex_matches(slot,&key,owners,mats)) {
        const FinalCelPacketKey *old=slot->vertices;
        if(old->source_indices==indices && old->source_count==b->n_idx) {
            memset(ge_packet_pinned+slot->page,1,slot->pages);final_cel_mark(slot->page,slot->pages,1);
            const void *vertices=(const unsigned char *)slot->vertices+final_cel_key_bytes();
            ge_submit_draw(GU_TRIANGLES,VFMT,slot->nidx,slot->indices,vertices);
            c->drawn_tris+=slot->nidx/3;
            final_cel_profile.hits++;final_cel_profile.triangles+=slot->nidx/3;
            final_cel_profile.cuts_reused+=slot->count;
            return 1;
        }
    }
    /* Stored keys were finite when admitted. A bit-exact warm matrix match
       proves that property too, so only new keys need a separate scan. */
    for(unsigned i=0;i<16;i++)if(!isfinite(mats[owners[0]][i])){final_cel_profile.rejected++;return 0;}
    final_cel_pending.bank=c;final_cel_pending.batch=b;final_cel_pending.model=ch;
    final_cel_pending.key.skin=key;final_cel_pending.key.owner.owner=owners[0];
    memcpy(final_cel_pending.key.owner.matrix,mats[owners[0]],64);
    final_cel_pending.key.source_indices=indices;final_cel_pending.key.source_count=b->n_idx;
    final_cel_pending.ready=1;final_cel_profile.misses++;return 0;
}
/* Capture only an admitted complete mixed packet. It is already exact and
   frame-owned; one cold copy buys direct GPU reuse on subsequent frames. */
static void final_cel_packet_capture(const Chars *c,const JgcBatch *b,const DrawVert *vertices,
    const unsigned short *indices,unsigned nvertices,unsigned nindices,unsigned cuts)
{
    if(!final_cel_packet_pending(c,b))return;
    final_cel_pending.ready=0;
    if(!cuts || nvertices>65535u || nindices>65535u || nindices%3)return;
    unsigned key_bytes=final_cel_key_bytes(),vertex_bytes=nvertices*sizeof(DrawVert),aligned_vertices=(vertex_bytes+15u)&~15u;
    unsigned index_bytes=nindices*2u,bytes=key_bytes+aligned_vertices+((index_bytes+15u)&~15u),needed=(bytes+255u)/256u;
    if(bytes>FINAL_CEL_PACKET_LIMIT){final_cel_profile.rejected++;return;}
    GePacket *slot=final_cel_slot(c,final_cel_pending.model,b);
    if(!slot){final_cel_profile.rejected++;return;}
    /* An unsubmitted previous version may be retired now; pinned versions
       retain their accounting bits even if the same metadata slot is reused. */
    if(slot->bank && slot->flags==GE_PACKET_FINAL_CEL && slot->pages && !ge_packet_pinned[slot->page]) {
        final_cel_mark(slot->page,slot->pages,0);slot->bank=NULL;
    }
    if(final_cel_resident_pages()+needed>FINAL_CEL_PACKET_LIMIT/256u){final_cel_profile.rejected++;return;}
    unsigned page=0,pages=0;unsigned char *memory=packet_memory(bytes,&page,&pages,slot);
    if(!memory){final_cel_profile.rejected++;return;}
    memcpy(memory,&final_cel_pending.key,sizeof final_cel_pending.key);
    memcpy(memory+key_bytes,vertices,vertex_bytes);
    unsigned short *ordered=(unsigned short *)(memory+key_bytes+aligned_vertices);
    memcpy(ordered,indices,index_bytes);
#ifdef __PSP__
    sceKernelDcacheWritebackRange(memory,(bytes+63u)&~63u);
#endif
    *slot=(GePacket){.bank=c,.batch=b,.vertices=memory,.indices=ordered,.nbones=final_cel_pending.model->nbones,
        .count=cuts,.flags=GE_PACKET_FINAL_CEL,.page=page,.pages=pages,.end=5,
        .nvert=nvertices,.nidx=nindices,.source_nvert=b->n_vert,.source_nidx=b->n_idx};
    final_cel_mark(page,pages,1);final_cel_profile.built++;final_cel_profile.bytes+=bytes;
}
#else
void chars_final_cel_packet_mode(int enabled){(void)enabled;}
void chars_final_cel_packet_profile(CharFinalCelPacketProfile *out){memset(out,0,sizeof *out);}
static void final_cel_packet_frame_begin(void){}
static void final_cel_packet_end(void){}
static int final_cel_packet_pending(const Chars *c,const JgcBatch *b){(void)c;(void)b;return 0;}
static int final_cel_packet_draw(Chars *c,const JgcChar *ch,const JgcBatch *b,float mats[][16],float alpha,int sorted)
{(void)c;(void)ch;(void)b;(void)mats;(void)alpha;(void)sorted;return 0;}
static void final_cel_packet_capture(const Chars *c,const JgcBatch *b,const DrawVert *v,const unsigned short *i,unsigned nv,unsigned ni,unsigned cuts)
{(void)c;(void)b;(void)v;(void)i;(void)nv;(void)ni;(void)cuts;}
#endif
