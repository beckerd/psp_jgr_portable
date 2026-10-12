/* Optional authored world mips. Level zero/palettes remain byte-identical to
   JGS; lower levels borrow a bounded tail of the existing texture VRAM. */
#define STAGE_MIP_BYTES 32768u
#define STAGE_MIP_SLOTS 16u
typedef struct {
    unsigned short texture,w,h,levels;
    unsigned base_key,offset[2],bytes[2],reserved;
} StageMipRecord;
typedef struct {
    SceUID fd;
    unsigned count,bytes,pixel_offset;
    unsigned char *memory;
    StageMipRecord record[STAGE_MIP_SLOTS];
} StageMipPlan;
static struct {
    const Stage *owner;
    unsigned count,bytes;
    unsigned char slot[STAGE_TEXTURE_CACHE_SLOTS];
    const unsigned char *pixels[STAGE_MIP_SLOTS][2];
} stage_mips;
static int stage_mipmaps_enabled=1;
void stage_set_mipmaps(int enabled){stage_mipmaps_enabled=!!enabled;}
void stage_mip_profile(unsigned out[2]){out[0]=stage_mips.count;out[1]=stage_mips.bytes;}
static int read_stage_part(SceUID fd,unsigned char *p,unsigned bytes);
static unsigned stage_mip_hash(const unsigned char *data,unsigned bytes)
{unsigned key=2166136261u;for(unsigned i=0;i<bytes;i++)key=(key^data[i])*16777619u;return key;}
static void stage_mip_forget(const Stage *s)
{if(stage_mips.owner==s)memset(&stage_mips,0,sizeof stage_mips);}
static unsigned stage_mip_slot(const Stage *s,unsigned texture)
{return stage_mipmaps_enabled && stage_mips.owner==s && texture<STAGE_TEXTURE_CACHE_SLOTS ? stage_mips.slot[texture]:0;}
static void stage_mip_close(StageMipPlan *plan)
{if(plan->fd>=0)sceIoClose(plan->fd);plan->fd=-1;}
static void stage_mip_open(StageMipPlan *plan,Stage *s,const char *jgs_path,
                           void *memory,unsigned capacity)
{
    memset(plan,0,sizeof *plan);plan->fd=-1;
    if(!memory || capacity<STAGE_MIP_BYTES || !jgs_path)return;
    char path[256];size_t length=strlen(jgs_path);
    if(length<4 || length>=sizeof path || strcmp(jgs_path+length-4,".jgs"))return;
    memcpy(path,jgs_path,length+1);memcpy(path+length-3,"jgm",3);
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0777);if(fd<0)return;
    unsigned h[8];SceOff size=sceIoLseek(fd,0,PSP_SEEK_END);
    if(size<32 || size>32+STAGE_MIP_SLOTS*32+STAGE_MIP_BYTES ||
       sceIoLseek(fd,0,PSP_SEEK_SET)<0 || read_stage_part(fd,(unsigned char *)h,32) ||
       memcmp(h,"JGM1",4) || h[1]!=stage_mip_hash(s->base,s->h->off_vert) ||
       h[2]!=s->h->n_tex || !h[3] || h[3]>STAGE_MIP_SLOTS || !h[4] || h[4]>STAGE_MIP_BYTES ||
       (h[4]&15) || h[5]!=32 || h[6]!=32+h[3]*32 || h[7]!=(unsigned)size || h[6]+h[4]!=h[7] ||
       read_stage_part(fd,(unsigned char *)plan->record,h[3]*32))goto invalid;
    unsigned used=0;unsigned char seen[STAGE_TEXTURE_CACHE_SLOTS]={0};
    for(unsigned i=0;i<h[3];i++) {
        const StageMipRecord *r=plan->record+i;
        if(r->texture>=s->h->n_tex || r->texture>=STAGE_TEXTURE_CACHE_SLOTS || seen[r->texture] ||
           r->levels!=2 || r->reserved || r->w<64 || r->h<32 || r->w>512 || r->h>512 ||
           (r->w&(r->w-1)) || (r->h&(r->h-1)))goto invalid;
        const JgsTex *t=s->tex+r->texture;
        if(t->w!=r->w || t->h!=r->h || (t->flags&JGS_TEX_ALPHA) || t->off_pix!=t->off_clut+1024 ||
           r->offset[0]!=used || r->bytes[0]!=(unsigned)r->w*r->h/4 ||
           r->offset[1]!=r->offset[0]+r->bytes[0] || r->bytes[1]!=(unsigned)r->w*r->h/16 ||
           r->offset[1]>h[4] || r->bytes[1]>h[4]-r->offset[1])goto invalid;
        used=r->offset[1]+r->bytes[1];seen[r->texture]=1;
    }
    if(used!=h[4])goto invalid;
    plan->fd=fd;plan->count=h[3];plan->bytes=h[4];plan->pixel_offset=h[6];
    plan->memory=(unsigned char *)memory+(capacity&~15u)-plan->bytes;
    memset(&stage_mips,0,sizeof stage_mips);stage_mips.owner=s;
    return;
invalid:
    sceIoClose(fd);plan->count=plan->bytes=0;
}
/* Hash the palette and base pixels while they are already in the load buffer.
   A stale optional sidecar can never attach mips to a different palette. */
static void stage_mip_load_texture(StageMipPlan *plan,unsigned texture,
                                   const unsigned char *data,unsigned bytes)
{
    if(plan->fd<0)return;
    for(unsigned i=0;i<plan->count;i++) {
        const StageMipRecord *r=plan->record+i;
        if(r->texture!=texture || r->base_key!=stage_mip_hash(data,bytes))continue;
        unsigned words[128] __attribute__((aligned(16)));
        if(sceIoLseek(plan->fd,plan->pixel_offset+r->offset[0],PSP_SEEK_SET)<0)return;
        unsigned total=r->bytes[0]+r->bytes[1];
        volatile unsigned *dest=(volatile unsigned *)(plan->memory+r->offset[0]);
        for(unsigned at=0;at<total;) {
            unsigned chunk=total-at;if(chunk>sizeof words)chunk=sizeof words;
            if(read_stage_part(plan->fd,(unsigned char *)words,chunk))return;
            for(unsigned j=0;j<chunk/4;j++)dest[at/4+j]=words[j];
            at+=chunk;
        }
        stage_mips.slot[texture]=(unsigned char)(i+1);
        for(unsigned level=0;level<2;level++)stage_mips.pixels[i][level]=plan->memory+r->offset[level];
        stage_mips.count++;stage_mips.bytes+=total;
        return;
    }
}
static void stage_mip_state(Stage *s,unsigned texture)
{
    if(stage_mips.owner!=s)return;
    unsigned slot=stage_mip_slot(s,texture);
    if(stage_water_texture(s,texture))slot=0;
    sceGuTexMode(GU_PSM_T8,slot ? 2:0,0,1);
    sceGuTexFilter(slot ? GU_LINEAR_MIPMAP_NEAREST:GU_LINEAR,GU_LINEAR);
    if(slot) {
        const JgsTex *t=s->tex+texture;
        for(unsigned level=1;level<=2;level++)sceGuTexImage(level,t->w>>level,t->h>>level,t->w>>level,stage_mips.pixels[slot-1][level-1]);
    }
}
