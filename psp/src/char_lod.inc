/* NPC-only visual detail. Original meshes, poses and textures remain resident.
   Small swizzled images are prepared during loading, never during a draw.
   Pages are immutable until their bank is freed after gfx_wait_idle(). */
#define LOD_TEXTURE_PAGES 256u
#define LOD_TEXTURE_SLOTS 128u
typedef struct {
    const Chars *bank;
    unsigned texture;
    unsigned short page,pages,w,h;
} LodTexture;
static unsigned char lod_pixels[LOD_TEXTURE_PAGES*256] __attribute__((aligned(64)));
static unsigned char lod_pages[LOD_TEXTURE_PAGES];
static LodTexture lod_textures[LOD_TEXTURE_SLOTS];
static CharLodProfile lod_profile;
static int lod_enabled=1,lod_level;
#ifdef __PSP__
typedef char lod_memory_budget[(sizeof lod_pixels+sizeof lod_pages+sizeof lod_textures+
    sizeof lod_profile+8<=CHAR_LOD_BYTES) ? 1:-1];
#endif
void chars_lod_mode(int enabled){lod_enabled=!!enabled;}
void chars_lod_totals(CharLodProfile *out){*out=lod_profile;}
void chars_lod_profile(CharLodProfile *out)
{
    *out=lod_profile;out->bytes=0;
    for(unsigned i=0;i<LOD_TEXTURE_PAGES;i++)out->bytes+=lod_pages[i]*256u;
    memset(&lod_profile,0,sizeof lod_profile);
}
static LodTexture *lod_texture_slot(const Chars *c,unsigned t)
{
    size_t hash=((size_t)c>>4)^t;
    LodTexture *set=lod_textures+(hash&31u)*4,*empty=NULL;
    for(unsigned i=0;i<4;i++) {
        if(set[i].bank==c && set[i].texture==t)return set+i;
        if(!set[i].bank)empty=set+i;
    }
    return empty; /* No eviction: earlier asynchronous draws keep their pixels. */
}
static void forget_lod(const Chars *c)
{
    for(unsigned i=0;i<LOD_TEXTURE_SLOTS;i++)if(lod_textures[i].bank==c) {
        LodTexture *t=lod_textures+i;
        memset(lod_pages+t->page,0,t->pages);memset(t,0,sizeof *t);
    }
}
static unsigned lod_swizzled(unsigned x,unsigned y,unsigned width)
{return ((y>>3)*(width>>4)+(x>>4))*128+(y&7)*16+(x&15);}
static void prepare_lod_texture(Chars *c,unsigned texture)
{
    const JgcTex *t=c->tex+texture;
    if(t->w<=32 || t->h<=16)return;
    LodTexture *slot=lod_texture_slot(c,texture);
    if(!slot || slot->bank)return;
    unsigned w=t->w/2,h=t->h/2;
    while(w>64 || h>64){w/=2;h/=2;}
    if(w<16 || h<8 || (w&15) || (h&7))return;
    unsigned pages=(w*h+255)/256,run=0,first=0;
    for(unsigned i=0;i<LOD_TEXTURE_PAGES;i++) {
        run=lod_pages[i] ? 0:run+1;
        if(run==pages){first=i+1-pages;break;}
    }
    if(run!=pages)return; /* Optional quality tier; original image is the fallback. */
    const unsigned char *palette=chars_texture_address(c,t,t->off_clut);
    const unsigned char *source=chars_texture_address(c,t,t->off_pix);
    unsigned char *dest=lod_pixels+first*256;
    unsigned sx=t->w/w,sy=t->h/h,n=sx*sy;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
        unsigned sum[4]={0};
        for(unsigned dy=0;dy<sy;dy++)for(unsigned dx=0;dx<sx;dx++) {
            unsigned index=source[lod_swizzled(x*sx+dx,y*sy+dy,t->w)];
            const unsigned char *p=palette+index*4;
            for(unsigned k=0;k<3;k++)sum[k]+=p[k]*p[3];
            sum[3]+=p[3];
        }
        /* Area filter in premultiplied alpha, then choose the closest entry
           from the unchanged palette. Transparent borders cannot darken RGB. */
        int colour[4];
        for(unsigned k=0;k<3;k++)colour[k]=sum[3] ? (sum[k]+sum[3]/2)/sum[3]:0;
        colour[3]=(sum[3]+n/2)/n;
        unsigned best=~0u,index=0;
        for(unsigned i=0;i<256;i++) {
            const unsigned char *p=palette+i*4;
            int a=(int)p[3]-colour[3];unsigned error=4*a*a;
            for(unsigned k=0;k<3;k++){int d=(int)p[k]-colour[k];error+=d*d;}
            if(error<best){best=error;index=i;if(!error)break;}
        }
        dest[lod_swizzled(x,y,w)]=(unsigned char)index;
    }
    memset(lod_pages+first,1,pages);
