#include "diagnostics.h"
#include "gfx.h"
#include <pspkernel.h>
#include <pspgu.h>
#include <pspgum.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include <float.h>
#include "stage.h"
#include "object_view.h"
#include "asset_progress.h"
#include "hang.h"
#include "skyclip.h"
#include "ge_submit.h"
#include "ge_projection.h"
#include "stage_water.inc"

/* View-safe grouping saved ~2% of binds but added CPU work in the native
   submission benchmark. Keep it available for A/B checks, disabled in play. */
static int command_cache_enabled=2;
static int fragment_batching=1;
static int mesh_clipping=1;
static int clip_reject_enabled=1;
static int visible_list_enabled=1;
static unsigned fragment_index_bytes;
static const Stage *fragment_owner;
static void fragment_cache_init(const Stage *s);
static void fragment_cache_forget(void);
static void stage_prepared_forget(void);
static void stage_capture_forget(void);
void stage_set_fragment_batching(int enabled){fragment_batching=enabled;}
void stage_clip_reject_mode(int enabled){clip_reject_enabled=!!enabled;}
void stage_visible_list_mode(int enabled)
{if(visible_list_enabled!=!!enabled){stage_prepared_forget();visible_list_enabled=!!enabled;}}
#ifdef JGR_STAGE_GROUPING
static int texture_grouping_enabled;
#endif
void stage_set_optimizations(int commands,int grouping)
{
    command_cache_enabled=commands;
#ifdef JGR_STAGE_GROUPING
    texture_grouping_enabled=grouping;
#else
    (void)grouping;
#endif
}
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS)
#ifdef __PSP__
#define command_packets (*(unsigned (*)[(STAGE_COMMAND_BYTES/192)*2][24])gfx_stage_command_memory())
#else
static unsigned command_packets[(STAGE_COMMAND_BYTES/192)*2][24] __attribute__((aligned(64)));
#endif
static const Stage *command_owner;
static unsigned command_count;
static void cache_material(Stage *s,const JgsBatch *b,unsigned *packet);
static void cache_commands(Stage *s)
{
    gfx_wait_idle();command_owner=NULL;command_count=0;
    if(!s || !s->base)return;
    command_count=s->h->n_batch;
    if(command_count>sizeof command_packets/sizeof command_packets[0])command_count=sizeof command_packets/sizeof command_packets[0];
    /* The SDK writes GU lists through the uncached alias. Retire cached
       lines before writing ANY prefix/geometry words through that alias,
       or a later dirty-line eviction can overwrite real hardware commands. */
    sceKernelDcacheWritebackInvalidateRange(command_packets,sizeof command_packets);
    for(unsigned i=0;i<command_count;i++) {
        unsigned *packet=(void *)((uintptr_t)command_packets[i]|0x40000000u);
        const JgsBatch *b=s->batch+i;unsigned *p=packet+16;
        cache_material(s,b,packet);
        uintptr_t idx=(uintptr_t)stage_batch_indices(s,b),v=(uintptr_t)stage_batch_vertices(s,b);
        p[0]=0x12000000u|GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_INDEX_16BIT|GU_TRANSFORM_3D;
        p[1]=0x10000000u|((idx>>8)&0xf0000);p[2]=0x02000000u|(idx&0xffffff);
        p[3]=0x10000000u|((v>>8)&0xf0000);p[4]=0x01000000u|(v&0xffffff);
        p[5]=0x04000000u|(GU_TRIANGLES<<16)|b->n_idx;p[6]=0x0b000000u;p[7]=0;
    }
#ifdef __PSP__
    __asm__ volatile("sync" ::: "memory");
#endif
    command_owner=s;
    fragment_cache_init(s);
}
#endif

/* Caller has fenced GE execution. Room rendering borrows the packet
   storage; a later city load repopulates it with that city's addresses. */
void stage_forget_commands(void)
{
    fragment_cache_forget();
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS)
    command_owner=NULL;command_count=0;
#endif
}

#define STAGE_TEXTURE_CACHE_SLOTS 512
static const Stage *texture_owner;
static const unsigned char *texture_address(const Stage *s,const JgsTex *t,unsigned offset)
{return s->texture_parts ? s->texture_parts[t-s->tex]+(offset-t->off_clut):
    s->texture_data ? s->texture_data+(offset-s->h->off_pix):s->base+offset;}
static const unsigned char *texture_pixels[STAGE_TEXTURE_CACHE_SLOTS];
static unsigned texture_count,texture_bytes,texture_hits,texture_misses;
#include "stage_mips.inc"
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS)
static void cache_material(Stage *s,const JgsBatch *b,unsigned *packet)
{
    /* Ask the installed SDK to encode the exact material commands. Each
       prefix falls through to the ordinary geometry packet at word 16.
       Alpha/blend/depth state stays owned by the existing ordered passes. */
    for(unsigned j=0;j<16;j++)packet[j]=0;
    sceGuStart(GU_CALL,packet);sceGuDisable(GU_CULL_FACE);
    if(b->flags&JGS_B_NOTEX)sceGuDisable(GU_TEXTURE_2D);
    else {
        const JgsTex *t=s->tex+b->tex;
        const unsigned char *pixels=texture_owner==s && b->tex<STAGE_TEXTURE_CACHE_SLOTS ? texture_pixels[b->tex]:NULL;
        sceGuEnable(GU_TEXTURE_2D);
        if(stage_mips.owner==s){sceGuTexMode(GU_PSM_T8,0,0,1);sceGuTexFilter(GU_LINEAR,GU_LINEAR);}
        sceGuClutLoad(32,pixels ? pixels-1024:texture_address(s,t,t->off_clut));
        sceGuTexImage(0,t->w,t->h,t->w,pixels ? pixels:texture_address(s,t,t->off_pix));
    }
    sceGuFinish();
    for(unsigned j=0;j<16;j++)if((packet[j]>>24)==0x0b)packet[j]=0;
}
#endif
static void stage_visibility_forget(const Stage *s);
static void stage_render_forget(const Stage *s);
static void forget_textures(const Stage *s)
{
    stage_capture_forget();
    stage_mip_forget(s);
    if(fragment_owner==s)fragment_cache_forget();
    stage_visibility_forget(s);
    stage_render_forget(s);
    if(texture_owner==s){texture_owner=NULL;texture_count=texture_bytes=0;}
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS)
    if(command_owner==s){command_owner=NULL;command_count=0;}
#endif
}
void stage_texture_profile(unsigned out[4])
{out[0]=texture_count;out[1]=texture_bytes;out[2]=texture_hits;out[3]=texture_misses;texture_hits=texture_misses=0;}
/* Pin the most frequently referenced texture bytes for the entire level.
   Remaining textures keep their resident main-RAM addresses. No frame-time
   uploads/eviction, texture resizing, or extra main-RAM pixel allocation. */
#include "stage_texture_usage.inc"
static const unsigned *texture_usage_weights(const Stage *s)
{
#ifndef JGR_TEXTURE_RESIDENCY_REFERENCE
    unsigned key=2166136261u;
    for(unsigned i=0;i<s->h->off_vert;i++)key=(key^s->base[i])*16777619u;
    for(unsigned i=0;i<sizeof stage_texture_usage/sizeof *stage_texture_usage;i++)
        if(stage_texture_usage[i].key==key && stage_texture_usage[i].count==s->h->n_tex)
            return stage_texture_usage[i].weight;
#else
    (void)s;
#endif
    return NULL; /* Unknown/rebuilt banks retain whole-stage selection. */
}
static void select_texture_pins(Stage *s,void *memory,unsigned capacity)
{
    texture_owner=NULL;texture_count=texture_bytes=0;memset(texture_pixels,0,sizeof texture_pixels);
    if(!s->base || !memory || ((uintptr_t)memory&15))return;
    unsigned count=s->h->n_tex<STAGE_TEXTURE_CACHE_SLOTS ? s->h->n_tex:STAGE_TEXTURE_CACHE_SLOTS;
    unsigned weight[STAGE_TEXTURE_CACHE_SLOTS]={0};
    for(unsigned i=0;i<s->h->n_obj;i++) {
        const JgsObj *o=s->obj+i;if(s->sky_explicit && (o->flags&JGS_O_SKY))continue;
        for(unsigned b=o->first_batch;b<o->first_batch+o->n_batch;b++) {
            const JgsBatch *batch=s->batch+b;
            if(!(batch->flags&JGS_B_NOTEX) && batch->tex<count)weight[batch->tex]+=batch->n_idx;
        }
    }
    const unsigned *usage=texture_usage_weights(s);
    if(usage)memcpy(weight,usage,count*sizeof *weight);
    for(;;) {
        unsigned best=count,best_bytes=0;
        for(unsigned t=0;t<count;t++)if(weight[t] && !texture_pixels[t]) {
            const JgsTex *tx=s->tex+t;unsigned pixels=(unsigned)tx->w*tx->h,bytes=pixels+1024;
            if(!pixels || (pixels&15) || tx->off_pix!=tx->off_clut+1024 || bytes>capacity-texture_bytes || (tx->off_pix&3) || (tx->off_clut&3))continue;
            if(best==count || (uint64_t)weight[t]*best_bytes>(uint64_t)weight[best]*bytes){best=t;best_bytes=bytes;}
        }
        if(best==count)break;
        texture_pixels[best]=(const unsigned char *)memory+texture_bytes+1024;
        texture_bytes+=best_bytes;texture_count++;
    }
}
static void copy_texture_pin(unsigned char *dest,const unsigned char *clut,const unsigned char *pixels,unsigned bytes)
{
    /* VRAM is accessed through the uncached alias with aligned words. */
    volatile unsigned *out=(volatile unsigned *)dest;
    const unsigned *pal=(const unsigned *)clut,*tex=(const unsigned *)pixels;
    for(unsigned i=0;i<256;i++)out[i]=pal[i];
    for(unsigned i=0;i<bytes/4;i++)out[256+i]=tex[i];
}
void stage_cache_textures(Stage *s,void *memory,unsigned capacity)
{
    gfx_wait_idle();
    /* Direct-loaded pins are the only copy. Their addresses stay fixed
       until stage_free; just rebuild the geometry/material packets. */
    if(texture_owner && texture_owner!=s && texture_owner->texture_vram)return;
    if(!s->texture_vram) {
        select_texture_pins(s,memory,capacity);
        if(!s->base || !memory || ((uintptr_t)memory&15))return;
        for(unsigned t=0;t<s->h->n_tex && t<STAGE_TEXTURE_CACHE_SLOTS;t++)if(texture_pixels[t]) {
            const JgsTex *tx=s->tex+t;
            copy_texture_pin((unsigned char *)texture_pixels[t]-1024,
                texture_address(s,tx,tx->off_clut),texture_address(s,tx,tx->off_pix),(unsigned)tx->w*tx->h);
            asset_progress();
        }
        texture_owner=s;
    }
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS)
    cache_commands(s);
