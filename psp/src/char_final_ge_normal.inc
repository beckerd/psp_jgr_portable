/* Experimental Final-only normal lighting. GE normalizes transformed
   normals, unlike the source kernel, so authored and current scale guards
   are mandatory. Positions/topology/material alpha stay unchanged; packed
   RGB uses the GE's documented fixed-point lighting arithmetic. */
#if JGR_FINAL_GE_NORMAL
static int final_ge_normal_enabled=1,final_ge_normal_ready;
static CharFinalGeNormalProfile final_ge_normal_profile;
void chars_final_ge_normal_mode(int enabled)
{final_ge_normal_enabled=!!enabled;final_ge_normal_ready=0;}
int chars_final_ge_normal_enabled(void){return final_ge_normal_enabled;}
void chars_final_ge_normal_profile(CharFinalGeNormalProfile *out)
{*out=final_ge_normal_profile;memset(&final_ge_normal_profile,0,sizeof final_ge_normal_profile);}
static void chars_final_ge_normal_frame_begin(void){final_ge_normal_ready=0;}
static int final_ge_normal_source(const Chars *c,const JgcBatch *b)
{
    if(!b->n_vert || ((b->flags>>12)&3)!=1 || (b->flags&(8|2048)))return 0;
    const unsigned char *data=chars_batch_vertices(c,b);
    for(unsigned i=0;i<b->n_vert;i++) {
        const SkinVert *v=(const SkinVert *)(data+i*40u);
        const float *n=(const float *)(data+i*40u+28u);
        if(!(v->pad&1) || !isfinite(v->u) || !isfinite(v->v) ||
           !isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z) ||
           !isfinite(n[0]) || !isfinite(n[1]) || !isfinite(n[2]))return 0;
        float length=(n[0]*n[0]+n[1]*n[1])+n[2]*n[2];
        if(!isfinite(length) || fabsf(length-1.f)>2e-5f)return 0;
    }
    return 1;
}
static int final_ge_normal_authored_owner(const Chars *c,const JgcChar *ch,unsigned local)
{
    if(ch->motion0>c->h->n_motions || ch->nmotions>c->h->n_motions-ch->motion0)return 0;
    for(unsigned depth=0;depth<ch->nbones;depth++) {
        if(local>=ch->nbones)return 0;
        const JgcBone *b=c->bones+ch->bone0+local;
        if(!(b->ev&4))for(unsigned k=0;k<3;k++)if(b->scl[k]!=1.f)return 0;
        for(unsigned mi=ch->motion0;mi<ch->motion0+ch->nmotions;mi++) {
            const JgcChan *cn=(const JgcChan *)chars_data(c,c->motions[mi].chan0)+local;
            const float *keys=(const float *)(chars_data(c,c->h->off_keys)+cn->off_scl);
            for(unsigned k=0;k<cn->nscl*3u;k++)if(keys[k]!=1.f)return 0;
        }
        if(b->parent<0)return 1;
        if((unsigned)b->parent<ch->bone0)return 0;
        local=(unsigned)b->parent-ch->bone0;
    }
    return 0;
}
static void final_ge_normal_prepare(const Chars *c,FinalIndexedPlan *p)
{
    if(c->motion_owner || !c->batch_owner_offsets)return;
    unsigned *bits=(unsigned *)((unsigned char *)p+p->normal_offset);
    for(unsigned model=0;model<c->h->n_chars && model<65534u;model++) {
        const JgcChar *ch=c->chars+model;
        if(!ch->nbones || ch->nbones>CHAR_RENDER_MAX_BONES || ch->bone0>c->h->n_bones ||
           ch->nbones>c->h->n_bones-ch->bone0)continue;
        unsigned char safe[CHAR_RENDER_MAX_BONES];
        for(unsigned bone=0;bone<ch->nbones;bone++)safe[bone]=final_ge_normal_authored_owner(c,ch,bone);
        for(unsigned bone=0;bone<ch->nbones;bone++) {
            const JgcBone *owner=c->bones+ch->bone0+bone;
            if(owner->batch0>p->nbatches || owner->nbatch>p->nbatches-owner->batch0)continue;
            for(unsigned bi=owner->batch0;bi<owner->batch0+owner->nbatch;bi++) {
                FinalIndexedBatch *meta=p->batch+bi;
                if(meta->reserved && meta->reserved!=model+1u) {
                    meta->reserved=65535u;bits[bi/32]&=~(1u<<(bi%32));continue;
                }
                meta->reserved=model+1u;
                const JgcBatch *b=c->batch+bi;
                if(!final_ge_normal_source(c,b))continue;
                unsigned first=c->batch_owner_offsets[bi],end=c->batch_owner_offsets[bi+1];
                const unsigned short *ids=(const unsigned short *)(c->batch_owner_offsets+c->h->n_batches+1);
                if(first==end || end-first>8u)continue;
                unsigned at=first;
                for(;at<end;at++)if(!safe[ids[at]<ch->nbones ? ids[at]:0])break;
                if(at==end)bits[bi/32]|=1u<<(bi%32);
            }
        }
    }
}
#if CHAR_GE_SKINNING
typedef struct {float u,v;unsigned color;float nx,ny,nz,x,y,z;} FinalGeNormalVertex;
static int final_ge_normal_rigid(const float m[16])
{
    for(unsigned i=0;i<16;i++)if(!isfinite(m[i]))return 0;
    if(m[12]!=0.f || m[13]!=0.f || m[14]!=0.f || m[15]!=1.f)return 0;
    for(unsigned a=0;a<3;a++)for(unsigned b=a;b<3;b++) {
        float dot=(m[a]*m[b]+m[4+a]*m[4+b])+m[8+a]*m[8+b];
        if(!isfinite(dot) || fabsf(dot-(a==b ? 1.f:0.f))>2e-4f)return 0;
    }
    float det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
    return det>0.f;
}
static void final_ge_normal_light(void)
{
    if(!final_ge_normal_ready) {
        const ScePspFVector3 direction={-.09f,-.84f,.01f};
        sceGuDisable(GU_LIGHT1);sceGuDisable(GU_LIGHT2);sceGuDisable(GU_LIGHT3);
        sceGuLightMode(GU_SINGLE_COLOR);
        sceGuModelColor(0,0xffffffffu,0xffffffffu,0);
        sceGuColorMaterial(GU_AMBIENT|GU_DIFFUSE);
        sceGuAmbient(0xffba9b77u);
        sceGuLight(0,GU_DIRECTIONAL,GU_AMBIENT_AND_DIFFUSE,&direction);
        sceGuLightColor(0,GU_AMBIENT,0);
        sceGuLightColor(0,GU_DIFFUSE,0x00f2c99bu);
        sceGuLightColor(0,GU_SPECULAR,0);
        final_ge_normal_ready=1;
    }
    sceGuEnable(GU_LIGHT0);sceGuEnable(GU_LIGHTING);
}
static int final_ge_normal_draw(Chars *c,const JgcChar *ch,const JgcBatch *b,float mats[][16],GePalette *palette)
{
    const FinalIndexedPlan *plan=c->indexed_prepare;
    if(!final_ge_normal_enabled || !plan || plan->header!=c->h || c->vert_stride!=40 || c->lighting ||
       !source_lighting_enabled || recovery_active || event_actor_material || spray_hud_lighting ||
       scene_light[0] || scene_light[1] || fade_active || ped_packet_scope || background_planes || material_planes ||
       hud_reciprocal_scale!=1.f || hud_depth_shift!=0.f || c->motion_owner || !c->batch_owner_offsets ||
       !ch->nbones || ch->nbones>CHAR_RENDER_MAX_BONES)return 0;
    unsigned bi=(unsigned)(b-c->batch);
    if(bi>=plan->nbatches || plan->batch[bi].nvert!=b->n_vert || plan->batch[bi].flags!=b->flags ||
       plan->batch[bi].reserved!=(unsigned)(ch-c->chars)+1u)return 0;
    const unsigned *bits=(const unsigned *)((const unsigned char *)plan+plan->normal_offset);
    if(!(bits[bi/32]&(1u<<(bi%32))))return 0;
    unsigned first=c->batch_owner_offsets[bi],end=c->batch_owner_offsets[bi+1],count=0,bones[8];
    const unsigned short *ids=(const unsigned short *)(c->batch_owner_offsets+c->h->n_batches+1);
    if(first==end || end-first>8u)return 0;
    for(unsigned i=first;i<end;i++) {
        unsigned owner=ids[i]<ch->nbones ? ids[i]:0,j;
        for(j=0;j<count && bones[j]!=owner;j++);
        if(j<count)continue;
        if(!final_ge_normal_rigid(mats[owner])){final_ge_normal_profile.rigid_rejected++;return 0;}
        bones[count++]=owner;
    }
    unsigned started=render_clock();
    unsigned weight_bytes=(count+3u)&~3u,stride=weight_bytes+sizeof(FinalGeNormalVertex);
    unsigned bytes=b->n_vert*stride,page=0,pages=0;unsigned char *vertices=NULL;
#if defined(__PSP__) || defined(JGR_TEST_GE_NORMAL_LIST)
    if(gfx_list_available()<65536u+512u){final_ge_normal_profile.fallbacks++;packet_us+=render_clock()-started;return 0;}
#endif
    GePacket *cached=packet_slot(c,b,ch->nbones,0,0);
    if(cached && cached->bank==c && cached->batch==b && cached->flags==GE_PACKET_FINAL_NORMAL &&
       cached->nbones==ch->nbones && !cached->end && cached->count==count && cached->nvert==b->n_vert &&
       cached->nidx==b->n_idx) {
        unsigned same=1;for(unsigned i=0;i<count;i++)same&=cached->bones[i]==bones[i];
        if(same) {
            vertices=cached->vertices;
            if(cached->pages)memset(ge_packet_pinned+cached->page,1,cached->pages);
            packet_reused++;final_ge_normal_profile.reused++;
        }
    }
    if(!vertices) {
        vertices=cached ? packet_memory(bytes,&page,&pages,cached):NULL;
        if(!vertices) {
#if defined(__PSP__) || defined(JGR_TEST_GE_NORMAL_LIST)
            if(gfx_list_available()<bytes+65536u+512u){final_ge_normal_profile.fallbacks++;packet_us+=render_clock()-started;return 0;}
#endif
            vertices=sceGuGetMemory(bytes);
            if(!vertices){final_ge_normal_profile.fallbacks++;packet_us+=render_clock()-started;return 0;}
        }
        const unsigned char *data=chars_batch_vertices(c,b);
        for(unsigned i=0;i<b->n_vert;i++) {
            const SkinVert *v=(const SkinVert *)(data+40u*i);
            unsigned owner=v->bone<ch->nbones ? v->bone:0,slot=0;
            while(slot<count && bones[slot]!=owner)slot++;
            unsigned char *dst=vertices+i*stride;
            memset(dst,0,weight_bytes);dst[slot]=128;
            FinalGeNormalVertex *out=(FinalGeNormalVertex *)(dst+weight_bytes);
            out->u=v->u;out->v=v->v;out->color=v->color;
            memcpy(&out->nx,data+40u*i+28u,12);out->x=v->x;out->y=v->y;out->z=v->z;
        }
#ifdef __PSP__
        if(pages)sceKernelDcacheWritebackRange(vertices,(bytes+63u)&~63u);
#endif
        if(cached) {
            cached->bank=c;cached->batch=b;cached->nbones=ch->nbones;cached->flags=GE_PACKET_FINAL_NORMAL;
            cached->count=count;cached->vertices=vertices;cached->page=page;cached->pages=pages;cached->end=0;
            cached->nvert=cached->source_nvert=b->n_vert;cached->nidx=cached->source_nidx=b->n_idx;
            cached->indices=chars_batch_indices(c,b);
            for(unsigned i=0;i<count;i++)cached->bones[i]=bones[i];
        }
        packet_prepared++;packet_vertices+=b->n_vert;final_ge_normal_profile.built++;
        final_ge_normal_profile.packet_bytes+=bytes;
    }
    packet_us+=render_clock()-started;
    if(palette->transformed){palette->transformed=0;palette->valid=0;}
    for(unsigned i=0;i<count;i++) {
        if((palette->valid&(1u<<i)) && palette->bone[i]==bones[i]){bone_reused++;continue;}
        palette->valid|=1u<<i;palette->bone[i]=bones[i];bone_uploads++;ge_submit_bone(i,mats[bones[i]]);
    }
    final_ge_normal_light();
    ge_submit_draw(GU_TRIANGLES,VFMT|GU_NORMAL_32BITF|GU_WEIGHT_8BIT|GU_WEIGHTS(count),b->n_idx,chars_batch_indices(c,b),vertices);
    sceGuDisable(GU_LIGHTING);sceGuDisable(GU_LIGHT0);
    c->drawn_tris+=b->n_idx/3;ge_skin_batches++;ge_skin_vertices+=b->n_vert;
    weighted_vertices+=b->n_vert*count;if(count>max_weights)max_weights=count;
    final_ge_normal_profile.batches++;final_ge_normal_profile.vertices+=b->n_vert;return 1;
}
#endif
#else
void chars_final_ge_normal_mode(int enabled){(void)enabled;}
int chars_final_ge_normal_enabled(void){return 0;}
void chars_final_ge_normal_profile(CharFinalGeNormalProfile *out){memset(out,0,sizeof *out);}
static void chars_final_ge_normal_frame_begin(void){}
#endif
