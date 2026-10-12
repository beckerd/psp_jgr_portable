/* Last-resort immutable JGC storage for fragmented movie handoffs. Each
   batch stays contiguous; neighbouring batches share 128 KiB pages. */
struct CharGeometryPage {struct CharGeometryPage *next;};
static void release_char_pages(Chars *c)
{
    while(c->geometry_pages){CharGeometryPage *p=c->geometry_pages;c->geometry_pages=p->next;free(p);}
    free(c->geometry);free(c->texture_metadata);
    if(c->texture_parts){for(unsigned i=0;c->h && i<c->h->n_tex;i++)free(c->texture_parts[i]);free(c->texture_parts);}
}
static int char_read_at(SceUID fd,unsigned char *dest,unsigned offset,unsigned bytes)
{return sceIoLseek(fd,offset,PSP_SEEK_SET)<0 ? -3:read_char_part(fd,dest,bytes);}
static int read_char_pages(Chars *c,SceUID fd,const JgcHeader *h,unsigned size)
{
    /* JGC2 skaters use PLT1 lighting bound to the complete vertex bank.
       Preserve their existing contiguous/section path and failure behavior;
       the movie casts needing this fallback carry JGC3 local normals. */
    if(!memcmp(h->magic,"JGC2",4))return -2;
    c->base=memalign(64,(h->off_verts+63)&~63u);if(!c->base)return -2;
    if(char_read_at(fd,c->base,0,h->off_verts))return -3;
    c->h=(JgcHeader *)c->base;c->batch=(JgcBatch *)(c->base+h->off_batches);
    unsigned stride=!memcmp(h->magic,"JGC3",4) ? 40:!memcmp(h->magic,"JGC2",4) ? 28:24;
    for(unsigned i=0;i<h->n_batches;i++) {
        const JgcBatch *b=c->batch+i;
        if(b->vert_off>h->n_verts || b->n_vert>h->n_verts-b->vert_off ||
           b->idx_off>h->n_idx || b->n_idx>h->n_idx-b->idx_off)return -4;
    }
    c->geometry=calloc(h->n_batches ? h->n_batches:1,sizeof *c->geometry);if(!c->geometry)return -2;
    for(unsigned kind=0;kind<2;kind++) {
        unsigned width=kind ? 2:stride,offset=kind ? h->off_idx:h->off_verts;
        for(unsigned first=0;first<h->n_batches;) {
            const JgcBatch *b=c->batch+first;
            unsigned start=kind ? b->idx_off:b->vert_off,end=start+(kind ? b->n_idx:b->n_vert),last=first+1;
            while(last<h->n_batches) {
                b=c->batch+last;unsigned at=kind ? b->idx_off:b->vert_off,stop=at+(kind ? b->n_idx:b->n_vert);
                if(at<start || stop<end || (stop-start)*width>128u*1024u)break;
                end=stop;last++;
            }
            unsigned bytes=(end-start)*width;
            CharGeometryPage *page=memalign(64,64+((bytes+63)&~63u));if(!page)return -2;
            page->next=c->geometry_pages;c->geometry_pages=page;unsigned char *data=(unsigned char *)page+64;
            if(char_read_at(fd,data,offset+start*width,bytes))return -3;
            for(unsigned i=first;i<last;i++) {
                b=c->batch+i;
                if(kind)c->geometry[i].indices=(unsigned short *)data+b->idx_off-start;
                else c->geometry[i].vertices=data+(b->vert_off-start)*width;
            }
            first=last;
        }
    }
    unsigned bytes=h->off_motions-h->off_tex;
    c->texture_metadata=memalign(64,(bytes+63)&~63u);if(bytes && !c->texture_metadata)return -2;
    if(char_read_at(fd,c->texture_metadata,h->off_tex,bytes))return -3;
    bytes=h->off_pix-h->off_motions;
    if(bytes){c->motion_data=memalign(64,(bytes+63)&~63u);if(!c->motion_data)return -2;
        if(char_read_at(fd,c->motion_data,h->off_motions,bytes))return -3;}
    c->tex=(JgcTex *)c->texture_metadata;
    c->texture_parts=calloc(h->n_tex ? h->n_tex:1,sizeof *c->texture_parts);if(!c->texture_parts)return -2;
    for(unsigned i=0;i<h->n_tex;i++) {
        const JgcTex *t=c->tex+i;unsigned pixels=(unsigned)t->w*t->h;
        if(t->off_clut<h->off_pix || t->off_clut>size || size-t->off_clut<1024 ||
           t->off_pix<t->off_clut+1024 || t->off_pix>size || pixels>size-t->off_pix || ((t->off_pix-t->off_clut)&15))return -4;
        bytes=t->off_pix-t->off_clut+pixels;
        c->texture_parts[i]=memalign(64,(bytes+63)&~63u);if(!c->texture_parts[i])return -2;
        if(char_read_at(fd,c->texture_parts[i],t->off_clut,bytes))return -3;
    }
    return 0;
}