#endif
}
unsigned stage_release_pinned_texture_copies(Stage *s)
{
    if(texture_owner!=s || !s->texture_parts || s->texture_vram || !texture_count)return 0;
    gfx_wait_idle();
    uintptr_t first=UINTPTR_MAX;
    for(unsigned i=0;i<s->h->n_tex && i<STAGE_TEXTURE_CACHE_SLOTS;i++)if(texture_pixels[i]) {
        uintptr_t at=(uintptr_t)texture_pixels[i]-1024;if(at<first)first=at;
    }
    s->texture_vram=(const unsigned char *)first;s->texture_vram_bytes=texture_bytes;
    for(unsigned i=0;i<s->h->n_tex && i<STAGE_TEXTURE_CACHE_SLOTS;i++)if(texture_pixels[i]) {
        free(s->texture_parts[i]);s->texture_parts[i]=(unsigned char *)texture_pixels[i]-1024;
    }
    return texture_bytes;
}
static StageProfile profile;
void stage_profile_totals(StageProfile *out){*out=profile;}
void stage_profile_read(StageProfile *out)
{ *out=profile;profile=(StageProfile){0}; }
#include "stage_city_profile.inc"

#define STAGE_VISIBILITY_CACHE 4096
static int stage_batch_alpha(const Stage *s,const JgsBatch *b);
#include "stage_instances.inc"
#include "stage_visibility.inc"
#include "stage_render.inc"
#include "stage_ped_proxy.inc"
#include "stage_capture_state.inc"
static void stage_prepared_forget(void)
{
    stage_ped_proxy_forget();
    /* Early preparation is a single draw's source-selection transaction.
       An abandoned preparation must not consume one-draw transition coverage. */
    if(stage_prepared.pending && source_render.owner==stage_prepared.owner)
        source_render.previous_special=stage_prepared.previous_special;
    stage_prepared.owner=NULL;stage_prepared.base=NULL;
    stage_prepared.ready=stage_prepared.pending=stage_prepared.building=0;
    stage_prepared.cone_count=0;
    stage_prepared.visible_count=stage_prepared.visible_valid=0;
}
void stage_set_scenery_mode(int enabled)
{
    mesh_clipping=!!enabled;
}

/* Only offline-verified surface overlays receive the paint depth treatment.
   The complete alpha triangle must have opaque coverage on/just behind it.
   Keep its biased depth out of the buffer so later actors/decals compare
   against the actual supporting surface. Ordinary glass/fences are unchanged. */
static void stage_decal_apply(const Stage *s,const JgsBatch *b,unsigned *current)
{
    unsigned i=(unsigned)(b-s->batch);
    unsigned bias=stage_decal_bits && visibility_owner==s->base && (b->flags&JGS_B_ALPHA) &&
        (stage_decal_bits[i>>3]&(1u<<(i&7))) ? GE_DECAL_DEPTH_BIAS:0;
    if(bias!=*current){CITY_SCOPE(s,CITY_MATERIAL);sceGuDepthOffset(bias);sceGuDepthMask(bias ? GU_TRUE:GU_FALSE);*current=bias;}
}
static void stage_decal_end(unsigned current)
{if(current){CITY_SCOPE(NULL,CITY_MATERIAL);sceGuDepthOffset(0);sceGuDepthMask(GU_FALSE);}}

static void prepare_batch_bounds(Stage *s)
{
    if(!s->h->n_batch || s->h->n_batch>16384)return;
    /* The unused tail of the resident command arena can own city bounds.
       It is outside every cached packet and lives until the city retires.
       This avoids another 75 KiB heap allocation beside the new far meshes. */
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS)
    unsigned used=s->h->n_batch*sizeof command_packets[0];
    unsigned bytes=s->h->n_batch*sizeof *s->batch_bounds;
    if(s->texture_vram && used<=sizeof command_packets && bytes<=sizeof command_packets-used) {
        s->batch_bounds=(void *)((unsigned char *)command_packets+used);s->batch_bounds_borrowed=1;
    }else
#endif
    s->batch_bounds=malloc(s->h->n_batch*sizeof *s->batch_bounds);
    if(!s->batch_bounds)return;
    for(unsigned i=0;i<s->h->n_batch;i++) {
        const JgsBatch *b=s->batch+i;StageBatchBound *bound=s->batch_bounds+i;
        *bound=(StageBatchBound){0,0,0,INFINITY};
        if(!b->n_vert || b->vert_off>s->h->n_vert || b->n_vert>s->h->n_vert-b->vert_off)continue;
        const JgsVert *vertices=stage_batch_vertices(s,b);
        float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
        for(unsigned j=0;j<b->n_vert;j++) {
            const JgsVert *v=vertices+j;float p[3]={v->x,v->y,v->z};
            for(int k=0;k<3;k++){if(!isfinite(p[k]))goto next_bound;if(p[k]<low[k])low[k]=p[k];if(p[k]>high[k])high[k]=p[k];}
        }
        float cx=(low[0]+high[0])*.5f,cy=(low[1]+high[1])*.5f,cz=(low[2]+high[2])*.5f;
        float dx=high[0]-low[0],dy=high[1]-low[1],dz=high[2]-low[2];
        float r=sqrtf(dx*dx+dy*dy+dz*dz)*.5f;
        r+=.05f+(fabsf(cx)+fabsf(cy)+fabsf(cz)+r)*1e-5f;
        *bound=(StageBatchBound){cx,cy,cz,r};
next_bound:;
    }
    /* The shared payload's local bounds are not world bounds. Only
       instanced objects need this second, load-time transform pass. */
    for(unsigned o=0;o<s->h->n_obj;o++) {
        const StageInstance *instance=stage_object_instance(s,o);if(!instance)continue;
        const JgsObj *object=s->obj+o;
        for(unsigned bi=object->first_batch;bi<object->first_batch+object->n_batch;bi++) {
            const JgsBatch *batch=s->batch+bi;const JgsVert *vertices=stage_batch_vertices(s,batch);
            float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
            for(unsigned j=0;j<batch->n_vert;j++) {
                float p[3];stage_instance_point(instance,vertices+j,p);
                for(unsigned k=0;k<3;k++){if(p[k]<low[k])low[k]=p[k];if(p[k]>high[k])high[k]=p[k];}
            }
            float center[3],r=0;
            for(unsigned k=0;k<3;k++){center[k]=(low[k]+high[k])*.5f;float d=high[k]-low[k];r+=d*d;}
            r=sqrtf(r)*.5f;r+=.05f+(fabsf(center[0])+fabsf(center[1])+fabsf(center[2])+r)*1e-5f;
            s->batch_bounds[bi]=(StageBatchBound){center[0],center[1],center[2],r};
        }
    }
}

#include "stage_clip_groups.inc"

struct StageGeometryPage {StageGeometryPage *next;};
static void release_stage_data(Stage *s)
{
    if(s->texture_parts){for(unsigned i=0;i<s->h->n_tex;i++) {
        uintptr_t p=(uintptr_t)s->texture_parts[i],v=(uintptr_t)s->texture_vram;
        if(!s->texture_vram || p<v || p-v>=s->texture_vram_bytes)free(s->texture_parts[i]);
    }free(s->texture_parts);}
    while(s->geometry_pages){StageGeometryPage *next=s->geometry_pages->next;free(s->geometry_pages);s->geometry_pages=next;}
    free(s->geometry);
    free(s->object_passes);free(s->prop_draw_cache);
    free(s->vertex_data);free(s->index_data);free(s->texture_data);
    if(!s->batch_bounds_borrowed)free(s->batch_bounds);
    free(s->clip_groups);free(s->base);memset(s,0,sizeof *s);
}
static int read_stage_part(SceUID fd,unsigned char *p,unsigned bytes)
{
    while(bytes) {
        int n=sceIoRead(fd,p,bytes>ASSET_READ_CHUNK ? ASSET_READ_CHUNK:bytes);
        if(n<=0)return -3;
        p+=n;bytes-=n;asset_progress();
    }
    return 0;
}
static int stage_layout_valid(const JgsHeader *h,unsigned size)
{
    return !memcmp(h->magic,"JGS1",4) && h->off_tex>=sizeof *h &&
        !((h->off_tex|h->off_obj|h->off_batch|h->off_vert|h->off_idx|h->off_pix)&3) &&
        h->off_tex<=h->off_obj && h->off_obj<=h->off_batch && h->off_batch<=h->off_vert &&
        h->off_vert<=h->off_idx && h->off_idx<=h->off_pix && h->off_pix<=size &&
        h->n_tex<=(h->off_obj-h->off_tex)/sizeof(JgsTex) &&
        h->n_obj<=(h->off_batch-h->off_obj)/sizeof(JgsObj) &&
        h->n_batch<=(h->off_vert-h->off_batch)/sizeof(JgsBatch) &&
        h->n_vert<=(h->off_idx-h->off_vert)/sizeof(JgsVert) &&
        h->n_idx<=(h->off_pix-h->off_idx)/sizeof(unsigned short);
}

/* A complete texture section can still exceed the largest free heap span
   after credits. Geometry stays indexed and contiguous; each texture owns
   its original palette and swizzled pixels until the stage retires. */
static int read_stage_texture_parts(Stage *s,SceUID fd,const JgsHeader *h,unsigned size,void *pin_memory,unsigned pin_capacity,const char *path)
{
    if(h->n_tex && !(s->texture_parts=calloc(h->n_tex,sizeof *s->texture_parts)))return -2;
    StageMipPlan mip_plan={.fd=-1};int result=0;
    if(pin_memory) {
        s->obj=(JgsObj *)(s->base+h->off_obj);s->batch=(JgsBatch *)(s->base+h->off_batch);
        for(unsigned i=0;i<h->n_obj;i++)if(s->obj[i].first_batch>h->n_batch || s->obj[i].n_batch>h->n_batch-s->obj[i].first_batch)return -4;
        stage_mip_open(&mip_plan,s,path,pin_memory,pin_capacity);
        select_texture_pins(s,pin_memory,(pin_capacity&~15u)-mip_plan.bytes);
        s->texture_vram=pin_memory;s->texture_vram_bytes=mip_plan.bytes ? pin_capacity:texture_bytes;
    }
    for(unsigned i=0;i<h->n_tex;i++) {
        const JgsTex *t=s->tex+i;unsigned pixels=(unsigned)t->w*t->h;
        if(t->off_clut<h->off_pix || t->off_clut>size || size-t->off_clut<1024 ||
           t->off_pix<t->off_clut+1024 || t->off_pix>size || pixels>size-t->off_pix ||
           ((t->off_pix-t->off_clut)&15)){result=-4;goto done;}
        unsigned bytes=t->off_pix-t->off_clut+pixels;
        int pinned=pin_memory && i<STAGE_TEXTURE_CACHE_SLOTS && texture_pixels[i];
        unsigned char *data=memalign(64,(bytes+63)&~63u);if(!data){result=-2;goto done;}
        if(!pinned)s->texture_parts[i]=data;
        if(sceIoLseek(fd,t->off_clut,PSP_SEEK_SET)<0 || read_stage_part(fd,data,bytes)) {
            if(pinned)free(data);
            result=-3;goto done;
        }
        stage_mip_load_texture(&mip_plan,i,data,bytes);
        if(pinned) {
            copy_texture_pin((unsigned char *)texture_pixels[i]-1024,data,data+t->off_pix-t->off_clut,pixels);
            free(data);s->texture_parts[i]=(unsigned char *)texture_pixels[i]-1024;
        }
    }
    if(pin_memory)texture_owner=s;
    if(mip_plan.bytes)printf("STAGE_MIPS textures=%u bytes=%u reserve=%u base_vram=%u\n",stage_mips.count,stage_mips.bytes,mip_plan.bytes,texture_bytes);
done:
    stage_mip_close(&mip_plan);return result;
}

