/* JCR1: seek only the current mission's immutable models. The assembled
   JGC2 retains global model IDs, exact animation keys and full textures.
   This runs at level entry, never from a draw or an enemy-spawn callback. */
#ifndef JGR_ROSTER_ALLOC
#define JGR_ROSTER_ALLOC memalign
#endif
typedef struct {unsigned offset,bytes,map,count;} CharRosterEntry;
static int roster_read(SceUID fd,unsigned at,void *dst,unsigned bytes)
{
    if(sceIoLseek(fd,at,PSP_SEEK_SET)<0)return -3;
    return read_char_part(fd,dst,bytes);
}
static unsigned roster_align(unsigned n){return (n+63u)&~63u;}
static int roster_range(unsigned at,unsigned bytes,unsigned size)
{return at<=size && bytes<=size-at;}
int chars_load_roster(Chars *c,const char *path,unsigned long long mask)
{
    forget_packets(c);memset(c,0,sizeof *c);
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0777);if(fd<0)return -1;
    unsigned top[8];CharRosterEntry entries[51];JgcHeader models[51],h={0};
    SceOff length=sceIoLseek(fd,0,PSP_SEEK_END);int result=-4;
    if(length<32 || length>0x04000000 || roster_read(fd,0,top,sizeof top) ||
       memcmp(top,"JCR1",4) || top[1]!=51 || top[2]!=(unsigned)length || top[4]<51 || top[4]>102 ||
       !top[3] || top[3]>100000 || top[6]!=1 || top[7] || !mask || mask>>51 ||
       roster_read(fd,32,entries,sizeof entries))goto failed;
    unsigned channel_bytes=0,key_bytes=0,pixel_bytes=0,far_count=0;
    for(unsigned ci=0;ci<51;ci++)if(mask&(1ULL<<ci)) {
        CharRosterEntry *e=entries+ci;JgcHeader *m=models+ci;
        if(e->bytes<sizeof *m || !roster_range(e->offset,e->bytes,length) || e->count>4096 ||
           !roster_range(e->map,e->count*4,length) || roster_read(fd,e->offset,m,sizeof *m) ||
           memcmp(m->magic,"JGC2",4) || !char_layout_valid(m,e->bytes) || (m->n_chars!=1 && m->n_chars!=2) ||
           !m->n_bones || m->n_bones>64*m->n_chars || m->n_batches!=e->count || m->n_motions>256 ||
           m->n_verts>100000 || m->n_idx>1000000 || m->n_tex>64)goto failed;
        h.n_bones+=m->n_bones;h.n_batches+=m->n_batches;h.n_verts+=m->n_verts;
        h.n_idx+=m->n_idx;h.n_tex+=m->n_tex;h.n_motions+=m->n_motions;
        channel_bytes+=m->off_keys-m->off_motions-m->n_motions*8;
        far_count+=m->n_chars==2;
        key_bytes+=m->off_pix-m->off_keys;pixel_bytes+=roster_align(e->bytes-m->off_pix);
        if(h.n_bones>32767 || h.n_batches>4096 || h.n_verts>100000 || h.n_idx>1000000 ||
           h.n_tex>4096 || channel_bytes>0x400000 || key_bytes>0x1000000 || pixel_bytes>0x1000000)goto failed;
    }
    memcpy(h.magic,"JGC2",4);h.n_chars=51+far_count;h.off_chars=far_count ? roster_align(80+51*4):80;
    h.off_bones=roster_align(h.off_chars+h.n_chars*sizeof(JgcChar));
    h.off_batches=roster_align(h.off_bones+h.n_bones*sizeof(JgcBone));
    h.off_verts=roster_align(h.off_batches+h.n_batches*sizeof(JgcBatch));
    h.off_idx=roster_align(h.off_verts+h.n_verts*28);
    h.off_tex=roster_align(h.off_idx+h.n_idx*2);
    h.off_motions=roster_align(h.off_tex+h.n_tex*sizeof(JgcTex));
    h.off_keys=roster_align(h.off_motions+h.n_motions*8+channel_bytes);
    h.off_pix=roster_align(h.off_keys+key_bytes);
    unsigned size=h.off_pix+pixel_bytes;
    unsigned offsets[]={0,h.off_verts,h.off_idx,h.off_motions,h.off_pix,size};
    unsigned char *parts[5]={0};unsigned allocated=0;result=-2;
    for(unsigned pass=0;pass<5;pass++) {
        unsigned best=5;
        for(unsigned j=0;j<5;j++)if(!(allocated&(1u<<j)) &&
           (best==5 || offsets[j+1]-offsets[j]>offsets[best+1]-offsets[best]))best=j;
        allocated|=1u<<best;unsigned bytes=offsets[best+1]-offsets[best];
        if(!(parts[best]=JGR_ROSTER_ALLOC(64,roster_align(bytes ? bytes:64)))) {
            for(unsigned j=0;j<5;j++)free(parts[j]);
            goto failed;
        }
        memset(parts[best],0,bytes);
    }
    c->base=parts[0];c->vertex_data=parts[1];c->index_data=parts[2];c->motion_data=parts[3];c->texture_data=parts[4];
    memcpy(c->base,&h,sizeof h);c->h=(JgcHeader *)c->base;c->vert_stride=28;
    if(far_count){unsigned extension[3]={0,51,80};memcpy(extension,"JCL1",4);memcpy(c->base+68,extension,sizeof extension);}
    c->chars=(JgcChar *)chars_data(c,h.off_chars);c->bones=(JgcBone *)chars_data(c,h.off_bones);
    c->batch=(JgcBatch *)chars_data(c,h.off_batches);c->verts=chars_data(c,h.off_verts);
    c->idx=(unsigned short *)chars_data(c,h.off_idx);c->tex=(JgcTex *)chars_data(c,h.off_tex);
    c->motions=(JgcMotion *)chars_data(c,h.off_motions);
    c->lighting_source=JGR_ROSTER_ALLOC(64,roster_align(h.n_batches*4));if(!c->lighting_source)goto failed;
    c->source_vertices=top[3];c->source_geometry_hash=top[5];c->source_models=top[4];
    unsigned bone=0,batch=0,vert=0,index=0,tex=0,motion=0,far_model=51;
    unsigned channels=h.off_motions+h.n_motions*8,keys=h.off_keys,pixels=h.off_pix;
    result=-4;
    for(unsigned ci=0;ci<51;ci++)if(mask&(1ULL<<ci)) {
        const JgcHeader *m=models+ci;const CharRosterEntry *e=entries+ci;JgcChar *ch=c->chars+ci;
        unsigned source_channels=m->off_motions+m->n_motions*8;
        unsigned cb=m->off_keys-source_channels,kb=m->off_pix-m->off_keys;
        if(roster_read(fd,e->offset+m->off_chars,ch,sizeof *ch) || ch->bone0 || ch->motion0 ||
           !ch->nbones || ch->nbones>64 || ch->nbones*m->n_chars!=m->n_bones || ch->nmotions!=m->n_motions ||
           roster_read(fd,e->offset+m->off_bones,c->bones+bone,m->n_bones*sizeof(JgcBone)) ||
           roster_read(fd,e->offset+m->off_batches,c->batch+batch,m->n_batches*sizeof(JgcBatch)) ||
           roster_read(fd,e->offset+m->off_verts,(unsigned char *)c->verts+vert*28,m->n_verts*28) ||
           roster_read(fd,e->offset+m->off_idx,c->idx+index,m->n_idx*2) ||
           roster_read(fd,e->offset+m->off_tex,c->tex+tex,m->n_tex*sizeof(JgcTex)) ||
           roster_read(fd,e->offset+m->off_motions,c->motions+motion,m->n_motions*8) ||
           roster_read(fd,e->offset+source_channels,chars_data(c,channels),cb) ||
           roster_read(fd,e->offset+m->off_keys,chars_data(c,keys),kb) ||
           roster_read(fd,e->offset+m->off_pix,chars_data(c,pixels),e->bytes-m->off_pix) ||
           roster_read(fd,e->map,c->lighting_source+batch,m->n_batches*4))goto failed;
        if(m->n_chars==2) {
            unsigned extension[4];JgcChar *low=c->chars+far_model;
            if(m->off_chars<84 || roster_read(fd,e->offset+68,extension,sizeof extension) ||
               memcmp(extension,"JCL1",4) || extension[1]!=1 || extension[2]!=80 || extension[3]!=1 ||
               roster_read(fd,e->offset+m->off_chars+sizeof *ch,low,sizeof *low) ||
               low->bone0!=ch->nbones || low->nbones!=ch->nbones || low->motion0 || low->nmotions!=ch->nmotions)goto failed;
            low->bone0+=bone;low->motion0=motion;((unsigned *)(c->base+80))[ci]=far_model++;
        }
        ch->bone0=bone;ch->motion0=motion;
        for(unsigned j=0;j<m->n_bones;j++) {
            JgcBone *b=c->bones+bone+j;
            if(b->parent>=(int)j || b->parent<-1 || b->batch0>m->n_batches || b->nbatch>m->n_batches-b->batch0)goto failed;
            if(b->parent>=0)b->parent+=bone;
            b->batch0+=batch;
        }
        for(unsigned j=0;j<m->n_batches;j++) {
            JgcBatch *b=c->batch+batch+j;
            if(b->vert_off>m->n_verts || b->n_vert>m->n_verts-b->vert_off || b->idx_off>m->n_idx ||
               b->n_idx>m->n_idx-b->idx_off || b->tex>=m->n_tex ||
               c->lighting_source[batch+j]>top[3] || b->n_vert>top[3]-c->lighting_source[batch+j])goto failed;
            for(unsigned k=0;k<b->n_idx;k++)if(c->idx[index+b->idx_off+k]>=b->n_vert)goto failed;
            b->vert_off+=vert;b->idx_off+=index;b->tex+=tex;
        }
        for(unsigned j=0;j<m->n_tex;j++) {
            JgcTex *t=c->tex+tex+j;
            if(!t->w || !t->h || t->w>512 || t->h>512 || t->off_clut<m->off_pix || t->off_pix<m->off_pix ||
               !roster_range(t->off_clut,1024,e->bytes) || !roster_range(t->off_pix,t->w*t->h,e->bytes))goto failed;
            t->off_clut+=pixels-m->off_pix;t->off_pix+=pixels-m->off_pix;
        }
        for(unsigned j=0;j<m->n_motions;j++) {
            JgcMotion *mo=c->motions+motion+j;
            if(mo->chan0<source_channels || mo->chan0>m->off_keys || ch->nbones*24>m->off_keys-mo->chan0 ||
               (mo->chan0-source_channels)%24)goto failed;
            mo->chan0+=channels-source_channels;
        }
        /* Channel tables can be shared by several motions. Relocate each
           stored row once, not once per reference to that table. */
        for(unsigned at=0;at+24<=cb;at+=24) {
            unsigned *cn=(unsigned *)chars_data(c,channels+at);
            for(unsigned kind=0;kind<3;kind++) {
                unsigned stride=kind==1 ? 6:12,n=cn[kind*2],off=cn[kind*2+1];
                if(off>kb || n>(kb-off)/stride || off%(kind==1 ? 2:4))goto failed;
                cn[kind*2+1]+=keys-h.off_keys;
            }
        }
        bone+=m->n_bones;batch+=m->n_batches;vert+=m->n_verts;index+=m->n_idx;tex+=m->n_tex;motion+=m->n_motions;
        channels+=cb;keys+=kb;pixels+=roster_align(e->bytes-m->off_pix);asset_progress();
    }
    if(!chars_validate_source_lod(c))goto failed;
    sceIoClose(fd);c->resident_mask=mask;c->resident_bytes=size+h.n_batches*4;
    prepare_render_bounds(c);prepare_motion_bounds(c,size);prepare_batch_owners(c);sceKernelDcacheWritebackAll();return 0;
failed:
    sceIoClose(fd);chars_free(c);return result;
}