#ifdef __PSP__
    sceKernelDcacheWritebackRange(dest,pages*256);
#endif
    *slot=(LodTexture){c,texture,first,pages,w,h};
}
void chars_prepare_lod(Chars *c,int model)
{
    if(!c || !c->h || (unsigned)model>=c->h->n_chars)return;
    const JgcChar *ch=c->chars+model;
    for(unsigned bi=0;bi<ch->nbones;bi++) {
        const JgcBone *bone=c->bones+ch->bone0+bi;
        for(unsigned j=0;j<bone->nbatch;j++) {
            const JgcBatch *b=c->batch+bone->batch0+j;
            if(!(b->flags&4) && b->tex<c->h->n_tex)prepare_lod_texture(c,b->tex);
        }
    }
}
static void lod_bind_image(Chars *c,unsigned texture)
{
    const JgcTex *t=c->tex+texture;
    LodTexture *mip=lod_level ? lod_texture_slot(c,texture):NULL;
    if(mip && mip->bank==c) {
        sceGuTexImage(0,mip->w,mip->h,mip->w,lod_pixels+mip->page*256);
        lod_profile.texture_binds++;lod_profile.source_texels+=t->w*t->h;
        lod_profile.lod_texels+=mip->w*mip->h;
    } else {
        sceGuTexImage(0,t->w,t->h,t->w,chars_texture_address(c,t,t->off_pix));
        if(lod_level && t->w>32 && t->h>16)lod_profile.texture_fallbacks++;
    }
}
static int lod_at_point(float x,float y,float z,const float view[16],float focal)
{
    if(lod_enabled && view && isfinite(focal) && focal>0.f) {
        /* Lens-aware camera depth, not player distance. main's projection
           uses a 480-high source viewport; PSP renders 272 pixels. The body
           point is the actor's transformed local centre. Small NPCs switch
           textures below 3 px/unit and omit outline hulls below 2 px/unit. */
        float depth=-(view[8]*x+view[9]*y+view[10]*z+view[11]);
        if(isfinite(depth) && depth>0.f) {
            float scale=focal*((float)SCR_H/480.f)/depth;
            return scale<2.f ? 2:scale<3.f ? 1:0;
        }
    }
    return 0;
}
int chars_npc_detail_begin(const float root[16],const float view[16],float focal)
{
    int previous=lod_level;
    lod_level=root ? lod_at_point(root[3]+10.f*root[1],root[7]+10.f*root[5],root[11]+10.f*root[9],view,focal):0;
    return previous;
}
void chars_npc_detail_end(int previous){lod_level=previous;}
void chars_draw_npc(Chars *c,int ci,float x,float y,float z,float yaw,int motion,float frame,
    float opacity,const float view[16],float focal)
{
    int previous=lod_level;lod_level=lod_at_point(x,y+10.f,z,view,focal);
    chars_draw_one_faded(c,ci,x,y,z,yaw,motion,frame,opacity);
    lod_level=previous;
}
void chars_draw_pedestrian(Chars *c,int ci,float x,float y,float z,float yaw,int motion,float frame,
    float opacity,const float view[16],float focal)
{
    int previous=ped_packet_scope;ped_packet_scope=1;
    chars_draw_npc(c,ci,x,y,z,yaw,motion,frame,opacity,view,focal);
    ped_packet_scope=previous;
}