static int read_stage_textures_separately(Stage *s,SceUID fd,const JgsHeader *h,unsigned size,void *pin_memory,unsigned pin_capacity,const char *path)
{
    unsigned lengths[3]={h->off_vert,h->off_idx-h->off_vert,h->off_pix-h->off_idx};
    unsigned char **parts[3]={&s->base,&s->vertex_data,&s->index_data};unsigned allocated=0;
    for(unsigned pass=0;pass<3;pass++) {
        unsigned best=3;
        for(unsigned j=0;j<3;j++)if(!(allocated&(1u<<j)) && (best==3 || lengths[j]>lengths[best]))best=j;
        allocated|=1u<<best;
        if(lengths[best] && !(*parts[best]=memalign(64,(lengths[best]+63)&~63u)))return -2;
    }
    if(sceIoLseek(fd,0,PSP_SEEK_SET)<0)return -3;
    for(unsigned j=0;j<3;j++)if(read_stage_part(fd,*parts[j],lengths[j]))return -3;
    s->h=(JgsHeader *)s->base;s->tex=(JgsTex *)(s->base+h->off_tex);
    return read_stage_texture_parts(s,fd,h,size,pin_memory,pin_capacity,path);
}

/* The PSP can have >8 MiB free after movie 29 without a 2.6 MiB geometry
   span. Pack consecutive whole batches into resident pages, retaining the
   exact index/vertex bytes and draw order. A batch never crosses a page;
   unusually large batches get their own allocation. No frame-time copies. */
static int read_stage_geometry_pages(Stage *s,SceUID fd,const JgsHeader *h,unsigned size,void *pin_memory,unsigned pin_capacity,const char *path)
{
    s->base=memalign(64,(h->off_vert+63)&~63u);if(!s->base)return -2;
    if(sceIoLseek(fd,0,PSP_SEEK_SET)<0 || read_stage_part(fd,s->base,h->off_vert))return -3;
    s->h=(JgsHeader *)s->base;s->tex=(JgsTex *)(s->base+h->off_tex);
    s->batch=(JgsBatch *)(s->base+h->off_batch);
    for(unsigned i=0;i<h->n_batch;i++) {
        const JgsBatch *b=s->batch+i;
        if(b->vert_off>h->n_vert || b->n_vert>h->n_vert-b->vert_off ||
           b->idx_off>h->n_idx || b->n_idx>h->n_idx-b->idx_off)return -4;
    }
    s->geometry=calloc(h->n_batch,sizeof *s->geometry);
    if(h->n_batch && !s->geometry)return -2;
    /* Packed banks share exact payload ranges across batches. Visit those
       ranges in file order, so fragmented loading keeps the same sharing
       instead of allocating another copy for each instance. */
    unsigned *order=malloc(h->n_batch*sizeof *order);if(!order)return -2;
    int result=0;
    for(unsigned kind=0;kind<2 && !result;kind++) {
        unsigned stride=kind ? 2:sizeof(JgsVert),offset=kind ? h->off_idx:h->off_vert;
        for(unsigned i=0;i<h->n_batch;i++) {
            unsigned at=kind ? s->batch[i].idx_off:s->batch[i].vert_off,j=i;
            while(j && (kind ? s->batch[order[j-1]].idx_off:s->batch[order[j-1]].vert_off)>at){order[j]=order[j-1];j--;}
            order[j]=i;
        }
        for(unsigned first=0;first<h->n_batch;) {
            const JgsBatch *b=s->batch+order[first];
            unsigned start=kind ? b->idx_off:b->vert_off,end=start+(kind ? b->n_idx:b->n_vert),last=first+1;
            while(last<h->n_batch) {
                b=s->batch+order[last];unsigned stop=(kind ? b->idx_off+b->n_idx:b->vert_off+b->n_vert);
                if(stop>end && (stop-start)*stride>128u*1024u)break;
                if(stop>end)end=stop;
                last++;
            }
            unsigned bytes=(end-start)*stride;
            StageGeometryPage *page=memalign(64,64+((bytes+63)&~63u));if(!page){result=-2;break;}
            page->next=s->geometry_pages;s->geometry_pages=page;
            unsigned char *data=(unsigned char *)page+64;
            if(sceIoLseek(fd,offset+start*stride,PSP_SEEK_SET)<0 || read_stage_part(fd,data,bytes)){result=-3;break;}
            for(unsigned i=first;i<last;i++) {
                unsigned bi=order[i];b=s->batch+bi;
                if(kind)s->geometry[bi].indices=(unsigned short *)data+(b->idx_off-start);
                else s->geometry[bi].vertices=(JgsVert *)data+(b->vert_off-start);
            }
            first=last;asset_progress();
        }
    }
    free(order);if(result)return result;
    return read_stage_texture_parts(s,fd,h,size,pin_memory,pin_capacity,path);
}

static int stage_load_internal(Stage *s,const char *path,int separate_textures,void *pin_memory,unsigned pin_capacity)
{
    forget_textures(s);
    memset(s, 0, sizeof *s);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd < 0) return -1;
    SceOff size = sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    JgsHeader header;int result=-4;
    if(size<(SceOff)sizeof header || size>0x02000000 ||
       read_stage_part(fd,(unsigned char *)&header,sizeof header) ||
       !stage_layout_valid(&header,(unsigned)size))goto failed;
    if(sceIoLseek(fd,0,PSP_SEEK_SET)<0){result=-3;goto failed;}
    if(separate_textures){result=read_stage_textures_separately(s,fd,&header,(unsigned)size,pin_memory,pin_capacity,path);goto loaded;}
#ifndef JGR_STAGE_SPLIT_ALLOC
    s->base=memalign(64,((unsigned)size+63)&~63u);
#endif
    if(s->base)result=read_stage_part(fd,s->base,(unsigned)size);
    else {
        /* A movie/level transition can leave enough total memory without
           one 6.5 MiB hole. Keep metadata, vertices, indices and texture
           bytes in separately aligned, immutable allocations. The file
           and all GE payloads remain identical. Largest blocks go first. */
        unsigned lengths[4]={header.off_vert,header.off_idx-header.off_vert,
            header.off_pix-header.off_idx,(unsigned)size-header.off_pix};
        unsigned char *parts[4]={0};unsigned allocated=0;
        result=-2;
        for(unsigned pass=0;pass<4;pass++) {
            unsigned best=4;
            for(unsigned j=0;j<4;j++)if(!(allocated&(1u<<j)) && (best==4 || lengths[j]>lengths[best]))best=j;
            allocated|=1u<<best;
            if(lengths[best] && !(parts[best]=memalign(64,(lengths[best]+63)&~63u))) {
                for(unsigned j=0;j<4;j++)free(parts[j]);
                printf("STAGE_ALLOC separate_textures section=%u bytes=%u\n",best,lengths[best]);
                result=read_stage_textures_separately(s,fd,&header,(unsigned)size,pin_memory,pin_capacity,path);
                goto loaded;
            }
        }
        s->base=parts[0];s->vertex_data=parts[1];s->index_data=parts[2];s->texture_data=parts[3];
        for(unsigned j=0;j<4;j++)if((result=read_stage_part(fd,parts[j],lengths[j])))goto failed;
    }
loaded:
    if(result==-2) {
        forget_textures(s);release_stage_data(s);
        result=read_stage_geometry_pages(s,fd,&header,(unsigned)size,pin_memory,pin_capacity,path);
        printf("STAGE_ALLOC geometry_pages result=%d batches=%u\n",result,header.n_batch);
    }
    if(result)goto failed;
    sceIoClose(fd);
    s->h = (JgsHeader *)s->base;
    s->tex = (JgsTex *)(s->base + s->h->off_tex);
    s->obj = (JgsObj *)(s->base + s->h->off_obj);
    s->batch = (JgsBatch *)(s->base + s->h->off_batch);
    s->vert = s->geometry ? NULL:(JgsVert *)(s->vertex_data ? s->vertex_data:s->base+s->h->off_vert);
    s->idx = s->geometry ? NULL:(unsigned short *)(s->index_data ? s->index_data:s->base+s->h->off_idx);
    for(unsigned i=0;i<s->h->n_obj;i++)if(s->obj[i].first_batch>s->h->n_batch || s->obj[i].n_batch>s->h->n_batch-s->obj[i].first_batch){forget_textures(s);release_stage_data(s);return -4;}
    for(unsigned i=0;i<s->h->n_batch;i++) {
        const JgsBatch *b=s->batch+i;
        if(b->vert_off>s->h->n_vert || b->n_vert>s->h->n_vert-b->vert_off || b->idx_off>s->h->n_idx || b->n_idx>s->h->n_idx-b->idx_off || (!(b->flags&JGS_B_NOTEX) && b->tex>=s->h->n_tex)){forget_textures(s);release_stage_data(s);return -4;}
    }
    if(stage_bind_instances(s)){forget_textures(s);release_stage_data(s);return -4;}
    for(unsigned i=0;i<s->h->n_tex;i++) {
        const JgsTex *t=s->tex+i;unsigned pixels=(unsigned)t->w*t->h;
        if(t->off_clut<s->h->off_pix || t->off_clut>(unsigned)size || (unsigned)size-t->off_clut<1024 ||
           t->off_pix<s->h->off_pix || t->off_pix>(unsigned)size || pixels>(unsigned)size-t->off_pix) {
            forget_textures(s);release_stage_data(s);return -4;
        }
    }
    stage_prepare_object_passes(s);prepare_batch_bounds(s);
    prepare_clip_groups(s);
    stage_ped_proxy_bind(s);
    sceKernelDcacheWritebackAll();
    return 0;
failed:
    sceIoClose(fd);forget_textures(s);release_stage_data(s);return result;
}

int stage_load(Stage *s,const char *path){return stage_load_internal(s,path,0,NULL,0);}
int stage_load_city(Stage *s,const char *path){return stage_load_internal(s,path,1,NULL,0);}
int stage_load_city_vram(Stage *s,const char *path,void *memory,unsigned capacity)
{
    if(memory && ((uintptr_t)memory&15))return -4;
    if(texture_owner && texture_owner!=s && texture_owner->texture_vram)return -5;
    gfx_wait_idle();return stage_load_internal(s,path,1,memory,capacity);
}

/* -1: invisible, 0: wholly inside, otherwise intersected plane bits. */
static inline int sphere_clip_mask(const float planes[6][4], const JgsObj *o)
{
    int mask=0;
    for (int i = 0; i < 6; i++) {
        float d = planes[i][0] * o->cx + planes[i][1] * o->cy + planes[i][2] * o->cz + planes[i][3];
        if (d < -o->r) return -1;
        if (d < o->r) mask|=1<<i;
    }
    return mask;
}
static inline int batch_clip_mask(const float planes[6][4],const StageBatchBound *b)
{
    int mask=0;
    for(int i=0;i<6;i++) {
        float d=planes[i][0]*b->cx+planes[i][1]*b->cy+planes[i][2]*b->cz+planes[i][3];
        if(d < -b->r)return -1;
        if(d < b->r)mask|=1<<i;
    }
    return mask;
}

/* Reuse the existing coarse/fine bounds, without more resident metadata.
   A coarse decision replaces two fine tests only when all geometry is
   accepted/rejected. Otherwise retain the original fine masks. Borrow the
   VFPU once for this batch; GU calls and polygon clipping run after restore. */
