/* Immutable batch membership: one packed allocation, optional and bounded.
   Owner IDs above the supported hierarchy limit have always mapped to 0. */
#define CHAR_OWNER_BUDGET (96u*1024u)
static unsigned owner_storage_bytes;
static unsigned batch_owner_set(const Chars *c,const JgcBatch *b,unsigned char seen[CHAR_RENDER_MAX_BONES])
{
    memset(seen,0,CHAR_RENDER_MAX_BONES);unsigned count=0;
    const unsigned char *src=chars_batch_vertices(c,b);
    for(unsigned k=0;k<b->n_vert;k++) {
        unsigned short owner=0;
        if(c->vert_stride>=28)memcpy(&owner,src+k*c->vert_stride+24,2);
        if(owner>=CHAR_RENDER_MAX_BONES)owner=0;
        if(!seen[owner]){seen[owner]=1;count++;}
    }
    return count;
}
static void prepare_batch_owners(Chars *c)
{
    if(!c->h->n_batches || c->h->n_batches>8192)return;
    unsigned char seen[CHAR_RENDER_MAX_BONES];unsigned count=0;
    for(unsigned i=0;i<c->h->n_batches;i++) {
        const JgcBatch *b=c->batch+i;
        if(b->vert_off>c->h->n_verts || b->n_vert>c->h->n_verts-b->vert_off)return;
        count+=batch_owner_set(c,b,seen);
        if(count>32768)return;
    }
    unsigned bytes=(c->h->n_batches+1)*sizeof(unsigned)+count*sizeof(unsigned short);
    if(bytes>CHAR_OWNER_BUDGET-owner_storage_bytes)return;
    unsigned *offsets=malloc(bytes);if(!offsets)return;
    unsigned short *owners=(unsigned short *)(offsets+c->h->n_batches+1);count=0;
    for(unsigned i=0;i<c->h->n_batches;i++) {
        offsets[i]=count;batch_owner_set(c,c->batch+i,seen);
        for(unsigned j=0;j<CHAR_RENDER_MAX_BONES;j++)if(seen[j])owners[count++]=(unsigned short)j;
    }
    offsets[c->h->n_batches]=count;c->batch_owner_offsets=offsets;
    c->batch_owner_bytes=bytes;owner_storage_bytes+=bytes;
}
static void batch_owner_codes(const Chars *c,const JgcChar *ch,const JgcBatch *b,
    const unsigned char *cross,const unsigned char *outside,unsigned *any,unsigned *all)
{
    *any=0;*all=63;
    if(preparation_enabled && c->batch_owner_offsets) {
        unsigned i=(unsigned)(b-c->batch),first=c->batch_owner_offsets[i],end=c->batch_owner_offsets[i+1];
        const unsigned short *owners=(const unsigned short *)(c->batch_owner_offsets+c->h->n_batches+1);
        for(unsigned k=first;k<end;k++) {
            unsigned owner=owners[k]<ch->nbones ? owners[k]:0;
            if(cross)*any|=cross[owner];
            *all&=outside[owner];
            preparation_profile.owner_reads++;
        }
    } else {
        const unsigned char *src=chars_batch_vertices(c,b);
        for(unsigned k=0;k<b->n_vert;k++) {
            unsigned short owner=0;
            if(c->vert_stride>=28)memcpy(&owner,src+k*c->vert_stride+24,2);
            if(owner>=ch->nbones)owner=0;
            if(cross)*any|=cross[owner];
            *all&=outside[owner];
            preparation_profile.vertex_reads++;
        }
    }
}