static void mesh_group_masks(const float guard[6][4],const StageBatchBound *groups,
                             const StageBatchBound *coarse,unsigned count,unsigned mask,signed char *out)
{
    ClipMathPlanes packed;clip_math_prepare(&packed,guard);
    RenderMathState math;render_math_begin(&math);clip_math_load(&packed);
    for(unsigned i=0;i<count;) {
        if(coarse) {
            const StageBatchBound *b=coarse+i/2;
            profile.mesh_bound_tests++;
            int m=clip_math_sphere_mask(&packed,&b->cx,b->r);
            if(m>=0)m&=mask;
            if(m<=0){profile.mesh_coarse_decisions++;out[i++]=(signed char)m;if(i<count)out[i++]=(signed char)m;continue;}
        }
        unsigned end=i+(coarse ? 2:1);if(end>count)end=count;
        do {
            const StageBatchBound *b=groups+i;
            profile.mesh_bound_tests++;
            int m=clip_math_sphere_mask(&packed,&b->cx,b->r);
            out[i++]=(signed char)(m<0 ? -1:m&(int)mask);
        } while(i<end);
    }
    render_math_end(&math);
}

#define VFMT (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D)

/* Selective GE clipping: a single corner outside the PSP's
   0..4096 screen-coordinate range can discard a whole road/wall triangle.
   Object visibility alone cannot prevent this. Use a conservative NDC +/-4
   guard region: at our maximum 480x272 viewport centered at 2048, this is
   x=1088..3008, y=1504..2592, well inside the GE limit. The ending viewport
   is smaller. Keep ordinary off-screen triangles on the GE path; clip only
   guard/near/far crossings back to the actual frustum.
   Clip in world space so UV/color interpolation and GE perspective remain
   intact. Whole inside objects and contiguous inside triangles retain the
   original indexed path; only unsafe triangles need transient vertices. */
static void clip_planes(const float c[16],float view[6][4],float guard[6][4])
{
    for(int p=0;p<6;p++) {
        float vn=0,gn=0,scale=p<4 ? .25f:1.f,sign=(p&1) ? -1.f:1.f;
        for(int j=0;j<4;j++) {
            view[p][j]=c[12+j]+sign*c[(p/2)*4+j];
            guard[p][j]=c[12+j]+sign*scale*c[(p/2)*4+j];
            if(j<3){vn+=view[p][j]*view[p][j];gn+=guard[p][j]*guard[p][j];}
        }
        vn=sqrtf(vn);gn=sqrtf(gn);
        for(int j=0;j<4;j++){view[p][j]/=vn;guard[p][j]/=gn;}
    }
}
typedef struct { unsigned short index[256];unsigned char code[256],known[256]; } ClipCache;
static unsigned vertex_code(ClipCache *cache,unsigned index,const JgsVert *vertices,
                            const float planes[6][4],unsigned mask)
{
    unsigned slot=index&255;
    if(cache->index[slot]==index && (cache->known[slot]&mask)==mask)return cache->code[slot]&mask;
    const JgsVert *v=vertices+index;unsigned code=0;
    for(unsigned p=0;p<6;p++)if(mask&(1u<<p)) {
        const float *q=planes[p];
        if(q[0]*v->x+q[1]*v->y+q[2]*v->z+q[3]<0)code|=1u<<p;
    }
    cache->index[slot]=index;cache->code[slot]=code;cache->known[slot]=mask;
    profile.classified_vertices++;profile.scalar_vertices++;return code;
}
/* No GU calls inside this bounded prepass. Multiple triangles share the
   original direct-mapped index cache and one set of saved VFPU registers.
   Single-plane batches retain their cheaper scalar expression. */
static void vertex_codes(ClipCache *cache,const unsigned short *indices,unsigned count,
                         const JgsVert *vertices,const ClipMathPlanes *packed,
                         unsigned mask,unsigned char *out)
{
    unsigned stamp=vfpu_profile_vertices_begin(),tested=0;
    RenderMathState math;render_math_begin(&math);clip_math_load(packed);
    for(unsigned i=0;i<count;i++) {
        unsigned index=indices[i],slot=index&255;
        if(cache->index[slot]!=index || (cache->known[slot]&mask)!=mask) {
            const JgsVert *v=vertices+index;
            cache->index[slot]=index;
            cache->code[slot]=clip_math_point_code(packed,&v->x,mask);cache->known[slot]=mask;tested++;
        }
        out[i]=cache->code[slot]&mask;
    }
    render_math_end(&math);vfpu_profile_vertices_end(stamp,tested);profile.classified_vertices+=tested;
}
static void draw_indexed(Stage *s,const JgsVert *vertices,const unsigned short *indices,unsigned count)
{
    if(!count)return;
    CITY_SCOPE(s,CITY_SUBMISSION);
    ge_submit_gum(GU_TRIANGLES,VFMT,count,indices,vertices);
    s->drawn_tris+=count/3;s->drawn_batches++;profile.draw_calls++;
}
static void draw_clipped(Stage *s,const JgsVert *vertices,unsigned count)
{
    if(!count)return;
    CITY_SCOPE(s,CITY_GEOMETRY);
    JgsVert *out=sceGuGetMemory(count*sizeof *out);
    for(unsigned i=0;i<count;i++) {
        /* Aligned full-word stores into uncached GU memory, as for actor
           packets. Avoid memcpy's Allegrex partial-word read/modify/write. */
        out[i].u=vertices[i].u;out[i].v=vertices[i].v;out[i].color=vertices[i].color;
        out[i].x=vertices[i].x;out[i].y=vertices[i].y;out[i].z=vertices[i].z;
    }
    {CITY_SCOPE(s,CITY_SUBMISSION);ge_submit_gum(GU_TRIANGLES,VFMT & ~GU_INDEX_16BIT,count,NULL,out);}
    s->drawn_tris+=count/3;s->drawn_batches++;profile.draw_calls++;
    profile.clipped_vertices+=count;
}
/* Rejected triangles split a source mesh into many indexed draws. Retain
   their surviving spans, in order, and join only the index stream. Vertices
   stay resident; real clipped geometry remains an ordering barrier. A lone
   span uses its original pointer with no copy. Bound transient list use. */
typedef struct {unsigned first,count;} StageIndexSpan;
typedef struct {StageIndexSpan span[32];unsigned count,indices;} StageIndexRuns;
/* Use only the otherwise unused command-arena tail, after all material
   packets and borrowed batch bounds. No new heap or vertex copy. Entries
   used by the current GE list stay pinned until gfx_begin's existing fence. */
typedef struct {
    const unsigned short *source;StageIndexRuns runs;unsigned *payload;
} StageFragmentEntry;
static unsigned char *fragment_cache;
static unsigned fragment_slots,fragment_entries,fragment_used,fragment_capacity,fragment_full;
static unsigned char *fragment_payload;
static StageFragmentEntry *fragment_entry(unsigned i)
{return (StageFragmentEntry *)fragment_cache+i;}
void stage_frame_begin(void)
{
    city_frame_begin();
    stage_capture_forget();
    stage_prepared_forget();
    /* Append-only between fences: a later draw can never overwrite indices
       already referenced by an in-flight GE command, even on cache overflow. */
    if(fragment_full)fragment_entries=fragment_used=fragment_full=0;
}
static void fragment_cache_forget(void)
{fragment_owner=NULL;fragment_cache=fragment_payload=NULL;fragment_slots=fragment_entries=fragment_used=fragment_capacity=fragment_full=0;}
static void fragment_cache_bind(const Stage *s,void *memory,unsigned bytes)
{
    fragment_cache_forget();
    unsigned metadata=(32*sizeof(StageFragmentEntry)+63u)&~63u;
    if(bytes<metadata+16384)return;
    fragment_cache=memory;fragment_owner=s;fragment_slots=32;
    fragment_payload=fragment_cache+metadata;fragment_capacity=bytes-metadata;
}
static void fragment_cache_init(const Stage *s)
{
    fragment_cache_forget();
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS)
    if(s->texture_vram && s->batch_bounds_borrowed) {
        unsigned used=(s->h->n_batch*(sizeof command_packets[0]+sizeof *s->batch_bounds)+63u)&~63u;
        if(used>sizeof command_packets)return;
        fragment_cache_bind(s,(unsigned char *)command_packets+used,sizeof command_packets-used);
    }
#else
    (void)s;
#endif
}
static StageFragmentEntry *fragment_cache_slot(const Stage *s,const unsigned short *indices,const StageIndexRuns *runs,int *hit)
{
    *hit=0;if(fragment_owner!=s)return NULL;
    for(unsigned i=0;i<fragment_entries;i++) {
        StageFragmentEntry *e=fragment_entry(i);
        if(e->source==indices && e->runs.count==runs->count && e->runs.indices==runs->indices &&
           !memcmp(e->runs.span,runs->span,runs->count*sizeof runs->span[0])) {
            *hit=1;return e;
        }
    }
    unsigned bytes=(runs->indices*2+63u)&~63u;
    if(fragment_entries==fragment_slots || bytes>fragment_capacity-fragment_used){fragment_full=1;return NULL;}
    StageFragmentEntry *e=fragment_entry(fragment_entries++);
    e->source=indices;e->runs=*runs;e->payload=(void *)(fragment_payload+fragment_used);fragment_used+=bytes;
    return e;
}
static void flush_index_runs(Stage *s,const JgsVert *vertices,const unsigned short *indices,StageIndexRuns *runs)
{
    if(!runs->count)return;
    CITY_SCOPE(s,CITY_INDICES);
    unsigned bytes=(runs->indices*2+15)&~15u;
    if(runs->count>1 && runs->indices<=8190 && fragment_index_bytes+bytes<=64u*1024u) {
        /* Full word writes avoid read/modify/write through the uncached GU
           alias. Every halfword still follows the original primitive order. */
        int hit;StageFragmentEntry *entry=fragment_cache_slot(s,indices,runs,&hit);
        unsigned *out=entry ? entry->payload:sceGuGetMemory(bytes),word=0,at=0;
        if(!hit)for(unsigned r=0;r<runs->count;r++) {
            const StageIndexSpan *span=runs->span+r;
            const unsigned short *src=indices+span->first;unsigned left=span->count;
            if(at&1){out[at/2]=word|((unsigned)*src++<<16);at++;left--;}
            while(left>=2){out[at/2]=(unsigned)src[0]|((unsigned)src[1]<<16);src+=2;at+=2;left-=2;}
            if(left){word=*src;at++;}
        }
        if(!hit) {
            if(at&1)out[at/2]=word;
            profile.joined_index_bytes+=bytes;
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS)
            if(entry)sceKernelDcacheWritebackRange(out,(bytes+63u)&~63u);
#endif
        }
        if(!entry)fragment_index_bytes+=bytes;
        if(hit)profile.index_cache_hits++;
        profile.joined_draws+=runs->count-1;
        draw_indexed(s,vertices,(const unsigned short *)out,runs->indices);
    } else for(unsigned r=0;r<runs->count;r++)
        draw_indexed(s,vertices,indices+runs->span[r].first,runs->span[r].count);
    runs->count=runs->indices=0;
}
static void append_index_run(Stage *s,const JgsVert *vertices,const unsigned short *indices,
                             StageIndexRuns *runs,unsigned first,unsigned count)
{
    if(!count)return;
    if(!fragment_batching){draw_indexed(s,vertices,indices+first,count);return;}
    if(runs->count && runs->span[runs->count-1].first+runs->span[runs->count-1].count==first)
        runs->span[runs->count-1].count+=count;
    else {
        if(runs->count==32)flush_index_runs(s,vertices,indices,runs);
        runs->span[runs->count++]=(StageIndexSpan){first,count};
    }
    runs->indices+=count;
}
/* The wide GE guard accepts ordinary off-screen geometry. A triangle that
   crosses that guard may nevertheless lie wholly beyond a real screen edge.
   Reject using positions before allocating/copying/interpolating attributes.
   Scalar FPU math leaves any live clipping VFPU scope untouched. Keep the
   original multiplication order and a scale-dependent negative margin for
   rounding through earlier clipping planes; uncertain/non-finite cases use
   the unchanged clipper. No batch-bound assumption: instances use local
   vertices/planes while their stored batch bounds may be in world space. */
static inline int stage_triangle_trivially_outside(const JgsVert *a,const JgsVert *b,
                                                  const JgsVert *c,const float planes[6][4])
{
    for(unsigned p=0;p<4;p++) {
        const float *q=planes[p];
        float ax=q[0]*a->x,ay=q[1]*a->y,az=q[2]*a->z;
        float da=ax+ay+az+q[3];if(!(da<0))continue;
        float bx=q[0]*b->x,by=q[1]*b->y,bz=q[2]*b->z;
        float db=bx+by+bz+q[3];if(!(db<0))continue;
        float cx=q[0]*c->x,cy=q[1]*c->y,cz=q[2]*c->z;
        float dc=cx+cy+cz+q[3];if(!(dc<0))continue;
        float scale=1.f+fabsf(q[3])+fabsf(ax)+fabsf(ay)+fabsf(az)+
            fabsf(bx)+fabsf(by)+fabsf(bz)+fabsf(cx)+fabsf(cy)+fabsf(cz);
        float margin=(32.f*FLT_EPSILON)*scale;
        if(isfinite(margin) && da < -margin && db < -margin && dc < -margin)return 1;
    }
    return 0;
}
/* Share register preservation across at most 64 clip candidates. The
   existing output buffer and indexed spans still determine draw order.
   Every GU/GUM publication below ends the scope first: GUM may use VFPU.
   Classification may nest its own saved scope, so elapsed clipping scope
   time includes that work and must not be added to the vertex timer. */
typedef struct {
#if CLIP_MATH_VFPU && !defined(JGR_STAGE_CLIP_SCOPE_REFERENCE)
    RenderMathState math;
#endif
    unsigned active,stamp,vectors,triangles;
} StageClipScope;
static inline void stage_clip_scope_end(StageClipScope *scope)
{
#ifndef JGR_STAGE_CLIP_SCOPE_REFERENCE
    if(!scope->active)return;
#if CLIP_MATH_VFPU
    render_math_end(&scope->math);
#endif
    vfpu_profile_clip_end(scope->stamp,scope->vectors);scope->active=0;
#else
    (void)scope;
#endif
}
static unsigned stage_clip_triangle(StageClipScope *scope,const JgsVert tri[3],
                                    const float planes[6][4],JgsVert out[27])
{
#ifdef JGR_STAGE_CLIP_SCOPE_REFERENCE
    (void)scope;return skyclip_triangle(tri,planes,out);
#else
    if(!scope->active) {
        scope->stamp=vfpu_profile_clip_begin();scope->vectors=scope->triangles=0;
#if CLIP_MATH_VFPU
        render_math_begin(&scope->math);
#endif
        scope->active=1;
    }
    unsigned emitted=skyclip_triangle_inner(tri,planes,out,&scope->vectors);
    if(++scope->triangles==64)stage_clip_scope_end(scope);
    return emitted;
#endif
}
static void draw_batch(Stage *s,const JgsBatch *batch,const float planes[6][4],const float guard[6][4],unsigned mask)
{
    CITY_SCOPE(s,CITY_GEOMETRY);
    const JgsVert *vertices=stage_batch_vertices(s,batch);
    const unsigned short *indices=stage_batch_indices(s,batch);
    if(!mask) {
        profile.fast_triangles+=batch->n_idx/3;
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS)
        if(command_cache_enabled && command_owner==s && (unsigned)(batch-s->batch)<command_count && batch->n_idx) {
            CITY_SCOPE(s,CITY_SUBMISSION);
            sceGumUpdateMatrix();ge_submit_call(command_packets[batch-s->batch]+16);
            s->drawn_tris+=batch->n_idx/3;s->drawn_batches++;profile.draw_calls++;profile.cached_calls++;return;
        }
#endif
        draw_indexed(s,vertices,indices,batch->n_idx);return;
    }
    ClipCache cache;memset(cache.index,0xff,sizeof cache.index);
    unsigned char codes[192];unsigned codes_start=0,codes_end=0;
    ClipMathPlanes packed;int vector_codes=!!(mask&(mask-1));
    if(vector_codes)clip_math_prepare(&packed,guard);
    /* Index 65535 cannot address a batch: n_vert itself is a ushort. */
    JgsVert clipped[192];unsigned used=0,start=0,inside=0;
    StageIndexRuns runs={0};
    StageClipScope clip_scope;clip_scope.active=0;
    const StageBatchBound *groups=NULL,*coarse=NULL;unsigned group_step=STAGE_CLIP_GROUP_INDICES,point_mask=mask;
    if(s->clip_groups && s->clip_groups[batch-s->batch]!=~0u) {
        unsigned entry=s->clip_groups[batch-s->batch],ref=reference_group_count(s,batch);
        if(refined_clipping && (entry&STAGE_CLIP_REFINED)) {
            groups=s->group_bounds+(entry&~STAGE_CLIP_REFINED)+ref;group_step=96;
            if(ref)coarse=s->group_bounds+(entry&~STAGE_CLIP_REFINED);
        } else if(ref)groups=s->group_bounds+(entry&~STAGE_CLIP_REFINED);
    }
    /* n_idx is 16-bit; the finest groups contain 96 indices. */
    signed char group_masks[(65535u+95u)/96u];unsigned group_index=0;
    if(groups && mesh_clipping)
        mesh_group_masks(guard,groups,coarse,(batch->n_idx+group_step-1)/group_step,mask,group_masks);
    unsigned next_group=0;
    for(unsigned j=0;j+2<batch->n_idx;j+=3) {
        if(groups && j==next_group) {
            unsigned end=j+group_step;if(end>batch->n_idx)end=batch->n_idx;
            next_group=end;profile.tested_groups++;
            int group_mask=mesh_clipping ? group_masks[group_index++]:batch_clip_mask(guard,groups);
            if(!mesh_clipping)profile.mesh_bound_tests++;
            groups++;
            point_mask=mask;
            if(refined_clipping && group_mask>=0) {
                point_mask=mask&(unsigned)group_mask;
                if(point_mask!=mask)profile.reduced_group_masks++;
                group_mask=(int)point_mask;
            }
            if(group_mask<=0) {
                if(!group_mask) {
                    if(used){stage_clip_scope_end(&clip_scope);draw_clipped(s,clipped,used);used=0;}
                    if(!inside)start=j;
                    inside+=end-j;profile.fast_triangles+=(end-j)/3;profile.accepted_groups++;
                } else {
                    if(inside){stage_clip_scope_end(&clip_scope);append_index_run(s,vertices,indices,&runs,start,inside);inside=0;}
                    profile.rejected_triangles+=(end-j)/3;profile.rejected_groups++;
                }
                j=end-3;continue;
            }
        }
        profile.tested_triangles++;
        unsigned a,b,c;
        if(vector_codes && (point_mask&(point_mask-1))) {
            if(j>=codes_end) {
                codes_start=j;codes_end=j+sizeof codes;
                if(codes_end>batch->n_idx)codes_end=batch->n_idx;
                /* Do not classify triangles a later group test may skip. */
                if(groups && codes_end>next_group)codes_end=next_group;
                vertex_codes(&cache,indices+j,codes_end-j,vertices,&packed,point_mask,codes);
            }
            a=codes[j-codes_start];b=codes[j-codes_start+1];c=codes[j-codes_start+2];
        } else {
            a=vertex_code(&cache,indices[j],vertices,guard,point_mask);
            b=vertex_code(&cache,indices[j+1],vertices,guard,point_mask);
            c=vertex_code(&cache,indices[j+2],vertices,guard,point_mask);
        }
        if(!(a|b|c)) {
            if(used){stage_clip_scope_end(&clip_scope);draw_clipped(s,clipped,used);used=0;}
            if(!inside)start=j;
            inside+=3;profile.fast_triangles++;continue;
        }
        if(inside){stage_clip_scope_end(&clip_scope);append_index_run(s,vertices,indices,&runs,start,inside);inside=0;}
        if(a&b&c){profile.rejected_triangles++;continue;}
        /* A clipped triangle produces at most 27 vertices. Keep bounded
           scratch and preserve primitive order, including alpha batches. */
        if(used+27>192){stage_clip_scope_end(&clip_scope);draw_clipped(s,clipped,used);used=0;}
        profile.clipped_triangles++; /* guard-crossing candidates, including early rejects */
        if(clip_reject_enabled) {
            profile.clip_reject_tests++;
            if(stage_triangle_trivially_outside(vertices+indices[j],vertices+indices[j+1],vertices+indices[j+2],planes)) {
                profile.clip_reject_hits++;continue;
            }
        }
        JgsVert tri[3]={vertices[indices[j]],vertices[indices[j+1]],vertices[indices[j+2]]};
        unsigned emitted=stage_clip_triangle(&clip_scope,tri,planes,clipped+used);
        if(emitted && runs.count){stage_clip_scope_end(&clip_scope);flush_index_runs(s,vertices,indices,&runs);}
        used+=emitted;
    }
    stage_clip_scope_end(&clip_scope);
    append_index_run(s,vertices,indices,&runs,start,inside);
    flush_index_runs(s,vertices,indices,&runs);
    draw_clipped(s,clipped,used);
}

/* Invariant within each stage draw: keep palette/pixel uploads per texture,
   but emit filtering/format/wrap commands only once for the draw invocation. */
static void stage_texture_state(void)
{
    CITY_SCOPE(NULL,CITY_MATERIAL);
    sceGuClutMode(GU_PSM_8888,0,0xff,0);
    sceGuTexMode(GU_PSM_T8,0,0,1);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexWrap(GU_REPEAT,GU_REPEAT);
    if(stage_mips.owner)sceGuTexLevelMode(GU_TEXTURE_AUTO,0.f);
}
static void bind_tex(Stage *s, int t)
{
    CITY_SCOPE(s,CITY_MATERIAL);
    JgsTex *tx = &s->tex[t];
    stage_mip_state(s,(unsigned)t);
    if(stage_water_texture(s,(unsigned)t)) {
        sceGuClutLoad(32,water_surface.data+64);
        sceGuTexImage(0,64,64,64,water_surface.data+1088+water_surface.frame*4096);
        s->texswitch++;return;
    }
    const unsigned char *cached=texture_owner==s && (unsigned)t<STAGE_TEXTURE_CACHE_SLOTS ? texture_pixels[t]:NULL;
    if(cached)texture_hits++;else texture_misses++;
    sceGuClutLoad(32, cached ? cached-1024:texture_address(s,tx,tx->off_clut));
    sceGuTexImage(0, tx->w, tx->h, tx->w, cached ? cached:texture_address(s,tx,tx->off_pix));
    s->texswitch++;
}

extern int stage_flat;
static int draw_cached_material(Stage *s,const JgsBatch *b,int mask,int cullmode,int *tex,int *two)
{
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS)
    unsigned index=(unsigned)(b-s->batch);
    if(command_cache_enabled<2 || mask || cullmode || stage_flat || command_owner!=s || index>=command_count || !b->n_idx || stage_water_texture(s,b->tex))return 0;
    /* Prefix selection and the cached material+geometry call form one
       submission operation. Splitting this tiny path doubled timer reads
       without separating the GPU work encoded by the resident packet. */
    CITY_SCOPE(s,CITY_SUBMISSION);
    int target=b->flags&JGS_B_NOTEX ? -2:b->tex;
    unsigned offset=16;
    if(*tex!=target || *two!=1) {
        /* A mip material cannot fit the fixed 16-word prefix. Bind it
           normally, then reuse the unchanged cached geometry packet. */
        if(target>=0 && stage_mip_slot(s,(unsigned)target)) {
            sceGuDisable(GU_CULL_FACE);sceGuEnable(GU_TEXTURE_2D);
            bind_tex(s,target);*tex=target;*two=1;
        } else {
            offset=0;
            if(target!=-2) {
                if(texture_owner==s && (unsigned)target<STAGE_TEXTURE_CACHE_SLOTS && texture_pixels[target])texture_hits++;else texture_misses++;
                s->texswitch++;
            }
            *tex=target;*two=1;
        }
    }
    /* The caller publishes the current model before these packets. None
       of the stage's texture/clip paths changes it between submissions. */
    {CITY_SCOPE(s,CITY_SUBMISSION);ge_submit_call(command_packets[index]+offset);}
    s->drawn_tris+=b->n_idx/3;s->drawn_batches++;
    profile.fast_triangles+=b->n_idx/3;profile.draw_calls++;profile.cached_calls++;profile.material_calls++;return 1;
#else
    (void)s;(void)b;(void)mask;(void)cullmode;(void)tex;(void)two;return 0;
#endif
}

int stage_flat;   /* debug: draw untextured */
/* Perform the same object and optional tighter-batch decisions in a CPU-only
   pass. This lets many sphere tests share VFPU planes/register preservation.
   Larger banks retain the scalar path beyond these bounded stack arrays. */
static void prepare_visibility(const Stage *s,const float visible[6][4],const float guard[6][4],
                               signed char *objects,signed char *batches)
{
    unsigned started=0,spheres=0;
#if defined(__PSP__) && JGR_DIAGNOSTICS
    started=sceKernelGetSystemTimeLow();
#endif
    unsigned count=s->h->n_obj<STAGE_VISIBILITY_CACHE ? s->h->n_obj:STAGE_VISIBILITY_CACHE;
    ClipMathPlanes p;RenderMathState state;render_math_begin(&state);
    clip_math_prepare(&p,visible);clip_math_load(&p);
    for(unsigned i=0;i<count;i++) {
        const JgsObj *o=s->obj+i;
        int sky=s->sky_explicit ? (o->flags&JGS_O_SKY):o->r>=1500.f;
        if(sky || !stage_render_object(s,o)){objects[i]=-1;continue;}
        objects[i]=clip_math_sphere_visible(&p,o,o->r) ? 0:-1;spheres++;
        if(objects[i]<0)continue;
        for(unsigned b=o->first_batch;b<o->first_batch+o->n_batch && b<STAGE_VISIBILITY_CACHE;b++) {
            batches[b]=0;
            if(s->batch_bounds && o->n_batch>1 && s->batch_bounds[b].r<o->r*.8f) {
                const StageBatchBound *q=s->batch_bounds+b;
                batches[b]=clip_math_sphere_visible(&p,q,q->r) ? 0:-1;spheres++;
            }
        }
    }
    clip_math_prepare(&p,guard);clip_math_load(&p);
    for(unsigned i=0;i<count;i++) {
        if(objects[i]<0)continue;
        const JgsObj *o=s->obj+i;
        objects[i]=(signed char)clip_math_sphere_mask(&p,o,o->r);spheres++;
        if(objects[i]<0)continue;
        for(unsigned b=o->first_batch;b<o->first_batch+o->n_batch && b<STAGE_VISIBILITY_CACHE;b++) {
            if(batches[b]<0)continue;
            batches[b]=objects[i];
            if(objects[i] && s->batch_bounds && o->n_batch>1 && s->batch_bounds[b].r<o->r*.8f) {
                const StageBatchBound *q=s->batch_bounds+b;
                batches[b]=(signed char)clip_math_sphere_mask(&p,q,q->r);spheres++;
            }
        }
    }
    render_math_end(&state);
#if defined(__PSP__) && JGR_DIAGNOSTICS
    started=sceKernelGetSystemTimeLow()-started;
#endif
    vfpu_profile_visibility(started,spheres);
}
/* The expensive object preparation is independent of opaque/alpha pass.
   Cache only CPU values; the GE always sees a freshly published model. */
static const float stage_identity[16] __attribute__((aligned(16)))={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static const StagePropPrepared *stage_prepare_prop_object(Stage *s,unsigned object,const float delta[16],
                                                        const float clip[16],StagePropPrepared *scratch)
{
    CITY_SCOPE(s,CITY_INSTANCE);
    StagePropDrawCache *cache=s->prop_draw_cache;StagePropPrepared *entry=scratch;
    if(cache) {
        if(!cache->clip_valid || memcmp(cache->clip,clip,64)) {
            memcpy(cache->clip,clip,64);cache->clip_valid=1;
            for(unsigned i=0;i<STAGE_PROP_DRAW_SLOTS;i++)cache->entry[i].valid=0;
        }
        entry=cache->entry+(object%STAGE_PROP_DRAW_SLOTS);
        if(entry->valid && entry->object==object && !memcmp(entry->delta,delta,64)) {
            profile.prop_prepare_hits++;return entry;
        }
    }
    profile.prop_prepared++;entry->valid=1;entry->object=object;memcpy(entry->delta,delta,64);
    const StageInstance *instance=stage_object_instance(s,object);
    float model[16],local_clip[16];
    if(instance) {
        for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++)
            model[r*4+c]=((delta[r*4]*instance->matrix[c]+delta[r*4+1]*instance->matrix[4+c])+delta[r*4+2]*instance->matrix[8+c])+(c==3 ? delta[r*4+3]:0.f);
    } else memcpy(model,delta,64);
    for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++) {
        float value=0;for(unsigned k=0;k<4;k++)value+=clip[r*4+k]*model[k*4+c];
        if(!isfinite(value) || !isfinite(model[r*4+c])){entry->mask=-1;return entry;}
        local_clip[r*4+c]=value;
        ((float *)&entry->model)[c*4+r]=model[r*4+c];
    }
    clip_planes(local_clip,entry->planes,entry->guard);
    const JgsObj *object_bound=s->obj+object;JgsObj local_bound={0};
    if(instance){memcpy(&local_bound.cx,&instance->bound.cx,16);object_bound=&local_bound;}
    entry->mask=sphere_clip_mask(entry->planes,object_bound)<0 ? -1:sphere_clip_mask(entry->guard,object_bound);
    return entry;
}
static void stage_draw_prepared_object(Stage *s,unsigned object,const StagePropPrepared *prepared,
                                      int pass,int cullmode,int standalone,int *texture,int *two_sided)
{
    const JgsObj *o=s->obj+object;int cur_tex=standalone ? -1:*texture,two=standalone ? -1:*two_sided;
    if(standalone) {
        CITY_SCOPE(s,CITY_MATERIAL);
        stage_texture_state();sceGuEnable(GU_DEPTH_TEST);sceGuDepthMask(GU_FALSE);
        if(!pass){sceGuDisable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);}
        else {sceGuEnable(GU_ALPHA_TEST);sceGuAlphaFunc(GU_GREATER,0x20,0xff);sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);}
    }
    if(standalone)sceGuEnable(GU_TEXTURE_2D);
    {CITY_SCOPE(s,CITY_SUBMISSION);
    if(standalone) {
        sceGumMatrixMode(GU_MODEL);sceGumPushMatrix();sceGumLoadMatrix(&prepared->model);
        /* Full cached packets assume the current model has been published. */
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS) || defined(JGR_TEST_GUM_MODEL)
        sceGumUpdateMatrix();
#endif
    } else {
        /* stage_draw owns a published, clean identity in GUM. Direct GE
           matrices avoid copying that identity through the GUM stack for
           every static instance; later GUM draws cannot overwrite them. */
        ge_submit_model(stage_object_instance(s,object)->matrix);
    }}
    unsigned before=s->drawn_batches;
    for(unsigned bi=o->first_batch;bi<o->first_batch+o->n_batch;bi++) {
        const JgsBatch *b=s->batch+bi;if(stage_batch_alpha(s,b)!=pass || !b->n_idx)continue;
        if(draw_cached_material(s,b,prepared->mask,cullmode,&cur_tex,&two))continue;
        {CITY_SCOPE(s,CITY_MATERIAL);
        if((b->flags&JGS_B_NOTEX) || stage_flat){if(cur_tex!=-2){sceGuDisable(GU_TEXTURE_2D);cur_tex=-2;}}
        else {if(cur_tex==-2)sceGuEnable(GU_TEXTURE_2D);if(cur_tex!=b->tex){bind_tex(s,b->tex);cur_tex=b->tex;}}
        int target=(b->flags&JGS_B_2SIDED) || cullmode==0;
        if(two!=target){if(target)sceGuDisable(GU_CULL_FACE);else sceGuEnable(GU_CULL_FACE);two=target;}
        }
        draw_batch(s,b,prepared->planes,prepared->guard,(unsigned)prepared->mask);
    }
    if(s->drawn_batches!=before){s->drawn_obj++;if(stage_object_instance(s,object))profile.prop_instance_draws++;}
    {CITY_SCOPE(s,CITY_SUBMISSION);
    if(standalone) {
        sceGumPopMatrix();
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS) || defined(JGR_TEST_GUM_MODEL)
        sceGumUpdateMatrix();
#endif
    } else {
        ge_submit_model(stage_identity);
        *texture=cur_tex;*two_sided=two;
    }}
    if(standalone){sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_BLEND);sceGuDisable(GU_TEXTURE_2D);sceGuEnable(GU_CULL_FACE);}
}
static void stage_draw_shared_object(Stage *s,unsigned object,const float clip[16],int mask,int pass,int cullmode,
                                     int *texture,int *two_sided)
{
    if(!(stage_object_pass_mask(s,object)&(1u<<pass))){profile.prop_empty_passes++;return;}
    StagePropPrepared scratch;const StagePropPrepared *prepared;
    if(!mask) {
        /* The world bound already proves every vertex lies inside the GE
           guard. No local planes or sphere test are needed. Keep this
           accepted object out of the clipping cache so it cannot
           evict a crossing object or be reused as prepared local planes. */
        scratch.mask=0;prepared=&scratch;
    } else prepared=stage_prepare_prop_object(s,object,stage_identity,clip,&scratch);
    if(prepared->mask>=0)stage_draw_prepared_object(s,object,prepared,pass,cullmode,0,texture,two_sided);
}
/* Keep original object order, including empty instance pass boundaries.
   Masks remain intact for batch rejection, actor queries and frame capture.
   Large/debug banks retain the full scan, including its uncached tail. */
static void stage_prepare_visible_list(const Stage *s)
{
    stage_prepared.visible_count=stage_prepared.visible_valid=0;
    if(!visible_list_enabled || s->h->n_obj>STAGE_VISIBILITY_CACHE)return;
    unsigned count=0;
    for(unsigned i=0;i<s->h->n_obj;i++)if(stage_prepared.objects[i]>=0) {
        if(count==STAGE_VISIBLE_OBJECTS)return;
        stage_prepared.visible_objects[count++]=(unsigned short)i;
    }
    stage_prepared.visible_count=count;stage_prepared.visible_valid=1;
    profile.city_list_objects+=count;
}
static const unsigned short *stage_visible_objects(const Stage *s,const signed char *visibility,unsigned *count)
{
    *count=s->h->n_obj;
    if(!visible_list_enabled || !stage_prepared.ready || !stage_prepared.visible_valid ||
       stage_prepared.owner!=s || stage_prepared.base!=s->base || visibility!=stage_prepared.objects)return NULL;
    *count=stage_prepared.visible_count;return stage_prepared.visible_objects;
}
#ifdef JGR_STAGE_GROUPING
#include "stage_order.inc"
static void draw_pass(Stage *s,const float visible[6][4],const float planes[6][4],const float guard[6][4],const float clip[16],int cullmode,int alpha_pass,signed char *visibility,const signed char *batch_visibility)
{
    StageOrder order={.tex=-1,.two=-1,.clip=clip};unsigned depth_bias=0;
    int grouping=texture_grouping_enabled && !alpha_pass && !stage_flat && s->batch_bounds;
    if(grouping) {
        order.radius_x=sqrtf(clip[0]*clip[0]+clip[1]*clip[1]+clip[2]*clip[2]);
        order.radius_y=sqrtf(clip[4]*clip[4]+clip[5]*clip[5]+clip[6]*clip[6]);
        order.radius_w=sqrtf(clip[12]*clip[12]+clip[13]*clip[13]+clip[14]*clip[14]);
    }
    /* Each pass has a fresh software texture cache, so give it a matching GU
       state too.  The opaque pass can finish on a JGS_B_NOTEX batch and leave
       GU_TEXTURE_2D disabled.  Without this reset the alpha pass still binds
       textures, but the GE draws their vertex colours only; which object is
       last changes with frustum culling, causing textured stairs to turn white
       when the camera moves by a few degrees. */
    sceGuEnable(GU_TEXTURE_2D);
    unsigned object_count;const unsigned short *objects=stage_visible_objects(s,visibility,&object_count);
    profile.city_object_visits+=object_count;
    for (unsigned at = 0; at < object_count; at++) {
        unsigned i=objects ? objects[at]:at;
        JgsObj *o = &s->obj[i];
        int mask;
        if(i<STAGE_VISIBILITY_CACHE)mask=visibility[i];
        else {
            /* City backgrounds use the original citysky banks; omit only
               baked duplicates, not large architecture. */
            int sky=s->sky_explicit ? (o->flags & JGS_O_SKY) : o->r>=1500.f;
            mask=sky || !stage_render_object(s,o) || sphere_clip_mask(visible,o)<0 ? -1:sphere_clip_mask(guard,o);
            if(i<STAGE_VISIBILITY_CACHE)visibility[i]=(signed char)mask;
        }
        if (mask<0) continue;
        if(o->flags&JGS_O_INSTANCE) {
            flush_order(s,&order,planes,guard,cullmode);stage_decal_end(depth_bias);depth_bias=0;
            stage_draw_shared_object(s,i,clip,mask,alpha_pass,cullmode,&order.tex,&order.two);continue;
        }
        if(visible_list_enabled && s->object_passes && !(s->object_passes[i]&(4u<<alpha_pass))) {
            profile.city_empty_passes++;continue;
        }
        int counted = 0;
        for (unsigned b = o->first_batch; b < o->first_batch + o->n_batch; b++) {
            JgsBatch *bt = &s->batch[b];
            if (stage_batch_alpha(s,bt) != alpha_pass) continue;
            if(i<STAGE_VISIBILITY_CACHE && b<STAGE_VISIBILITY_CACHE && batch_visibility[b]<0){profile.rejected_batches++;continue;}
            int batch_mask=mask;
            if(s->batch_bounds && o->n_batch>1 && s->batch_bounds[b].r<o->r*.8f) {
                profile.tested_batches++;
                if(i<STAGE_VISIBILITY_CACHE && b<STAGE_VISIBILITY_CACHE)batch_mask=batch_visibility[b];
                else {
                    if(batch_clip_mask(visible,s->batch_bounds+b)<0){profile.rejected_batches++;continue;}
                    if(mask)batch_mask=batch_clip_mask(guard,s->batch_bounds+b);
                }
                if(batch_mask<0){profile.rejected_batches++;continue;}
            }
            if (!counted) { s->drawn_obj++; counted = 1; }
            if(alpha_pass)stage_decal_apply(s,bt,&depth_bias);
            if(grouping && !batch_mask && !(bt->flags&JGS_B_NOTEX)) {
                const StageBatchBound *bound=s->batch_bounds+b;StageOrderEntry entry={.batch=bt,.bound=bound,.mask=0};
                if(isfinite(bound->cx) && isfinite(bound->cz) && fabsf(bound->cx)<1e8f && fabsf(bound->cz)<1e8f) {
                    int x=(int)floorf(bound->cx/512.f),z=(int)floorf(bound->cz/512.f);
                    if(order.count==STAGE_ORDER_WINDOW || (order.count && (order.cell_x!=x || order.cell_z!=z)))flush_order(s,&order,planes,guard,cullmode);
                    order.cell_x=x;order.cell_z=z;order.entry[order.count++]=entry;continue;
                }
            }
            if(!grouping && draw_cached_material(s,bt,batch_mask,cullmode,&order.tex,&order.two))continue;
            flush_order(s,&order,planes,guard,cullmode);
            ordered_draw(s,&order,bt,(unsigned)batch_mask,planes,guard,cullmode);
        }
    }
    flush_order(s,&order,planes,guard,cullmode);
    stage_decal_end(depth_bias);
}

#else
static void draw_pass(Stage *s,const float visible[6][4],const float planes[6][4],const float guard[6][4],const float clip[16],int cullmode,int alpha_pass,signed char *visibility,const signed char *batch_visibility)
{
    (void)clip;int cur_tex = -1, cur_2s = -1;unsigned depth_bias=0;
    /* Each pass has a fresh software texture cache, so give it a matching GU
       state too.  The opaque pass can finish on a JGS_B_NOTEX batch and leave
       GU_TEXTURE_2D disabled.  Without this reset the alpha pass still binds
       textures, but the GE draws their vertex colours only; which object is
       last changes with frustum culling, causing textured stairs to turn white
       when the camera moves by a few degrees. */
    sceGuEnable(GU_TEXTURE_2D);
    unsigned object_count;const unsigned short *objects=stage_visible_objects(s,visibility,&object_count);
    profile.city_object_visits+=object_count;
    for (unsigned at = 0; at < object_count; at++) {
        unsigned i=objects ? objects[at]:at;
        JgsObj *o = &s->obj[i];
        int mask;
        if(i<STAGE_VISIBILITY_CACHE)mask=visibility[i];
        else {
            /* City backgrounds use the original citysky banks; omit only
               baked duplicates, not large architecture. */
            int sky=s->sky_explicit ? (o->flags & JGS_O_SKY) : o->r>=1500.f;
            mask=sky || !stage_render_object(s,o) || sphere_clip_mask(visible,o)<0 ? -1:sphere_clip_mask(guard,o);
            if(i<STAGE_VISIBILITY_CACHE)visibility[i]=(signed char)mask;
        }
        if (mask<0) continue;
        if(o->flags&JGS_O_INSTANCE) {
            stage_decal_end(depth_bias);depth_bias=0;
            stage_draw_shared_object(s,i,clip,mask,alpha_pass,cullmode,&cur_tex,&cur_2s);continue;
        }
        if(visible_list_enabled && s->object_passes && !(s->object_passes[i]&(4u<<alpha_pass))) {
            profile.city_empty_passes++;continue;
        }
        int counted = 0;
        for (unsigned b = o->first_batch; b < o->first_batch + o->n_batch; b++) {
            JgsBatch *bt = &s->batch[b];
            if (stage_batch_alpha(s,bt) != alpha_pass) continue;
            if(i<STAGE_VISIBILITY_CACHE && b<STAGE_VISIBILITY_CACHE && batch_visibility[b]<0){profile.rejected_batches++;continue;}
            int batch_mask=mask;
            if(s->batch_bounds && o->n_batch>1 && s->batch_bounds[b].r<o->r*.8f) {
                profile.tested_batches++;
                if(i<STAGE_VISIBILITY_CACHE && b<STAGE_VISIBILITY_CACHE)batch_mask=batch_visibility[b];
                else {
                    if(batch_clip_mask(visible,s->batch_bounds+b)<0){profile.rejected_batches++;continue;}
                    if(mask)batch_mask=batch_clip_mask(guard,s->batch_bounds+b);
                }
                if(batch_mask<0){profile.rejected_batches++;continue;}
            }
            if (!counted) { s->drawn_obj++; counted = 1; }
            if(alpha_pass)stage_decal_apply(s,bt,&depth_bias);
            if(draw_cached_material(s,bt,batch_mask,cullmode,&cur_tex,&cur_2s))continue;
            {CITY_SCOPE(s,CITY_MATERIAL);
            if ((bt->flags & JGS_B_NOTEX) || stage_flat) { if (cur_tex != -2) { sceGuDisable(GU_TEXTURE_2D); cur_tex = -2; } }
            else { if (cur_tex == -2) sceGuEnable(GU_TEXTURE_2D); if (bt->tex != cur_tex) { bind_tex(s, bt->tex); cur_tex = bt->tex; } }
            int two = (bt->flags & JGS_B_2SIDED) || cullmode == 0;
            if (two != cur_2s) {
                if (two) sceGuDisable(GU_CULL_FACE); else sceGuEnable(GU_CULL_FACE);
                cur_2s = two;
            }
            }
            draw_batch(s,bt,planes,guard,(unsigned)batch_mask);
        }
    }
    stage_decal_end(depth_bias);
}

#endif

/* Menu banks retain the direct path. Placed world props need the same
   near/guard clipping as city geometry when the camera reaches their bed,
   rails or walls. A world-space sphere test alone cannot protect the GE. */
static void draw_object(Stage *s, int idx, const float *clip,int uvtex,float u,float v,int selected_pass)
{
    if (!s->base || idx < 0 || idx >= (int)s->h->n_obj) return;
    float planes[6][4],guard[6][4];int mask=0;
    if(clip){
        clip_planes(clip,planes,guard);
        if(sphere_clip_mask(planes,s->obj+idx)<0)return;
        mask=sphere_clip_mask(guard,s->obj+idx);if(mask<0)return;
    }
    JgsObj *o = &s->obj[idx]; int cur_tex = -1, textured = 1;stage_texture_state();
    sceGuDisable(GU_CULL_FACE); sceGuEnable(GU_TEXTURE_2D); sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);
    for (int pass = 0; pass < 2; pass++) {
        if(selected_pass>=0 && pass!=selected_pass)continue;
        /* A pass can end on an untextured Ninja strip.  Reset both the
           software cache and the GE state before the next pass, just as the
           main stage renderer does. */
        cur_tex = -1; textured = 1; sceGuEnable(GU_TEXTURE_2D);
        if (pass == 0) { sceGuDisable(GU_BLEND); sceGuDisable(GU_ALPHA_TEST); sceGuDepthMask(GU_FALSE); }
        else { sceGuEnable(GU_ALPHA_TEST); sceGuAlphaFunc(GU_GREATER, 0x20, 0xff); sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); }
        for (unsigned b = o->first_batch; b < o->first_batch + o->n_batch; b++) {
            JgsBatch *bt = &s->batch[b];
            if (stage_batch_alpha(s,bt) != pass) continue;
            if (bt->flags & JGS_B_NOTEX) { if (textured) { sceGuDisable(GU_TEXTURE_2D); textured = 0; } }
            else { if (!textured) { sceGuEnable(GU_TEXTURE_2D); textured = 1; } if (bt->tex != cur_tex) { bind_tex(s, bt->tex); cur_tex = bt->tex; } }
            if(uvtex>=0)sceGuTexOffset(bt->tex==uvtex ? u:0.f,bt->tex==uvtex ? v:0.f);
            if(clip)draw_batch(s,bt,planes,guard,(unsigned)mask);
            else ge_submit_gum(GU_TRIANGLES, VFMT, bt->n_idx, stage_batch_indices(s,bt), stage_batch_vertices(s,bt));
        }
    }
    if(uvtex>=0)sceGuTexOffset(0.f,0.f);
    sceGuDisable(GU_ALPHA_TEST); sceGuDisable(GU_BLEND); sceGuDisable(GU_TEXTURE_2D); sceGuEnable(GU_CULL_FACE);
}
void stage_draw_obj(Stage *s,int idx){draw_object(s,idx,NULL,-1,0,0,-1);}
void stage_draw_obj_clipped(Stage *s,int idx,const float clip[16]){draw_object(s,idx,clip,-1,0,0,-1);}
void stage_draw_obj_uv_clipped(Stage *s,int idx,const float clip[16],int tex,float u,float v)
{draw_object(s,idx,clip,tex,u,v,-1);}

int stage_prop_visible(Stage *s,int handle,const float position[3],const float eye[3],
                       const float forward[3],float tangent)
{
    if(!stage_prop_record(s,handle) || !position || !eye || !forward)return 0;
    for(unsigned k=0;k<3;k++)if(!isfinite(position[k]))return 0;
    StagePropDrawCache *cache=s->prop_draw_cache;ObjectView scratch,*view=&scratch;
    if(cache) {
        float key[7];memcpy(key,eye,12);memcpy(key+3,forward,12);key[6]=tangent;
        if(!cache->view_valid || memcmp(key,cache->view_key,sizeof key)) {
            memcpy(cache->view_key,key,sizeof key);cache->view_valid=1;
            object_view_setup(&cache->view,source_render.h->stage,eye,forward,tangent,1.f);
        }
        view=&cache->view;
    } else object_view_setup(view,source_render.h->stage,eye,forward,tangent,1.f);
    return object_pickup_visible(view,position[0],position[1],position[2]);
}
static int stage_draw_prop_range(Stage *s,int handle,unsigned part,const float delta[16],const float position[3],
                                const float eye[3],const float forward[3],float tangent,const float clip[16],int pass)
{
    const StageRenderRecord *r=stage_prop_record(s,handle);
    if(!r || !source_render.prop_hidden[handle-1] || !delta || !position || !eye || !forward || !clip || (unsigned)pass>1)return 0;
    if(part!=~0u && part>=r->near_count)return 0;
    for(unsigned k=0;k<16;k++)if(!isfinite(delta[k]) || !isfinite(clip[k]))return 0;
    if(!stage_prop_visible(s,handle,position,eye,forward,tangent))return 0;
    float dx=position[0]-eye[0],dy=position[1]-eye[1],dz=position[2]-eye[2];
    int far=source_lod && r->lod2>0.f && (dx*dx+dy*dy)+dz*dz>=r->lod2;
    /* Kogane's animated sign has a single rigid far root, with no child.
       Its root pass owns that far mesh; the child pass must emit nothing. */
    if(far && part!=~0u && part!=0)return 0;
    unsigned first=far ? r->far_first:r->near_first+(part==~0u ? 0:part);
    unsigned count=far ? r->far_count:part==~0u ? r->near_count:1;
    unsigned submitted=s->drawn_batches;
    for(unsigned i=first;i<first+count;i++) {
        if(!(stage_object_pass_mask(s,i)&(1u<<pass))){profile.prop_empty_passes++;continue;}
        StagePropPrepared scratch;const StagePropPrepared *prepared=stage_prepare_prop_object(s,i,delta,clip,&scratch);
        if(prepared->mask>=0)stage_draw_prepared_object(s,i,prepared,pass,0,1,NULL,NULL);
    }
    return (int)(s->drawn_batches-submitted);
}
int stage_draw_prop(Stage *s,int handle,const float delta[16],const float position[3],
                    const float eye[3],const float forward[3],float tangent,const float clip[16],int pass)
{return stage_draw_prop_range(s,handle,~0u,delta,position,eye,forward,tangent,clip,pass);}
int stage_draw_prop_part(Stage *s,int handle,unsigned part,const float delta[16],const float position[3],
                         const float eye[3],const float forward[3],float tangent,const float clip[16],int pass)
{return stage_draw_prop_range(s,handle,part,delta,position,eye,forward,tangent,clip,pass);}

static int stage_prepared_matches(const Stage *s,const float visible[6][4],const float clip[16],int cullmode)
{
    return s && s->base && visible && clip && stage_prepared.ready && stage_prepared.pending &&
        stage_prepared.owner==s && stage_prepared.base==s->base && stage_prepared.cullmode==cullmode &&
        !memcmp(stage_prepared.visible,visible,sizeof stage_prepared.visible) &&
        !memcmp(stage_prepared.clip,clip,sizeof stage_prepared.clip);
}
void stage_prepare_draw(Stage *s,const float visible[6][4],const float clip[16],int cullmode)
{
    if(stage_prepared_matches(s,visible,clip,cullmode))return;
    stage_capture_forget();
    stage_prepared_forget();
    if(!s || !s->base || !s->h || !visible || !clip)return;
    stage_prepared.owner=s;stage_prepared.base=s->base;stage_prepared.cullmode=cullmode;
    stage_prepared.previous_special=source_render.previous_special;
    stage_prepared.pending=stage_prepared.building=1;
    memcpy(stage_prepared.visible,visible,sizeof stage_prepared.visible);
    memcpy(stage_prepared.clip,clip,sizeof stage_prepared.clip);
    clip_planes(clip,stage_prepared.planes,stage_prepared.guard);
    {CITY_SCOPE(s,CITY_SOURCE);stage_render_prepare(s);}
    /* Unused and source-rejected entries are explicitly invalid, including
       batches beyond a culled object. A published blocker must be submitted. */
    memset(stage_prepared.objects,-1,sizeof stage_prepared.objects);
    memset(stage_prepared.batches,-1,sizeof stage_prepared.batches);
    {CITY_SCOPE(s,CITY_FRUSTUM);prepare_visibility(s,visible,stage_prepared.guard,stage_prepared.objects,stage_prepared.batches);}
    if(cullmode==0){CITY_SCOPE(s,CITY_OCCLUSION);stage_visibility_apply_detail(s,visible,clip,stage_prepared.objects,STAGE_VISIBILITY_CACHE,
        stage_prepared.batches,STAGE_VISIBILITY_CACHE);}
    {CITY_SCOPE(s,CITY_TRAVERSAL);stage_prepare_visible_list(s);}
    stage_prepared.building=0;stage_prepared.ready=1;
}
void stage_draw(Stage *s,const float visible[6][4],const float clip[16],int cullmode)
{
    stage_prepare_draw(s,visible,clip,cullmode);
    if(!stage_prepared_matches(s,visible,clip,cullmode))return;
    /* Consume once: source_render_prepare's transition coverage belongs to
       this draw, while certified actor cones stay valid until frame end. */
    stage_prepared.pending=0;
    signed char *visibility=stage_prepared.objects,*batch_visibility=stage_prepared.batches;
    const float (*planes)[4]=stage_prepared.planes,(*guard)[4]=stage_prepared.guard;
    fragment_index_bytes=0;
    stage_texture_state();
    s->drawn_obj = s->drawn_tris = s->drawn_batches = s->texswitch = 0;
    if(city_owner==s)city_draw_completed=1;
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();
#if defined(__PSP__) || defined(JGR_TEST_STAGE_COMMANDS) || defined(JGR_TEST_GUM_MODEL)
    sceGumUpdateMatrix();
#endif
    sceGuEnable(GU_TEXTURE_2D);
    sceGuFrontFace(cullmode == 2 ? GU_CCW : GU_CW);
    sceGuEnable(GU_DEPTH_TEST); sceGuDepthMask(GU_FALSE);
    /* opaque */
    sceGuDisable(GU_BLEND); sceGuDisable(GU_ALPHA_TEST);
    draw_pass(s,visible,planes,guard,clip,cullmode,0,visibility,batch_visibility);
    /* alpha: punch-through test + blend, no depth write */
    sceGuEnable(GU_ALPHA_TEST); sceGuAlphaFunc(GU_GREATER, 0x20, 0xff);
    sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    draw_pass(s,visible,planes,guard,clip,cullmode,1,visibility,batch_visibility);
    sceGuDisable(GU_ALPHA_TEST); sceGuDisable(GU_BLEND);
    sceGuEnable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
}

void stage_free(Stage *s) {gfx_wait_idle();forget_textures(s);release_stage_data(s);}

#include "stage_frame_dump.inc"

void stage_draw_sky(Stage *s)
{
    if (!s->base) return;
    int cur_tex = -1;
    stage_texture_state();
    sceGuDisable(GU_CULL_FACE); sceGuEnable(GU_TEXTURE_2D); sceGuDisable(GU_DEPTH_TEST); sceGuDepthMask(GU_TRUE);
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0) { sceGuDisable(GU_BLEND); sceGuDisable(GU_ALPHA_TEST); }
        else { sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); }
        for (unsigned i = 0; i < s->h->n_obj; i++) {
            JgsObj *o = &s->obj[i]; if (o->r < 1500.f) continue;
            for (unsigned b = o->first_batch; b < o->first_batch + o->n_batch; b++) {
                JgsBatch *bt = &s->batch[b];
                if (stage_batch_alpha(s,bt) != pass) continue;
                if (bt->flags & JGS_B_FALLBACK) continue;   /* unresolved sky slot: leave the stage back colour instead */
                if (bt->tex != cur_tex) { bind_tex(s, bt->tex); cur_tex = bt->tex; }
                ge_submit_gum(GU_TRIANGLES, VFMT, bt->n_idx, stage_batch_indices(s,bt), stage_batch_vertices(s,bt));
            }
        }
    }
    sceGuDisable(GU_BLEND); sceGuEnable(GU_DEPTH_TEST); sceGuDepthMask(GU_FALSE); sceGuEnable(GU_CULL_FACE);
}
