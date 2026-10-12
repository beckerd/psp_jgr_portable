#include "diagnostics.h"
#include "gfx.h"
#include <pspkernel.h>
#include <pspgu.h>
#include <pspgum.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "charanim.h"
#include "asset_progress.h"
#ifdef JGR_CHAR_ALLOC_LIMIT
static int char_limit_active;
static void *char_limited_alloc(size_t alignment,size_t bytes)
{return char_limit_active && bytes>JGR_CHAR_ALLOC_LIMIT ? NULL:memalign(alignment,bytes);}
#define memalign char_limited_alloc
#endif
static float hud_reciprocal_scale=1.f,hud_depth_shift;
static int fade_active;
static unsigned fade_colour;
void chars_hud_depth(float scale,float shift){hud_reciprocal_scale=scale;hud_depth_shift=shift;}
#include "charpose.h"
#include "source_light.h"
#include "skyclip.h"
#include "ge_submit.h"
static const float (*background_planes)[4];
static const float (*material_planes)[4];
static const float (*batch_planes)[4];
void chars_batch_frustum(const float planes[6][4]){batch_planes=planes;}
static const Chars *final_floor_bank;
static float final_floor_view[6][4],final_floor_guard[6][4];
static ClipMathPlanes final_floor_packed;
static unsigned char final_floor_codes[CHAR_CPU_SKIN_VERTICES];
static CharFinalClipProfile final_clip_profile;
#ifdef __PSP__
typedef char final_clip_budget[(sizeof final_floor_bank+sizeof final_floor_view+
    sizeof final_floor_guard+sizeof final_floor_packed+sizeof final_floor_codes+
    sizeof final_clip_profile<=CHAR_FINAL_CLIP_BYTES) ? 1:-1];
#endif
void chars_final_clip_profile(CharFinalClipProfile *out)
{*out=final_clip_profile;memset(&final_clip_profile,0,sizeof final_clip_profile);}
void chars_final_floor_clip(const Chars *bank,const float clip[16])
{
    final_floor_bank=NULL;
    if(!bank || !clip)return;
    for(unsigned i=0;i<16;i++)if(!isfinite(clip[i]))return;
    /* Same NDC +/-4 guard as the static city. Keep actual near/far
       planes, and clip unsafe triangles back to the actual view. */
    for(unsigned p=0;p<6;p++) {
        float vn=0.f,gn=0.f,scale=p<4 ? .25f:1.f,sign=(p&1) ? -1.f:1.f;
        for(unsigned j=0;j<4;j++) {
            float v=clip[12+j]+sign*clip[(p/2)*4+j];
            float g=clip[12+j]+sign*scale*clip[(p/2)*4+j];
            final_floor_view[p][j]=v;final_floor_guard[p][j]=g;
            if(j<3){vn+=v*v;gn+=g*g;}
        }
        if(!(vn>0.f) || !(gn>0.f) || !isfinite(vn) || !isfinite(gn))return;
        vn=sqrtf(vn);gn=sqrtf(gn);
        for(unsigned j=0;j<4;j++){final_floor_view[p][j]/=vn;final_floor_guard[p][j]/=gn;}
    }
    clip_math_prepare(&final_floor_packed,final_floor_guard);final_floor_bank=bank;
}
static const float *material_sort_forward;
void chars_material_depth_sort(const float forward[3]){material_sort_forward=forward;}
static int material_selective;
static unsigned material_clip_mask=63;
#ifndef CHAR_GE_SKINNING
#ifdef __PSP__
#define CHAR_GE_SKINNING 1
#else
#define CHAR_GE_SKINNING 0
#endif
#endif
#ifndef JGR_FINAL_VERTEX_ADMISSION_RETRY
#ifdef __PSP__
#define JGR_FINAL_VERTEX_ADMISSION_RETRY 1
#else
#define JGR_FINAL_VERTEX_ADMISSION_RETRY 0
#endif
#endif
#if CHAR_GE_SKINNING && JGR_FINAL_VERTEX_ADMISSION_RETRY
static int final_vertex_admission_retry_enabled=1;
void chars_final_admission_retry_mode(int enabled){final_vertex_admission_retry_enabled=!!enabled;}
#else
void chars_final_admission_retry_mode(int enabled){(void)enabled;}
#endif
static unsigned ge_skin_batches,ge_skin_vertices,cpu_skin_batches,cpu_skin_vertices;
/* Cumulative unweighted submissions augment caller-scoped actor work;
   the legacy GE-vs-CPU skin profile keeps its original categorization. */
static unsigned ped_shared_skin_draws,ped_shared_skin_vertices;
typedef struct {
    const Chars *bank;
    const JgcBatch *batch;
    void *vertices;
    unsigned short bones[8],nbones;
    unsigned char count,flags;
    unsigned short page,pages; /* zero pages: transient live-list storage */
    const unsigned short *indices;
    unsigned end; /* exclusive source batch index; zero for a single batch */
    unsigned short nvert,nidx;
    unsigned short source_nvert,source_nidx;
} GePacket;
enum { GE_PACKET_PED=1,GE_PACKET_STRIP=2,GE_PACKET_NATIVE=4,GE_PACKET_FINAL=8,GE_PACKET_FINAL_UNLIT=16,GE_PACKET_FINAL_NORMAL=32,GE_PACKET_FINAL_CEL=64 };
static CharFinalVertexProfile final_vertex_profile;
static int final_vertex_enabled=1;
void chars_final_vertex_mode(int enabled){final_vertex_enabled=!!enabled;}
void chars_final_vertex_profile(CharFinalVertexProfile *out)
{*out=final_vertex_profile;memset(&final_vertex_profile,0,sizeof final_vertex_profile);}
#ifdef __PSP__
/* The original packet was 36 bytes; group metadata has a fixed list offset. */
typedef char group_metadata_budget[(sizeof(GePacket)<=52) ? 1:-1];
#endif
static GePacket ge_packets[512];
static int ge_packets_active;
#if CHAR_GE_SKINNING
static unsigned char ge_packet_memory[CHAR_GE_PACKET_BYTES] __attribute__((aligned(64)));
#define GE_PACKET_PAGES (CHAR_GE_PACKET_BYTES/256)
/* Independent pins preserve every submitted address even if its hash-table
   entry is replaced by a collision later in the same asynchronous list. */
static unsigned char ge_packet_pinned[GE_PACKET_PAGES];
/* Page -> hash slot + 1. Old tags may survive a hash collision; validate
   the slot's CURRENT range before evicting it. This avoids scanning every
   cache entry whenever a small actor packet needs a few pages. */
static unsigned short ge_packet_owner[GE_PACKET_PAGES];
static unsigned ge_packet_cursor;
#define FINAL_VERTEX_SEARCH_STEPS (GE_PACKET_PAGES*4u)
static unsigned final_vertex_search_budget;
static GePacket *packet_reclaim_cpu(GePacket *p)
{
    if(p->bank && (p->flags&GE_PACKET_FINAL)) {
        if(p->count)final_vertex_profile.evictions++;
        p->bank=NULL;
    }
    return p;
}
/* Final's CPU results and GE packets share metadata as well as pages.
   Allocation addresses changed their collision pattern after unrelated
   structure edits. Keep adjacent batch IDs distributed deterministically;
   full bank/batch/context comparisons still decide every cache hit. */
static unsigned final_packet_hash(const Chars *c,const JgcBatch *b)
{return (unsigned)(b-c->batch)*73u+c->h->n_batches*19u+c->h->n_bones*37u;}
/* Prepared Final banks use 64 sets of eight ways within the same 512
   entries. Other callers retain their existing four-way layout. */
static GePacket *packet_slot(const Chars *c,const JgcBatch *b,unsigned nbones,int grouped,int compact)
{
    if(!ge_packets_active)return NULL;
    size_t key;unsigned ways;
    if(c->indexed_prepare){key=final_packet_hash(c,b);ways=8;}
    else {key=((size_t)c>>4)^((size_t)b>>4);key^=key>>9;key^=grouped ? 0x6d:0;key^=compact ? 0x39:0;ways=4;}
    GePacket *set=ge_packets+((key&(512u/ways-1u))*ways),*victim=NULL;
    for(unsigned i=0;i<ways;i++) {
        GePacket *p=set+i;
        if(!(p->flags&GE_PACKET_FINAL) && p->bank==c && p->batch==b && p->nbones==nbones && !!p->end==grouped && !!(p->flags&GE_PACKET_PED)==compact)return p;
        if(!p->bank)victim=p;
    }
    if(victim)return victim;
    for(unsigned i=0;i<ways;i++)if(!set[i].pages || !ge_packet_pinned[set[i].page])return packet_reclaim_cpu(set+i);
    /* Even if all ways are live, independent page pins protect their data. */
    return packet_reclaim_cpu(set);
}
static void *packet_memory(unsigned bytes,unsigned *page,unsigned *pages,GePacket *owner)
{
    *pages=0;
    unsigned needed=(bytes+255)/256;
    if(!ge_packets_active || needed>GE_PACKET_PAGES)return NULL;
    for(int pass=0;pass<2;pass++) {
        unsigned run=0;
        for(unsigned i=pass ? 0:ge_packet_cursor;i<GE_PACKET_PAGES;i++) {
            run=ge_packet_pinned[i] ? 0:run+1;
            if(run<needed)continue;
            unsigned first=i+1-needed;
            for(unsigned j=first;j<=i;j++) {
                unsigned tag=ge_packet_owner[j];
                if(tag) {
                    GePacket *p=ge_packets+tag-1;
                    if(p->bank && p->pages && p->page<=j && j<p->page+p->pages) {
                        if((p->flags&GE_PACKET_FINAL) && p->count)final_vertex_profile.evictions++;
                        p->bank=NULL;
                    }
                }
                ge_packet_owner[j]=(unsigned short)(owner-ge_packets+1);
            }
            memset(ge_packet_pinned+first,1,needed);
            ge_packet_cursor=(i+1)%GE_PACKET_PAGES;
            *page=first;*pages=needed;return ge_packet_memory+first*256;
        }
    }
    return NULL; /* All available ranges are still owned by this frame. */
}
#endif
static void final_indexed_reset(void);
static void chars_final_ge_normal_frame_begin(void);
static void final_cel_packet_frame_begin(void);
static void forget_preparation(const Chars *c);
static void preparation_begin(void);
static void forget_draw_plans(const Chars *c);
static void forget_arrows(const Chars *c);
static void forget_material_cache(const Chars *c);
static void forget_ped_shared(const Chars *c);
static void load_ped_native(Chars *c,const char *path);
static void forget_packets(const Chars *c)
{
    forget_preparation(c);forget_arrows(c);forget_draw_plans(c);forget_material_cache(c);forget_ped_shared(c);
    for(unsigned i=0;i<512;i++)if(ge_packets[i].bank==c)ge_packets[i].bank=NULL;
}
static const float (*world_planes)[4];
static unsigned packet_prepared,packet_reused,packet_vertices,actors_culled;
static unsigned pose_us,packet_us,cpu_skin_us,bounds_us,posed_bones;
static unsigned posed_total_us,ge_skin_us,draw_skin_us; /* garage/actor draw phases */
static unsigned cel_cut_triangles,cel_batches;
static unsigned cel_mixed_batches,cel_mixed_saved_draws,cel_mixed_extra_bytes,cel_mixed_frame_bytes;
static unsigned cel_shared_cut_triangles;
/* Detailed actor clocks are diagnostic. Keep integer work counters and
   frame/section timing available without a timer syscall at every stage. */
#ifndef JGR_ACTOR_TIMING_FORCE
#define JGR_ACTOR_TIMING_FORCE 0
#endif
#if JGR_ACTOR_TIMING_FORCE
static unsigned final_cache_lookup_us;
#endif
unsigned chars_final_lookup_profile(void)
{
#if JGR_ACTOR_TIMING_FORCE
    unsigned value=final_cache_lookup_us;final_cache_lookup_us=0;return value;
#else
    return 0;
#endif
}
static int actor_timing_enabled;
void chars_actor_timing_mode(int enabled){actor_timing_enabled=JGR_DIAGNOSTICS && !!(enabled||JGR_ACTOR_TIMING_FORCE);}
int chars_actor_timing_enabled(void){return actor_timing_enabled;}
static unsigned render_clock(void)
{
#if defined(__PSP__) && JGR_DIAGNOSTICS
    return actor_timing_enabled ? sceKernelGetSystemTimeLow():0;
#else
    return 0;
#endif
}
#include "char_draw_plan.inc"
static int ped_packet_enabled=1,ped_packet_scope;
static CharPedOcclusionQuery ped_occlusion_query;
static void *ped_occlusion_context;
void chars_ped_occlusion_query(CharPedOcclusionQuery query,void *context)
{ped_occlusion_query=query;ped_occlusion_context=query ? context:NULL;}
static int ped_transform_mode,ped_transform_active;
static const float ped_identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void chars_ped_transform_mode(int mode){ped_transform_mode=mode>=0 && mode<=2 ? mode:0;}
static CharPedPacketProfile ped_packet_window,ped_packet_total;
void chars_ped_packet_mode(int enabled){ped_packet_enabled=!!enabled;}
void chars_ped_packet_totals(CharPedPacketProfile *out){*out=ped_packet_total;}
void chars_ped_packet_profile(CharPedPacketProfile *out)
{*out=ped_packet_window;ped_packet_window=(CharPedPacketProfile){0};}
#define PED_PACKET_ADD(field,n) do {unsigned amount=(n);ped_packet_window.field+=amount;ped_packet_total.field+=amount;} while(0)
#include "char_lod.inc"
void chars_cpu_profile(unsigned out[5])
{
    out[0]=pose_us;out[1]=packet_us;out[2]=cpu_skin_us;out[3]=bounds_us;out[4]=posed_bones;
    pose_us=packet_us=cpu_skin_us=bounds_us=posed_bones=0;
}
void chars_draw_phase_profile(unsigned out[3])
{out[0]=posed_total_us;out[1]=ge_skin_us;out[2]=draw_skin_us;posed_total_us=ge_skin_us=draw_skin_us=0;}
void chars_cel_profile(unsigned out[2])
{out[0]=cel_cut_triangles;out[1]=cel_batches;cel_cut_triangles=cel_batches=0;}
void chars_cel_mixed_profile(unsigned out[3])
{out[0]=cel_mixed_batches;out[1]=cel_mixed_saved_draws;out[2]=cel_mixed_extra_bytes;
 cel_mixed_batches=cel_mixed_saved_draws=cel_mixed_extra_bytes=0;}
void chars_cel_shared_profile(unsigned out[3])
{out[0]=cel_shared_cut_triangles;out[1]=cel_shared_cut_triangles*5u;out[2]=cel_shared_cut_triangles*2u;
 cel_shared_cut_triangles=0;}
void chars_frame_begin(void)
{
    chars_final_ge_normal_frame_begin();
    final_cel_packet_frame_begin();
    chars_final_floor_clip(NULL,NULL);
    cel_mixed_frame_bytes=0;final_indexed_reset();
    chars_ped_occlusion_query(NULL,NULL);
    chars_material_cache_scope(NULL,0);
    preparation_begin();chars_pose_frame_begin();
    /* Final's zero-page entries remember immutable no-normal classification;
       unlike transient GE vertices they contain no frame-owned pointer. */
    for(unsigned i=0;i<512;i++)if(!ge_packets[i].pages && !(ge_packets[i].flags&GE_PACKET_FINAL))ge_packets[i].bank=NULL;
#if CHAR_GE_SKINNING
    memset(ge_packet_pinned,0,sizeof ge_packet_pinned);
    final_vertex_search_budget=FINAL_VERTEX_SEARCH_STEPS;
#endif
    ge_packets_active=1;world_planes=NULL;lod_level=0;
}
void chars_frame_end(void){chars_pose_join();ge_packets_active=0;world_planes=NULL;}
void chars_world_frustum(const float planes[6][4]){world_planes=planes;}
void chars_render_profile(unsigned out[4])
{
    out[0]=packet_prepared;out[1]=packet_reused;out[2]=packet_vertices;out[3]=actors_culled;
    packet_prepared=packet_reused=packet_vertices=actors_culled=0;
}
void chars_skin_profile(unsigned out[4])
{
    out[0]=ge_skin_batches;out[1]=ge_skin_vertices;
    out[2]=cpu_skin_batches;out[3]=cpu_skin_vertices;
    ge_skin_batches=ge_skin_vertices=cpu_skin_batches=cpu_skin_vertices=0;
}

void chars_free_lighting(Chars *c)
{free(c->lighting_image);c->lighting_image=NULL;c->lighting=NULL;}
int chars_load_lighting(Chars *c,const char *path)
{
    if(!c->h || c->geometry || c->vert_stride!=28 || c->h->n_verts>100000)return -1;
    FILE *f=fopen(path,"rb");if(!f)return -2;
    long length=0;unsigned char *data=NULL;const PlayerLightVertex *v=NULL;
    if(c->lighting_source) {
        unsigned char header[20];unsigned bytes=20+c->h->n_verts*20;
        int valid=fread(header,1,20,f)==20 && !memcmp(header,"PLT1",4) &&
            player_light_word(header+4)==c->source_vertices && player_light_word(header+8)==c->source_models &&
            player_light_word(header+12)==c->source_geometry_hash && player_light_word(header+16)==20;
        if(valid)data=calloc(1,bytes);
        if(data) {
            memcpy(data,header,20);unsigned *words=(unsigned *)data;
            words[1]=c->h->n_verts;words[2]=c->h->n_chars;
            words[3]=player_light_geometry_hash(c->verts,c->h->n_verts);
            for(unsigned i=0;i<c->h->n_batches && valid;i++) {
                const JgcBatch *b=c->batch+i;unsigned n=b->n_vert*20;
                valid=!fseek(f,20+c->lighting_source[i]*20,SEEK_SET) &&
                    fread(data+20+b->vert_off*20,1,n,f)==n;
            }
            if(valid)v=player_light_bind(data,bytes,c->verts,c->h->n_verts,c->h->n_chars);
        }
    } else
    if(!fseek(f,0,SEEK_END) && (length=ftell(f))==(long)(20+c->h->n_verts*20)) {
        rewind(f);data=malloc(length);
        if(data && fread(data,1,length,f)==(unsigned)length)
            v=player_light_bind(data,length,c->verts,c->h->n_verts,c->h->n_chars);
    }
    fclose(f);if(!v){free(data);return -3;}
    for(unsigned j=0;j<c->h->n_batches;j++) {
        const JgcBatch *b=c->batch+j;
        if(b->vert_off>c->h->n_verts || b->n_vert>c->h->n_verts-b->vert_off){free(data);return -3;}
        for(unsigned k=1;k<b->n_vert;k++)if((v[b->vert_off].flags&3)!=(v[b->vert_off+k].flags&3)){free(data);return -3;}
    }
    chars_free_lighting(c);c->lighting_image=data;c->lighting=v;return 0;
}

static CharPreparationProfile preparation_profile;
void chars_preparation_profile(CharPreparationProfile *out)
{*out=preparation_profile;memset(&preparation_profile,0,sizeof preparation_profile);}
static int preparation_enabled;
#include "char_batch_owners.inc"
static void prepare_render_bounds(Chars *c)
{
    if(c->vert_stride<28 || !c->h->n_bones || c->h->n_bones>65536)return;
    float *radius=malloc(c->h->n_bones*sizeof *radius);
    if(!radius)return; /* Allocation failure means draw, never disappear. */
    for(unsigned i=0;i<c->h->n_bones;i++)radius[i]=-1.f;
    for(unsigned ci=0;ci<c->h->n_chars;ci++) {
        const JgcChar *ch=c->chars+ci;
        if(!ch->nbones)continue; /* empty stable-ID slot in a mission cast */
        if(ch->bone0>c->h->n_bones || ch->nbones>c->h->n_bones-ch->bone0)goto invalid;
        for(unsigned bi=0;bi<ch->nbones;bi++) {
            const JgcBone *bone=c->bones+ch->bone0+bi;
            if(bone->batch0>c->h->n_batches || bone->nbatch>c->h->n_batches-bone->batch0)goto invalid;
            for(unsigned j=0;j<bone->nbatch;j++) {
                const JgcBatch *b=c->batch+bone->batch0+j;
                if(b->vert_off>c->h->n_verts || b->n_vert>c->h->n_verts-b->vert_off)goto invalid;
                for(unsigned k=0;k<b->n_vert;k++) {
                    const unsigned char *v=chars_batch_vertices(c,b)+k*c->vert_stride;
                    unsigned short owner;float xyz[3];memcpy(&owner,v+24,2);memcpy(xyz,v+12,12);
                    if(owner>=ch->nbones)owner=0;
                    for(int axis=0;axis<3;axis++) {
                        if(!isfinite(xyz[axis]))goto invalid;
                        float r=fabsf(xyz[axis]);
                        if(r>radius[ch->bone0+owner])radius[ch->bone0+owner]=r;
                    }
                }
            }
        }
        asset_progress();
    }
    c->render_radius=radius;return;
invalid:
    free(radius);
}

/* Bound every native translation/scale key before runtime posing. Rotations
   preserve Euclidean length; component maxima also contain interpolated
   clips and convex blends. Custom/overlay/shared poses take the exact path. */
static void prepare_motion_bounds(Chars *c,unsigned bytes)
{
    if(!c->render_radius || !c->h->n_chars || c->h->n_chars>4096)return;
    c->motion_radius=malloc(c->h->n_chars*sizeof(float));if(!c->motion_radius)return;
    for(unsigned ci=0;ci<c->h->n_chars;ci++) {
        const JgcChar *ch=c->chars+ci;float reach[CHAR_RENDER_MAX_BONES],scale[CHAR_RENDER_MAX_BONES],radius=0;
        c->motion_radius[ci]=INFINITY;
        if(ch->nbones>CHAR_RENDER_MAX_BONES || ch->motion0>c->h->n_motions || ch->nmotions>c->h->n_motions-ch->motion0)continue;
        for(unsigned bi=0;bi<ch->nbones;bi++) {
            const JgcBone *bone=c->bones+ch->bone0+bi;
            float extent[3]={fabsf(bone->pos[0]),fabsf(bone->pos[1]),fabsf(bone->pos[2])};
            float largest=fmaxf(1.f,fmaxf(fabsf(bone->scl[0]),fmaxf(fabsf(bone->scl[1]),fabsf(bone->scl[2]))));
            for(int k=0;k<3;k++)if(!isfinite(bone->pos[k]) || !isfinite(bone->scl[k]))goto next_character;
            for(unsigned mi=0;mi<ch->nmotions;mi++) {
                unsigned at=c->motions[ch->motion0+mi].chan0;
                if(at>bytes || ch->nbones>(bytes-at)/sizeof(JgcChan))goto next_character;
                const JgcChan *chan=(const JgcChan *)chars_data(c,at)+bi;
                unsigned offset[2]={chan->off_pos,chan->off_scl},count[2]={chan->npos,chan->nscl};
                for(unsigned kind=0;kind<2;kind++) {
                    if(c->h->off_keys>bytes || offset[kind]>bytes-c->h->off_keys || count[kind]>(bytes-c->h->off_keys-offset[kind])/12)goto next_character;
                    const float *keys=(const float *)chars_data(c,c->h->off_keys+offset[kind]);
                    for(unsigned k=0;k<count[kind]*3;k++) {
                        float v=fabsf(keys[k]);if(!isfinite(v))goto next_character;
                        if(kind){if(v>largest)largest=v;}else if(v>extent[k%3])extent[k%3]=v;
                    }
                }
            }
            int parent=bone->parent-(int)ch->bone0;
            if(parent>=(int)bi)goto next_character;
            float ps=parent>=0 ? scale[parent]:1.f;
            scale[bi]=ps*largest*1.0001f;
            reach[bi]=(parent>=0 ? reach[parent]:0.f)+ps*sqrtf(extent[0]*extent[0]+extent[1]*extent[1]+extent[2]*extent[2]);
            float owner=c->render_radius[ch->bone0+bi];
            if(owner>=0){float r=reach[bi]+scale[bi]*owner*1.732051f;if(r>radius)radius=r;}
        }
        c->motion_radius[ci]=radius*1.001f+.05f;
next_character:
        asset_progress();
    }
}

static int read_char_part(SceUID fd,unsigned char *p,unsigned size)
{
    while(size){unsigned count=size>ASSET_READ_CHUNK ? ASSET_READ_CHUNK:size;
        int n=sceIoRead(fd,p,count);if(n<=0)return -3;p+=n;size-=n;asset_progress();}
    return 0;
}
static int char_layout_valid(const JgcHeader *h,unsigned size)
{
    int stride=!memcmp(h->magic,"JGC3",4) ? 40:!memcmp(h->magic,"JGC2",4) ? 28:!memcmp(h->magic,"JGC1",4) ? 24:0;
    if(!stride || h->off_chars<sizeof *h)return 0;
    unsigned offsets[]={h->off_chars,h->off_bones,h->off_batches,h->off_verts,h->off_idx,h->off_tex,h->off_motions,h->off_keys,h->off_pix,size};
    for(unsigned i=0;i<9;i++)if(offsets[i]>offsets[i+1] || (offsets[i]&3))return 0;
    return h->n_chars<=(h->off_bones-h->off_chars)/sizeof(JgcChar) &&
        h->n_bones<=(h->off_batches-h->off_bones)/sizeof(JgcBone) &&
        h->n_batches<=(h->off_verts-h->off_batches)/sizeof(JgcBatch) &&
        h->n_verts<=(h->off_idx-h->off_verts)/(unsigned)stride &&
        h->n_idx<=(h->off_tex-h->off_idx)/2 &&
        h->n_tex<=(h->off_motions-h->off_tex)/sizeof(JgcTex) &&
        h->n_motions<=(h->off_keys-h->off_motions)/sizeof(JgcMotion);
}
/* Optional mesh-only extension. All validation precedes publication; a bad
   directory is an asset error, while an absent directory is legacy mode. */
static int chars_validate_source_lod(Chars *c)
{
    if(c->h->off_chars<80 || memcmp(c->base+68,"JCL1",4))return 1;
    unsigned words[2];memcpy(words,c->base+72,sizeof words);
    unsigned count=words[0],at=words[1];
    if(!count || count>c->h->n_chars || at<80 || (at&3) ||
       at>c->h->off_chars || count>(c->h->off_chars-at)/4)return 0;
    const unsigned *map=(const unsigned *)(c->base+at);
    for(unsigned ci=0;ci<count;ci++)if(map[ci]) {
        unsigned far=map[ci];if(far==ci || far>=c->h->n_chars || (far<count && map[far]))return 0;
        const JgcChar *near=c->chars+ci,*low=c->chars+far;
        if(!near->nbones || near->nbones!=low->nbones || near->motion0!=low->motion0 || near->nmotions!=low->nmotions ||
           near->bone0>c->h->n_bones || near->nbones>c->h->n_bones-near->bone0 ||
           low->bone0>c->h->n_bones || low->nbones>c->h->n_bones-low->bone0)return 0;
        for(unsigned bi=0;bi<near->nbones;bi++) {
            const JgcBone *a=c->bones+near->bone0+bi,*b=c->bones+low->bone0+bi;
            /* Far trees occasionally have different unkeyed bind values
               (and Bomb5 has a different ignore-angle flag). Sampling the
               selected tree preserves those source values while borrowing
               the unchanged channel stream. */
            if((a->parent<0 ? -1:a->parent-(int)near->bone0)!=(b->parent<0 ? -1:b->parent-(int)low->bone0) ||
               b->batch0>c->h->n_batches || b->nbatch>c->h->n_batches-b->batch0)return 0;
        }
    }
    c->source_lod=map;c->source_lod_count=count;return 1;
}
#include "char_storage.inc"
static int chars_load_storage(Chars *c, const char *path,int sections)
{
    forget_packets(c);memset(c,0,sizeof *c);
#ifdef JGR_CHAR_ALLOC_LIMIT
    char_limit_active=!strncmp(path,"movie",5) || !strncmp(path,"ending",6);
#endif
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0777);if(fd<0)return -1;
    SceOff size=sceIoLseek(fd,0,PSP_SEEK_END);JgcHeader header;int result=-4;
    if(sceIoLseek(fd,0,PSP_SEEK_SET)<0 || size<(SceOff)sizeof header || size>0x02000000 ||
       read_char_part(fd,(unsigned char *)&header,sizeof header) || !char_layout_valid(&header,(unsigned)size))goto failed;
    if(sceIoLseek(fd,0,PSP_SEEK_SET)<0){result=-3;goto failed;}
#ifndef JGR_CHAR_SPLIT_ALLOC
    if(!sections)c->base=memalign(64,((unsigned)size+63)&~63u);
#endif
    if(c->base)result=read_char_part(fd,c->base,(unsigned)size);
    else {
        /* Movie/garage/level handoffs fragment the PSP heap. Keep original
           offsets and immutable payloads, allocating the largest sections
           first. No conversion, texture eviction or animation reduction. */
        unsigned offsets[]={0,header.off_verts,header.off_idx,header.off_motions,header.off_pix,(unsigned)size};
        unsigned char *parts[5]={0};unsigned allocated=0;result=-2;
        for(unsigned pass=0;pass<5;pass++) {
            unsigned best=5;
            for(unsigned j=0;j<5;j++)if(!(allocated&(1u<<j)) && (best==5 || offsets[j+1]-offsets[j]>offsets[best+1]-offsets[best]))best=j;
            allocated|=1u<<best;unsigned length=offsets[best+1]-offsets[best];
            if(length && !(parts[best]=memalign(64,(length+63)&~63u))){for(unsigned j=0;j<5;j++)free(parts[j]);
                result=read_char_pages(c,fd,&header,(unsigned)size);goto storage_ready;}
        }
        c->base=parts[0];c->vertex_data=parts[1];c->index_data=parts[2];c->motion_data=parts[3];c->texture_data=parts[4];
        for(unsigned j=0;j<5;j++)if((result=read_char_part(fd,parts[j],offsets[j+1]-offsets[j])))goto failed;
    }
storage_ready:
    if(result)goto failed;
    c->h=(JgcHeader *)c->base;
    c->vert_stride=!memcmp(c->h->magic,"JGC3",4) ? 40:!memcmp(c->h->magic,"JGC2",4) ? 28:24;
    c->chars=(JgcChar *)chars_data(c,c->h->off_chars);c->bones=(JgcBone *)chars_data(c,c->h->off_bones);
    c->batch=(JgcBatch *)chars_data(c,c->h->off_batches);c->verts=c->geometry ? NULL:chars_data(c,c->h->off_verts);
    c->idx=c->geometry ? NULL:(unsigned short *)chars_data(c,c->h->off_idx);c->tex=(JgcTex *)(c->texture_metadata ? c->texture_metadata:chars_data(c,c->h->off_tex));
    c->motions=c->h->n_motions ? (JgcMotion *)chars_data(c,c->h->off_motions):NULL;
    if(!chars_validate_source_lod(c)){result=-4;goto failed;}
    for(unsigned i=0;i<c->h->n_tex;i++) {
        const JgcTex *t=c->tex+i;
        if(t->off_clut<c->h->off_pix || t->off_clut>(unsigned)size || (unsigned)size-t->off_clut<1024 ||
           t->off_pix<c->h->off_pix || t->off_pix>(unsigned)size || (unsigned)t->w*t->h>(unsigned)size-t->off_pix){result=-4;goto failed;}
    }
    if(c->geometry)printf("CHAR_ALLOC paged path=%s batches=%u textures=%u\n",path,c->h->n_batches,c->h->n_tex);
    sceIoClose(fd);c->resident_bytes=(unsigned)size;prepare_render_bounds(c);prepare_motion_bounds(c,(unsigned)size);prepare_batch_owners(c);
    load_ped_native(c,path);sceKernelDcacheWritebackAll();return 0;
failed:
    sceIoClose(fd);chars_free(c);return result;
}
int chars_load(Chars *c,const char *path){return chars_load_storage(c,path,0);}
int chars_load_sections(Chars *c,const char *path){return chars_load_storage(c,path,1);}

#include "char_roster.inc"

void chars_update(Chars *c, float dt) { c->frame += dt * 30.0f; }

/* ---- row-major 4x4 helpers ---- */
static void m_ident(float *m) { for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.f : 0.f; }
#include "render_math.h"
#define m_mul render_affine_multiply
#define node_matrix chars_local_matrix
#include "char_preparation.inc"
static void gu_model(const float *m){ge_submit_model(m);}
void chars_mesh_matrices(const Chars *c,const JgcChar *ch,int motion,float frame,
                         const float root[16],float mats[64][16])
{
    if(ch->nbones>64)return;
    unsigned started=render_clock();RenderMathState math;render_math_begin(&math);
    for(unsigned i=0;i<ch->nbones;i++) {
        CharPose pose;float local[16] __attribute__((aligned(16)));
        chars_sample_pose(c,ch,motion,frame,i,&pose);
        node_matrix(local,pose.ev,pose.pos,pose.ang,pose.scl);
        int parent=c->bones[ch->bone0+i].parent-(int)ch->bone0;
        render_affine_multiply(mats[i],parent>=0 ? mats[parent] : root,local);
    }
    render_math_end(&math);vfpu_profile_attachment(render_clock()-started,ch->nbones);
}
void chars_pose_mesh_matrices(const Chars *c,const JgcChar *ch,const CharPose *pose,
                              const float root[16],float mats[64][16])
{
    if(ch->nbones>64)return;
    unsigned started=render_clock();RenderMathState math;render_math_begin(&math);
    for(unsigned i=0;i<ch->nbones;i++) {
        float local[16] __attribute__((aligned(16)));node_matrix(local,pose[i].ev,pose[i].pos,pose[i].ang,pose[i].scl);
        int parent=c->bones[ch->bone0+i].parent-(int)ch->bone0;
        render_affine_multiply(mats[i],parent>=0 ? mats[parent]:root,local);
    }
    render_math_end(&math);vfpu_profile_attachment(render_clock()-started,ch->nbones);
}
#define VFMT (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D)
typedef SourceLitVertex DrawVert;
static DrawVert cached_skin_vertices[CHAR_CPU_SKIN_VERTICES] __attribute__((aligned(16)));
static float cpu_cel_values[CHAR_CPU_SKIN_VERTICES];
static unsigned char cpu_cel_exact[CHAR_CPU_SKIN_VERTICES/8];
static DrawVert *publish_vertices(const void *src,unsigned count)
{
    DrawVert *out=sceGuGetMemory(count*sizeof *out);
    /* Aligned word stores avoid libc's partial-word read/modify/write
       copies into the uncached GU command/vertex arena. */
    for(unsigned i=0;i<count*sizeof *out/4;i++) {
        unsigned word;memcpy(&word,(const unsigned char *)src+4*i,4);
        ((unsigned *)out)[i]=word;
    }
    return out;
}
typedef struct { float u, v; unsigned color; float x, y, z; unsigned short bone, pad; } SkinVert;
static float environment_view[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static int source_lighting_enabled;
static float recovery_offset[3];
static int recovery_active;
static unsigned char recovery_sub[256],recovery_add[256];
void chars_recovery_flash(float strength)
{
    recovery_active=strength>0.f;
    if(!recovery_active)return;
    recovery_offset[0]=recovery_offset[1]=-strength;recovery_offset[2]=strength;
    /* Ordinary PLAY banks already contain baked lighting. A tiny channel
       lookup applies the pulse to that base without retaining/reloading
       every original material and normal. Dynamic lighting below instead
       adds to original materials before lighting, as on Dreamcast. */
    const float white[3]={1,1,1};
    for(unsigned i=0;i<256;i++) {
        unsigned c=source_light_colour_offset(i*0x010101u,white,1.f,recovery_offset);
        recovery_sub[i]=c&255;recovery_add[i]=(c>>16)&255;
    }
}
static unsigned recovery_colour(unsigned colour)
{
    return (colour&0xff000000u)|recovery_sub[colour&255]|
        ((unsigned)recovery_sub[(colour>>8)&255]<<8)|((unsigned)recovery_add[(colour>>16)&255]<<16);
}
static int event_actor_material;
static int spray_hud_lighting;
static const CharLight *scene_light[2];
static const unsigned short *external_pixels;static unsigned external_width,external_height;
void chars_external_texture(const unsigned short *pixels,unsigned width,unsigned height)
{external_pixels=pixels;external_width=width;external_height=height;}
void chars_scene_lighting(const CharLight *normal,const CharLight *cel){scene_light[0]=normal;scene_light[1]=cel;}
static float *cel_values;
static unsigned char *cel_exact;
static struct {
    const Chars *bank;const JgcChar *ch;const JgcBatch *batch;
    float diffuse,ambient,local[CHAR_RENDER_MAX_BONES][3];
} cel_source;
/* The light and posed hierarchy are immutable during one actor draw.
   Reuse the exact transformed light across that actor's material strips. */
static unsigned char cel_local_valid[CHAR_RENDER_MAX_BONES/8];
#include "source_local_light.h"
static SourceLocalLightKey local_light_key;
/* Hosts can exercise the production native split policy without enabling
   PSP GU/VFPU code. The default policy is unchanged on either platform. */
#ifndef JGR_CEL_NATIVE_SPLIT
#if defined(__PSP__) && !defined(JGR_CEL_EXACT_SPLIT)
#define JGR_CEL_NATIVE_SPLIT 1
#else
#define JGR_CEL_NATIVE_SPLIT 0
#endif
#endif
#ifndef JGR_FINAL_CEL_ESTIMATE_REUSE
#ifdef __PSP__
#define JGR_FINAL_CEL_ESTIMATE_REUSE 1
#else
#define JGR_FINAL_CEL_ESTIMATE_REUSE 0
#endif
#endif
#if JGR_FINAL_CEL_ESTIMATE_REUSE && JGR_CEL_NATIVE_SPLIT && !defined(JGR_LIGHT_REFERENCE)
static int final_cel_estimate_enabled=1;
static inline float final_cel_estimate(const float local[3],const float normal[3],
    float diffuse,float ambient,unsigned *resolved)
{
    float error,value=source_light_estimate(local,normal,diffuse,ambient,&error);
    /* The old side classifier accepts this very estimate. Its sign and
       bits therefore also equal the later native boundary calculation. */
    if(isfinite(value) && isfinite(error) && fabsf(value)>error) {
        *resolved=1;return value;
    }
    return source_light_cel_side(local,normal,diffuse,ambient);
}
#endif
#ifndef JGR_FINAL_CEL_RESOLVED_PREPASS
#ifdef __PSP__
#define JGR_FINAL_CEL_RESOLVED_PREPASS 1
#else
#define JGR_FINAL_CEL_RESOLVED_PREPASS 0
#endif
#endif
#if JGR_FINAL_CEL_RESOLVED_PREPASS
/* Scratch for this skin_batch/draw_skin pair, never a retained cache key.
   Full-cache early returns leave it false rather than adding a new scan. */
static int final_cel_resolved_prepass_enabled=1;
static unsigned cel_prepared_all_resolved;
#ifdef JGR_TEST_CEL_RESOLVED_PREPASS
static unsigned cel_prepass_skipped_batches,cel_prepass_skipped_cuts,cel_prepass_checked_corners;
#endif
#endif
static float final_indexed_resolved(unsigned index,float value);
static float exact_cel_value(unsigned index)
{
    unsigned mask=1u<<(index&7);
    if(cel_exact[index>>3]&mask)return cel_values[index];
    const Chars *c=cel_source.bank;
    const SkinVert *src=(const SkinVert *)(chars_batch_vertices(c,cel_source.batch)+index*c->vert_stride);
    const PlayerLightVertex *retained=c->lighting ? c->lighting+cel_source.batch->vert_off+index:NULL;
    cel_exact[index>>3]|=mask;
    if(!(retained ? retained->flags&0x100:src->pad&1))return final_indexed_resolved(index,cel_values[index]=1.f);
    unsigned bone=src->bone<cel_source.ch->nbones ? src->bone:0;
    const float *normal=retained ? retained->normal:(const float *)((const unsigned char *)src+28);
    return final_indexed_resolved(index,cel_values[index]=source_light_value(cel_source.local[bone],normal,cel_source.diffuse,cel_source.ambient,1));
}
/* Split position of a boundary vertex. The PSP uses the native-float
   light value: 2,700 cel triangles per character-selection frame each
   needed three ordered binary64 kernels, 10 ms of a 41 ms frame. The
   recorded side keeps the sign, so an ambiguous vertex still lands on
   the side the binary64 kernel chose and the cut stays consistent. The
   boundary moves by far less than a pixel. Hosts keep the exact value. */
static float cel_boundary_value(unsigned index)
{
#if JGR_CEL_NATIVE_SPLIT
    unsigned mask=1u<<(index&7);
    if(cel_exact[index>>3]&mask)return cel_values[index];
    const Chars *c=cel_source.bank;
    const SkinVert *src=(const SkinVert *)(chars_batch_vertices(c,cel_source.batch)+index*c->vert_stride);
    const PlayerLightVertex *retained=c->lighting ? c->lighting+cel_source.batch->vert_off+index:NULL;
    cel_exact[index>>3]|=mask;
    if(!(retained ? retained->flags&0x100:src->pad&1))return final_indexed_resolved(index,cel_values[index]=1.f);
    unsigned bone=src->bone<cel_source.ch->nbones ? src->bone:0;
    const float *normal=retained ? retained->normal:(const float *)((const unsigned char *)src+28);
    float error,value=source_light_estimate(cel_source.local[bone],normal,cel_source.diffuse,cel_source.ambient,&error);
    int side=cel_values[index]>0;
    if(!isfinite(value) || !isfinite(error) || (value>0)!=side)value=side ? error:-error;
    return final_indexed_resolved(index,cel_values[index]=value);
#else
    return exact_cel_value(index);
#endif
}
void chars_final_lighting(int enabled){source_lighting_enabled=enabled;}
void chars_spray_hud_lighting(int enabled){spray_hud_lighting=enabled;}
void chars_environment_view(const float *view){memcpy(environment_view,view,sizeof environment_view);}

#include "char_material_cache.inc"
#include "char_final_vertex.inc"
#include "char_final_indexed.inc"
#include "char_final_cel_packet.inc"
/* Garage room: batches under bones whose pose never changes over the room
   motion are skinned ONCE into resident GE-readable storage and drawn from
   there. The caller scopes it around the room passes only; lighting, guard
   codes and near-plane clipping still run every frame on the same output. */
static const Chars *static_cache_bank;static const unsigned char *static_cache_bones;
static DrawVert *static_cache_vertices;static unsigned *static_cache_offsets; /* per batch: offset+1, 0 = not cached */
static unsigned static_cache_capacity,static_cache_used,static_cache_hits,static_cache_fills;
static int static_cache_active;static unsigned skin_batch_bone=~0u;
static void static_cache_release(void)
{
    free(static_cache_vertices);free(static_cache_offsets);static_cache_vertices=NULL;static_cache_offsets=NULL;
    static_cache_bank=NULL;static_cache_bones=NULL;static_cache_capacity=static_cache_used=0;static_cache_active=0;
}
int chars_static_cache_prepare(const Chars *c,const unsigned char *bones,unsigned vertices)
{
    static_cache_release();
    if(!c || !c->h || !bones || !vertices)return -1;
    static_cache_offsets=calloc(c->h->n_batches,sizeof *static_cache_offsets);
    static_cache_vertices=malloc(vertices*sizeof *static_cache_vertices);
    if(!static_cache_offsets || !static_cache_vertices){static_cache_release();return -1;}
    static_cache_bank=c;static_cache_bones=bones;static_cache_capacity=vertices;return 0;
}
void chars_static_cache_scope(int active){static_cache_active=active && static_cache_bank!=NULL;}
void chars_static_cache_profile(unsigned out[2]){out[0]=static_cache_hits;out[1]=static_cache_fills;static_cache_hits=static_cache_fills=0;}
static int static_cache_slot(const Chars *c,const JgcChar *ch,const JgcBatch *b)
{
    if(!static_cache_active || static_cache_bank!=c || skin_batch_bone>=ch->nbones)return -1;
    if(!(static_cache_bones[skin_batch_bone>>3]&(1u<<(skin_batch_bone&7))))return -1;
    if(c->vert_stride!=40 || c->lighting || (b->flags&2048) || recovery_active || event_actor_material || spray_hud_lighting ||
       source_lighting_enabled || hud_reciprocal_scale!=1.f || hud_depth_shift!=0.f)return -1;
    if(scene_light[0] && !(b->flags&8) && ((b->flags>>12)&3)==2)return -1; /* cel batches carry per-vertex sides */
    unsigned slot=(unsigned)(b-c->batch);
    return slot<c->h->n_batches ? (int)slot:-1;
}
unsigned chars_static_bones(const Chars *c,int ci,int motion,unsigned char *mask,unsigned *cacheable)
{
    if(cacheable)*cacheable=0;
    if(!c || !c->h || !mask || (unsigned)ci>=c->h->n_chars)return 0;
    const JgcChar *ch=c->chars+ci;
    memset(mask,0,(CHAR_RENDER_MAX_BONES+7)/8);
    if(ch->nbones>CHAR_RENDER_MAX_BONES || motion<0 || (ch->nmotions && (unsigned)motion>=ch->nmotions))return 0;
    /* A bank without motions draws its rest pose: every bone is static. */
    int mi=ch->nmotions ? (int)ch->motion0+motion:-1;unsigned frames=mi>=0 ? c->motions[mi].nframes:0;
    unsigned char local[(CHAR_RENDER_MAX_BONES+7)/8];memset(local,0,sizeof local);
    for(unsigned b=0;b<ch->nbones;b++) {
        CharPose first,pose;int constant=1;
        if(frames)chars_sample_pose(c,ch,mi,0.f,b,&first);
        for(unsigned f=1;f<frames && constant;f++) {
            chars_sample_pose(c,ch,mi,(float)f,b,&pose);
            if(memcmp(pose.pos,first.pos,sizeof pose.pos) || memcmp(pose.ang,first.ang,sizeof pose.ang) ||
               memcmp(pose.scl,first.scl,sizeof pose.scl) || pose.ev!=first.ev)constant=0;
        }
        if(constant)local[b>>3]|=1u<<(b&7);
    }
    /* An animated ancestor moves every descendant. Iterate to a fixed point
       so the hierarchy order does not matter. */
    memcpy(mask,local,sizeof local);
    for(unsigned round=0;round<ch->nbones;round++) {
        int changed=0;
        for(unsigned b=0;b<ch->nbones;b++) {
            if(!(mask[b>>3]&(1u<<(b&7))))continue;
            int parent=c->bones[ch->bone0+b].parent;
            if(parent>=0)parent-=(int)ch->bone0;
            if(parent>=0 && ((unsigned)parent>=ch->nbones || !(mask[parent>>3]&(1u<<(parent&7))))){mask[b>>3]&=~(1u<<(b&7));changed=1;}
        }
        if(!changed)break;
    }
    unsigned count=0;
    for(unsigned b=0;b<ch->nbones;b++) {
        if(!(mask[b>>3]&(1u<<(b&7))))continue;
        count++;
        const JgcBone *bone=c->bones+ch->bone0+b;
        for(unsigned j=0;j<bone->nbatch && cacheable;j++) {
            const JgcBatch *batch=c->batch+bone->batch0+j;
            if(c->vert_stride==40 && !(batch->flags&2048) && ((batch->flags&8) || ((batch->flags>>12)&3)!=2))*cacheable+=batch->n_vert;
        }
    }
    return count;
}
static DrawVert *skin_batch(Chars *c, const JgcChar *ch, const JgcBatch *b, float mats[][16])
{
    final_indexed_current=NULL;
#if JGR_FINAL_CEL_RESOLVED_PREPASS
    cel_prepared_all_resolved=0;
#endif
#if JGR_ACTOR_TIMING_FORCE
    unsigned lookup_started=render_clock();
#endif
    GePacket *final_cache=final_cel_packet_pending(c,b) ? NULL:final_vertex_lookup(c,ch,b,mats);
#if JGR_ACTOR_TIMING_FORCE
    final_cache_lookup_us+=render_clock()-lookup_started;
#endif
    if(final_cache && final_cache->count)return cached_skin_vertices;
    int static_slot=static_cache_slot(c,ch,b);
    if(static_slot>=0 && static_cache_offsets[static_slot]) {
        cel_values=NULL;static_cache_hits++;
        return static_cache_vertices+static_cache_offsets[static_slot]-1;
    }
    MaterialCacheEntry *cache=material_cache_lookup(c,b);
    if(cache && cache->bank) {
        memcpy(cached_skin_vertices,material_cache_vertices+cache->first,b->n_vert*sizeof(DrawVert));
        cel_values=NULL;
        return cached_skin_vertices;
    }
    unsigned started=render_clock();
    cpu_skin_batches++;cpu_skin_vertices+=b->n_vert;
    const unsigned char *vertices=chars_batch_vertices(c,b);
    DrawVert *dst = b->n_vert<=CHAR_CPU_SKIN_VERTICES ? cached_skin_vertices:sceGuGetMemory(b->n_vert*sizeof *dst);
    int lighting=source_lighting_enabled && c->vert_stride==40 && !(b->flags&8) ? (b->flags>>12)&3:0;
    /* Mode 7 -> 234fc6/23471e consumes prelit vertex-cache intensity;
       unlike mode 2, it does not bypass lighting for strip bit 0.
       The white reverse faces are intentionally shaded into black. */
    if(spray_hud_lighting && c->vert_stride==40)lighting=1;
    const PlayerLightVertex *retained=c->lighting ? c->lighting+b->vert_off:NULL;
    if(source_lighting_enabled && retained && b->n_vert && !(b->flags&8))lighting=retained[0].flags&3;
    if(scene_light[0] && c->vert_stride==40 && !(b->flags&8))lighting=(b->flags>>12)&3;
    if(lighting && event_actor_material)lighting=2; /* 0cc7c8: primary typed body uses mode 0. */
    cel_values=lighting==2 ? (b->n_vert<=CHAR_CPU_SKIN_VERTICES ? cpu_cel_values:sceGuGetMemory(b->n_vert*sizeof(float))):NULL;
    float direction[3]={.09f,.84f,-.01f},local_light[3]={0};unsigned light_bone=~0u;
    if(lighting==2){direction[0]=-.01f;direction[1]=-.6f;direction[2]=.04f;}
    else if(spray_hud_lighting){direction[0]=direction[1]=0;direction[2]=1;}
    else if(lighting) {float length=sqrtf((direction[0]*direction[0]+direction[1]*direction[1])+direction[2]*direction[2]);for(int k=0;k<3;k++)direction[k]/=length;}
    const CharLight *light=lighting ? scene_light[lighting==2]:NULL;
    if(light){memcpy(direction,light->direction,12);if(lighting!=2 && !light->raw_direction){float n=sqrtf(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);if(n>0)for(int j=0;j<3;j++)direction[j]/=n;}}
    float diffuse=light ? light->diffuse:lighting==2 || spray_hud_lighting ? 1.f:.95f;
    float ambient=light ? light->ambient:lighting==2 ? 0.f:spray_hud_lighting ? .5f:.73f;
    float rgb[3]={spray_hud_lighting ? 1.f:.64f,spray_hud_lighting ? 1.f:.83f,1.f};
    if(light)for(int j=0;j<3;j++)rgb[j]=((light->color[0]>>(16-j*8))&255)/255.f;
    SourceNormalPalette normal_palette;
    if(lighting && lighting!=2 && !recovery_active)source_normal_palette_begin(&normal_palette,rgb,ambient);
    if(lighting)source_local_light_begin(&local_light_key,direction,cel_local_valid,sizeof cel_local_valid);
    if(cel_values) {
        cel_exact=b->n_vert<=CHAR_CPU_SKIN_VERTICES ? cpu_cel_exact:sceGuGetMemory((b->n_vert+7)/8);
        memset(cel_exact,0,(b->n_vert+7)/8);
        cel_source.bank=c;cel_source.ch=ch;cel_source.batch=b;
        cel_source.diffuse=diffuse;cel_source.ambient=ambient;
    }
    const unsigned short *indexed=final_indexed_current=final_indexed_batch(c,ch,b);
    unsigned indexed_reused=0;
#if JGR_FINAL_CEL_ESTIMATE_REUSE && JGR_CEL_NATIVE_SPLIT && !defined(JGR_LIGHT_REFERENCE)
    const FinalIndexedPlan *estimate_plan=c->indexed_prepare;
    int retain_estimate=final_cel_estimate_enabled && lighting==2 && estimate_plan &&
        estimate_plan->header==c->h && !retained && !recovery_active && !event_actor_material &&
        !spray_hud_lighting && !scene_light[0] && !scene_light[1] && !(b->flags&2048) &&
        hud_reciprocal_scale==1.f && hud_depth_shift==0.f;
#if JGR_FINAL_CEL_RESOLVED_PREPASS
    cel_prepared_all_resolved=final_cel_resolved_prepass_enabled && retain_estimate;
#endif
#endif
    for (unsigned i = 0; i < b->n_vert; i++) {
        const SkinVert *src=(const SkinVert *)(vertices+i*c->vert_stride);
        unsigned bi = src->bone < ch->nbones ? src->bone : 0;
        const float *m = mats[bi]; float x = src->x, y = src->y, z = src->z;
        dst[i].u = src->u; dst[i].v = src->v; dst[i].color = src->color;
        unsigned shared_id=indexed ? indexed[i]:FINAL_INDEXED_NONE;
        FinalIndexedValue *shared=shared_id!=FINAL_INDEXED_NONE ? final_indexed_work.value+shared_id:NULL;
        if(shared && (shared->generation&FINAL_INDEXED_GENERATION)==final_indexed_generation) {
            dst[i].x=shared->x;dst[i].y=shared->y;dst[i].z=shared->z;dst[i].color=shared->color;
            if(cel_values) {
                cel_values[i]=shared->cel;
#if JGR_FINAL_CEL_RESOLVED_PREPASS
                if(cel_prepared_all_resolved && ((shared->generation&FINAL_INDEXED_EXACT) ?
                   !isfinite(shared->cel):((src->pad&1) || shared->cel!=1.f)))cel_prepared_all_resolved=0;
#endif
                if(shared->generation&FINAL_INDEXED_EXACT) {
                    cel_exact[i>>3]|=1u<<(i&7);final_indexed_profile.boundary_hits++;
                } else if((src->pad&1) && light_bone!=bi) {
                    /* A normal-lit strip may have replaced this bone's
                       local light since the shared cel side was prepared. */
                    source_local_light(local_light,cel_source.local,cel_local_valid,bi,m,direction);light_bone=bi;
                }
            }
            indexed_reused++;continue;
        }
        uint32_t material=lighting && retained ? retained[i].material:src->color;
        if(lighting)dst[i].color=material;
        if(lighting && (retained ? retained[i].flags&0x100:src->pad&1)) {
            const float *n=retained ? retained[i].normal:(const float *)((const unsigned char *)src+28);
            /* World/view rotations cancel algebraically in M^T * L.
               Retain scale (no inverse-transpose normalization): source
               2337b4/2956e0 deliberately transform the light this way. */
            if(light_bone!=bi) {
                source_local_light(local_light,cel_source.local,cel_local_valid,bi,m,direction);
                light_bone=bi;
            }
            if(lighting==2) {
#if JGR_FINAL_CEL_ESTIMATE_REUSE && JGR_CEL_NATIVE_SPLIT && !defined(JGR_LIGHT_REFERENCE)
                unsigned resolved=0;
                cel_values[i]=retain_estimate ? final_cel_estimate(local_light,n,diffuse,ambient,&resolved):
                    source_light_cel_side(local_light,n,diffuse,ambient);
                if(resolved)cel_exact[i>>3]|=1u<<(i&7);
#if JGR_FINAL_CEL_RESOLVED_PREPASS
                else cel_prepared_all_resolved=0;
#endif
#else
                cel_values[i]=source_light_cel_side(local_light,n,diffuse,ambient);
#endif
            }
            else dst[i].color=recovery_active ?
                source_light_colour_offset(material,rgb,source_light_value(local_light,n,diffuse,ambient,0),recovery_offset):
                source_light_normal_colour_cached(material,&normal_palette,local_light,n,diffuse);
        } else if(cel_values)cel_values[i]=1; /* 2957a4: no-normal side=1, intensity=1 */
        else if(recovery_active)dst[i].color=recovery_colour(dst[i].color);
        if((b->flags&2048) && c->vert_stride==40) {
            const float *n=(const float *)((const unsigned char *)src+28);float world[3];
            /* 256900 transforms the original normal with w=0, without
               normalization. 24d94c maps cached view XY to .5*n + .5.
               Ninja's screen-down camera Y is the opposite of PSP's. */
            for(int r=0;r<3;r++)world[r]=m[r*4]*n[0]+m[r*4+1]*n[1]+m[r*4+2]*n[2];
            float nx=environment_view[0]*world[0]+environment_view[1]*world[1]+environment_view[2]*world[2];
            float ny=environment_view[4]*world[0]+environment_view[5]*world[1]+environment_view[6]*world[2];
            dst[i].u=.5f*nx+.5f;dst[i].v=-.5f*ny+.5f;
        }
        dst[i].x = m[0]*x + m[1]*y + m[2]*z + m[3];
        dst[i].y = m[4]*x + m[5]*y + m[6]*z + m[7];
        dst[i].z = m[8]*x + m[9]*y + m[10]*z + m[11];
        if((hud_reciprocal_scale!=1.f || hud_depth_shift!=0.f) && dst[i].z<0.f){
            /* 02f000 / 0ba260 operate on the already projected PVR
               reciprocal Z. PSP needs equivalent virtual vertices: change
               XYZ together so its x/z and y/z projection does not move. */
            float reciprocal=-1.f/dst[i].z;
            reciprocal*=hud_reciprocal_scale;
            reciprocal/=1.f-hud_depth_shift*reciprocal;
            float z=-1.f/reciprocal,ratio=z/dst[i].z;
            dst[i].x*=ratio;dst[i].y*=ratio;dst[i].z=z;
        }
        if(shared) {
            shared->x=dst[i].x;shared->y=dst[i].y;shared->z=dst[i].z;shared->color=dst[i].color;
            shared->cel=cel_values ? cel_values[i]:0.f;shared->generation=final_indexed_generation;
#if JGR_FINAL_CEL_ESTIMATE_REUSE && JGR_CEL_NATIVE_SPLIT && !defined(JGR_LIGHT_REFERENCE)
            if(cel_values && (cel_exact[i>>3]&(1u<<(i&7))))shared->generation|=FINAL_INDEXED_EXACT;
#endif
        }
    }
    if(indexed) {
        final_indexed_profile.reused_vertices+=indexed_reused;
        final_indexed_profile.prepared_vertices+=b->n_vert-indexed_reused;
    }
    if(final_cache)final_vertex_store(final_cache,c,ch,b,mats,dst);
    if(cache)material_cache_store(cache,c,b,dst);
    if(static_slot>=0 && !cel_values && static_cache_used+b->n_vert<=static_cache_capacity) {
        DrawVert *keep=static_cache_vertices+static_cache_used;
        memcpy(keep,dst,b->n_vert*sizeof *keep);
#ifdef __PSP__
        sceKernelDcacheWritebackRange(keep,b->n_vert*sizeof *keep);
#endif
        static_cache_offsets[static_slot]=static_cache_used+1;static_cache_used+=b->n_vert;static_cache_fills++;
        cpu_skin_us+=render_clock()-started;
        return keep;
    }
    cpu_skin_us+=render_clock()-started;
    return dst;
}
/* PSP submission adapter: Ninja cache vertices have one owner bone, even
   when a triangle spans several bones. Preserve those owners with exact
   unit weights, instead of multiplying every vertex on the CPU. At most
   eight different bones fit a GE palette; wider batches retain the CPU
   path. No pose, animation, mesh, UV or physics data is approximated.

   Lighting, environment UVs, CPU clipping and HUD reciprocal-depth edits
   still need their original CPU kernels. Do not silently bypass them. */
/* Scoped to one immutable posed actor. Consecutive strips often use the
   same owner bones; the GE retains its palette until we replace a slot. */
typedef struct { unsigned valid;unsigned short bone[8];unsigned transformed; } GePalette;
static unsigned bone_uploads,bone_reused;
static CharWorkStats work_window[CHAR_WORK_COUNT],work_totals[CHAR_WORK_COUNT];
void chars_work_begin(CharWorkStats *mark)
{
    *mark=(CharWorkStats){.total_us=render_clock(),.pose_us=pose_us,.bounds_us=bounds_us,
        .packet_us=packet_us,.skin_us=ge_skin_us,.bones=posed_bones,
        .bone_uploads=bone_uploads,.skin_draws=ge_skin_batches+ped_shared_skin_draws,
        .skin_vertices=ge_skin_vertices+ped_shared_skin_vertices,
        .cpu_skin_us=cpu_skin_us,.culled=actors_culled};
}
void chars_work_end(unsigned kind,const CharWorkStats *mark)
{
    if(kind>=CHAR_WORK_COUNT)return;
    CharWorkStats now;chars_work_begin(&now);
    CharWorkStats *window=work_window+kind,*total=work_totals+kind;
    window->calls++;total->calls++;
#define CHAR_WORK_DELTA(field) do {unsigned delta=now.field-mark->field;window->field+=delta;total->field+=delta;} while(0)
    CHAR_WORK_DELTA(total_us);CHAR_WORK_DELTA(pose_us);CHAR_WORK_DELTA(bounds_us);
    CHAR_WORK_DELTA(packet_us);CHAR_WORK_DELTA(skin_us);CHAR_WORK_DELTA(bones);
    CHAR_WORK_DELTA(bone_uploads);CHAR_WORK_DELTA(skin_draws);CHAR_WORK_DELTA(skin_vertices);
    CHAR_WORK_DELTA(cpu_skin_us);CHAR_WORK_DELTA(culled);
#undef CHAR_WORK_DELTA
}
void chars_work_profile(CharWorkStats out[CHAR_WORK_COUNT])
{memcpy(out,work_window,sizeof work_window);memset(work_window,0,sizeof work_window);}
void chars_work_totals(CharWorkStats out[CHAR_WORK_COUNT])
{memcpy(out,work_totals,sizeof work_totals);}
/* GE evaluates every palette weight, including zero weights. Bound merged
   palettes to four bones so a crowd does not multiply vertex work by eight.
   An individual authored strip may still need the full eight-bone palette. */
#ifndef CHAR_GE_GROUP_BONES
#define CHAR_GE_GROUP_BONES 4
#endif
static unsigned grouped_draws,weighted_vertices,max_weights;
void chars_group_profile(unsigned out[3])
{out[0]=grouped_draws;out[1]=weighted_vertices;out[2]=max_weights;grouped_draws=weighted_vertices=max_weights=0;}
void chars_bone_profile(unsigned out[2])
{out[0]=bone_uploads;out[1]=bone_reused;bone_uploads=bone_reused=0;}
#include "char_final_ge_normal.inc"
#if CHAR_GE_SKINNING
#include "char_ped_packets.inc"
#endif
static int ped_transform_packet(unsigned char **,unsigned,unsigned,unsigned,const unsigned *,float [][16]);
static void ped_transform_draw_profile(unsigned,unsigned,unsigned);
static int draw_ge_skin_range(Chars *c,const JgcChar *ch,const JgcBatch *b,float mats[][16],GePalette *palette,unsigned limit,unsigned *next,unsigned *hint)
{
#if CHAR_GE_SKINNING
#if JGR_FINAL_GE_NORMAL
    if(!limit && final_ge_normal_draw(c,ch,b,mats,palette))return 1;
#endif
    int final_unlit=final_indexed_ge_unlit(c,b);
    if(c->vert_stride<28 || !b->n_vert || background_planes || material_planes ||
       hud_reciprocal_scale!=1.f || hud_depth_shift!=0.f || (b->flags&2048) ||
       (c->vert_stride==40 && (spray_hud_lighting ||
          (!(b->flags&8) && ((b->flags>>12)&3) && (source_lighting_enabled || scene_light[0]) && !final_unlit))) ||
       (source_lighting_enabled && c->lighting && !(b->flags&8)))return 0;
    unsigned started=render_clock();
    unsigned bones[8],count=0;unsigned char *vertices;
    unsigned first=(unsigned)(b-c->batch),end=first+1,nvert=b->n_vert,nidx=b->n_idx;
    const unsigned short *indices=chars_batch_indices(c,b);
    int grouped=limit>first+1;
    int compact=ped_packet_scope && ped_packet_enabled;
    unsigned flags=(compact ? GE_PACKET_PED:0)|(final_unlit ? GE_PACKET_FINAL_UNLIT:0),source_nvert=nvert,source_nidx=nidx;
    /* Local owners/UVs/colours are immutable for the bank's lifetime.
       Persistent pages survive list reuse; fallback list allocations do
       not. Bone slots retain only THIS actor's current pose; a new actor
       always starts with an empty palette cache. */
    GePacket *cached=packet_slot(c,b,ch->nbones,grouped,compact);
    if(cached && !(cached->flags&(GE_PACKET_FINAL|GE_PACKET_FINAL_NORMAL|GE_PACKET_FINAL_CEL)) && cached->bank==c && cached->batch==b && cached->nbones==ch->nbones && !!cached->end==grouped && !!(cached->flags&GE_PACKET_PED)==compact && !!(cached->flags&GE_PACKET_FINAL_UNLIT)==final_unlit) {
        count=cached->count;vertices=cached->vertices;
        if(grouped)end=cached->end;
        nvert=cached->nvert;nidx=cached->nidx;indices=cached->indices;flags=cached->flags;
        source_nvert=cached->source_nvert;source_nidx=cached->source_nidx;
        for(unsigned i=0;i<count;i++)bones[i]=cached->bones[i];
        if(cached->pages)memset(ge_packet_pinned+cached->page,1,cached->pages);
        packet_reused++;
        if(ped_packet_scope)PED_PACKET_ADD(reused,1);
    } else {
    int map[CHAR_RENDER_MAX_BONES];
    for(unsigned i=0;i<ch->nbones;i++)map[i]=-1;
    /* The ordinary draw order is all outlines, then all bodies. Only
       adjacent submissions with identical material flags may coalesce.
       Opposite-pass batches are not submissions in this pass. */
    unsigned merged=0;nvert=nidx=0;
    for(unsigned at=first;at<(grouped ? limit:first+1);at++) {
        const JgcBatch *part=c->batch+at;
        if((part->flags&8)!=(b->flags&8))continue;
        if(part->flags!=b->flags || part->tex!=b->tex ||
           nvert+part->n_vert>65535 || nidx+part->n_idx>65535 ||
           (final_unlit && !final_indexed_ge_unlit(c,part)))break;
        unsigned previous=count;
        const unsigned char *source=chars_batch_vertices(c,part);
        int fits=1;
        for(unsigned i=0;i<part->n_vert;i++) {
            const SkinVert *v=(const SkinVert *)(source+i*c->vert_stride);
            plan_profile.group_vertices++;
            unsigned bone=v->bone<ch->nbones ? v->bone:0;
            if(map[bone]<0) {
                if(count==(grouped ? CHAR_GE_GROUP_BONES:8)){fits=0;break;}
                map[bone]=(int)count;bones[count++]=bone;
            }
        }
        if(!fits){while(count>previous)map[bones[--count]]=-1;break;}
        nvert+=part->n_vert;nidx+=part->n_idx;end=at+1;merged++;
    }
    if(!merged || (grouped && merged<2)){if(hint)*hint=first+1;packet_us+=render_clock()-started;return 0;}
    source_nvert=nvert;source_nidx=nidx;
    /* GE unsigned-byte weights are normalized by 128. UVs need float
       alignment after the weight bytes; zero padding is not another weight. */
    unsigned weight_bytes=(count+3)&~3u,stride=weight_bytes+sizeof(DrawVert);
    unsigned page=0,pages=0,vertex_bytes=nvert*stride,bytes=vertex_bytes+(grouped || compact ? nidx*2:0);
    vertices=cached ? packet_memory(bytes,&page,&pages,cached):NULL;
    if(!vertices)vertices=sceGuGetMemory(bytes);
    unsigned vo=0,io=0;
    unsigned short *joined=grouped || compact ? (unsigned short *)(vertices+vertex_bytes):NULL;
    for(unsigned at=first;at<end;at++) {
        const JgcBatch *part=c->batch+at;if((part->flags&8)!=(b->flags&8))continue;
        const unsigned char *source=chars_batch_vertices(c,part);
        for(unsigned i=0;i<part->n_vert;i++) {
            const SkinVert *v=(const SkinVert *)(source+i*c->vert_stride);
            unsigned bone=v->bone<ch->nbones ? v->bone:0;
            unsigned char *dst=vertices+(vo+i)*stride;
            unsigned slot=(unsigned)map[bone],weight=128u<<((slot&3)*8);
            unsigned *weights=(unsigned *)dst;
            weights[0]=slot<4 ? weight:0;
            if(weight_bytes==8)weights[1]=slot>=4 ? weight:0;
            DrawVert *out=(DrawVert *)(dst+weight_bytes);
            out->u=v->u;out->v=v->v;out->color=v->color;
            out->x=v->x;out->y=v->y;out->z=v->z;
        }
        if(joined){const unsigned short *src=chars_batch_indices(c,part);
            for(unsigned i=0;i<part->n_idx;i++)joined[io+i]=src[i]+vo;}
        vo+=part->n_vert;io+=part->n_idx;
    }
    if(compact) {
        if(compact_ped_packet(vertices,stride,joined,&nvert,&nidx))flags|=GE_PACKET_STRIP;
        unsigned short *packed=(unsigned short *)(vertices+nvert*stride);
        memmove(packed,joined,nidx*sizeof *packed);joined=packed;
        bytes=nvert*stride+nidx*sizeof *packed;
        trim_ped_packet(page,&pages,bytes);
    }
    if(joined)indices=joined;
#ifdef __PSP__
    /* Persistent storage is cached CPU memory, unlike sceGuGetMemory.
       Publish the completed packet before any draw can reference it. */
    if(pages)sceKernelDcacheWritebackRange(vertices,(bytes+63)&~63u);
#endif
    if(cached) {
        cached->bank=c;cached->batch=b;cached->nbones=ch->nbones;
        cached->count=count;cached->flags=flags;cached->vertices=vertices;
        cached->page=page;cached->pages=pages;
        cached->end=grouped ? end:0;cached->nvert=nvert;cached->nidx=nidx;cached->indices=indices;
        cached->source_nvert=source_nvert;cached->source_nidx=source_nidx;
        for(unsigned i=0;i<count;i++)cached->bones[i]=bones[i];
    }
    packet_prepared++;packet_vertices+=nvert;
    if(ped_packet_scope)PED_PACKET_ADD(built,1);
    }
    /* Never recolour the cached packet: earlier draws (and other actors)
       may still be using it on GE. Only the short pulse needs a frame-owned
       copy. Weight bytes, positions, UVs, indices and bone palettes stay
       identical, so the GPU still performs all eligible skin transforms. */
    if(recovery_active) {
        unsigned weight_bytes=(count+3)&~3u,stride=weight_bytes+sizeof(DrawVert);
        unsigned bytes=nvert*stride;
        unsigned char *coloured=sceGuGetMemory(bytes);
        for(unsigned i=0;i<bytes/4;i++)((unsigned *)coloured)[i]=((const unsigned *)vertices)[i];
        for(unsigned i=0;i<nvert;i++) {
            DrawVert *v=(DrawVert *)(coloured+i*stride+weight_bytes);v->color=recovery_colour(v->color);
        }
        vertices=coloured;
    }
    packet_us+=render_clock()-started;
    int transformed=ped_transform_active && ped_transform_packet(&vertices,count,nvert,source_nvert,bones,mats);
    if(palette->transformed!=(unsigned)transformed){palette->valid=0;palette->transformed=(unsigned)transformed;}
    unsigned transform_submit_started=transformed ? render_clock():0;
    for(unsigned i=0;i<count;i++) {
        if((palette->valid&(1u<<i)) && palette->bone[i]==bones[i]){bone_reused++;continue;}
        palette->valid|=1u<<i;palette->bone[i]=bones[i];bone_uploads++;
        ge_submit_bone(i,transformed ? ped_identity:mats[bones[i]]);
    }
    ge_submit_draw(flags&GE_PACKET_STRIP ? GU_TRIANGLE_STRIP:GU_TRIANGLES,VFMT|GU_WEIGHT_8BIT|GU_WEIGHTS(count),nidx,indices,vertices);
    c->drawn_tris+=source_nidx/3;ge_skin_batches++;ge_skin_vertices+=nvert;
    if(transformed)ped_transform_draw_profile(nidx,source_nidx/3,render_clock()-transform_submit_started);
    grouped_draws+=grouped;weighted_vertices+=nvert*count;if(count>max_weights)max_weights=count;
    if(ped_packet_scope) {
        PED_PACKET_ADD(draws,1);PED_PACKET_ADD(strip_draws,!!(flags&GE_PACKET_STRIP));
        PED_PACKET_ADD(source_vertices,source_nvert);PED_PACKET_ADD(vertices,nvert);
        PED_PACKET_ADD(source_indices,source_nidx);PED_PACKET_ADD(indices,nidx);
        PED_PACKET_ADD(triangles,source_nidx/3);PED_PACKET_ADD(fallbacks,compact && !(flags&GE_PACKET_STRIP));
    }
    if(next)*next=end;
    if(hint)*hint=end;
    return 1;
#else
    (void)c;(void)ch;(void)b;(void)mats;(void)palette;(void)limit;(void)next;(void)hint;return 0;
#endif
}
static int draw_ge_skin(Chars *c,const JgcChar *ch,const JgcBatch *b,float mats[][16],GePalette *palette)
{return draw_ge_skin_range(c,ch,b,mats,palette,0,NULL,NULL);}
/* Loaded/fragmented banks keep the source hierarchy and batches immutable.
   A group is only a cached GE packet; CPU lighting/clipping still sees each
   original batch. Reject unusual non-contiguous hierarchy ranges. */
static unsigned ge_group_limit(const Chars *c,const JgcChar *ch)
{
#if CHAR_GE_SKINNING && !defined(JGR_CHAR_GROUP_REFERENCE)
    if(c->vert_stride<28 || !ch->nbones)return 0;
    unsigned end=0;int found=0;
    for(unsigned i=0;i<ch->nbones;i++) {
        const JgcBone *bone=c->bones+ch->bone0+i;
        if(!bone->nbatch)continue;
        if(found && bone->batch0!=end)return 0;
        if(bone->batch0>c->h->n_batches || bone->nbatch>c->h->n_batches-bone->batch0)return 0;
        end=bone->batch0+bone->nbatch;found=1;
    }
    return end;
#else
    (void)c;(void)ch;return 0;
#endif
}
/* Keep clipped material/background polygons in ordered packets. Positions
   and UVs stay in world space; interpolation and depth/blend state remain
   unchanged. One VFPU scope covers this material batch, including sky. */
static unsigned draw_clipped_material(const DrawVert *v,const unsigned short *idx,unsigned count,
                                     const float planes[6][4],unsigned clip_mask)
{
    DrawVert packet[192];unsigned used=0,total=0;
    unsigned vectors=0,stamp=vfpu_profile_clip_begin();
#if CLIP_MATH_VFPU
    RenderMathState state;render_math_begin(&state);
#endif
    for(unsigned j=0;j+2<count;j+=3) {
        JgsVert tri[3],out[27];
        for(unsigned k=0;k<3;k++)memcpy(tri+k,v+(idx ? idx[j+k]:j+k),sizeof *tri);
        unsigned n=skyclip_triangle_masked_inner(tri,planes,clip_mask,out,&vectors);
        if(used+n>192) {
            ge_submit_draw(GU_TRIANGLES,VFMT&~GU_INDEX_16BIT,used,NULL,publish_vertices(packet,used));
            total+=used;used=0;
        }
        memcpy(packet+used,out,n*sizeof *out);used+=n;
    }
    if(used)ge_submit_draw(GU_TRIANGLES,VFMT&~GU_INDEX_16BIT,used,NULL,publish_vertices(packet,used));
#if CLIP_MATH_VFPU
    render_math_end(&state);
#endif
    vfpu_profile_clip_end(stamp,vectors);
    return (total+used)/3;
}
static unsigned final_floor_scalar_code(const DrawVert *v)
{
    if(!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z))return 63;
    unsigned code=0;
    for(unsigned p=0;p<6;p++) {
        const float *q=final_floor_guard[p];
        if(q[0]*v->x+q[1]*v->y+q[2]*v->z+q[3]<0.f)code|=1u<<p;
    }
    return code;
}
static void final_floor_safe_run(const DrawVert *v,const unsigned short *idx,unsigned first,
                                unsigned count,unsigned vertices,const DrawVert **resident)
{
    if(idx) {
        if(!*resident)*resident=v==cached_skin_vertices ? publish_vertices(v,vertices):v;
        ge_submit_draw(GU_TRIANGLES,VFMT,count,idx+first,*resident);
    } else ge_submit_draw(GU_TRIANGLES,VFMT&~GU_INDEX_16BIT,count,NULL,publish_vertices(v+first,count));
}
static inline void final_floor_math_open(RenderMathScope *scope)
{
#if CLIP_MATH_VFPU || defined(JGR_TEST_FINAL_FLOOR_SCOPE)
    if(!render_math_scope_active(scope))render_math_scope_begin(scope);
#else
    (void)scope;
#endif
}
/* Bounds may cross a guard plane while most of a floor remains safe.
   Keep safe indexed runs, and publish only clipped unsafe polygons. All
   clipped storage is frame-owned; the code scratch is never submitted. */
static unsigned draw_floor_material(const DrawVert *v,const unsigned short *idx,unsigned count,unsigned vertices)
{
    if(!count)return 0;
    RenderMathScope math;math.active=0;
    unsigned vectors=0,stamp=0,profile_active=0;
    if(vertices<=CHAR_CPU_SKIN_VERTICES) {
        final_floor_math_open(&math);
#if CLIP_MATH_VFPU
        /* Keep the VFPU inline-asm operand register-based. A folded global
           symbol would produce an invalid "16 + %lo(symbol)" address. */
        const ClipMathPlanes *packed=&final_floor_packed;
        __asm__ volatile("" : "+r"(packed));clip_math_load(packed);
#endif
        for(unsigned i=0;i<vertices;i++)final_floor_codes[i]=
            (!isfinite(v[i].x) || !isfinite(v[i].y) || !isfinite(v[i].z)) ? 63:
            clip_math_point_code(&final_floor_packed,&v[i].x,63);
    }
    DrawVert packet[192];unsigned used=0,total=0,first=0,run=0;
    const DrawVert *resident=NULL;
    for(unsigned j=0;j+2<count;j+=3) {
        unsigned indices[3],any=0,all=63;
        for(unsigned k=0;k<3;k++) {
            unsigned at=idx ? idx[j+k]:j+k;indices[k]=at;
            unsigned code=vertices<=CHAR_CPU_SKIN_VERTICES ? final_floor_codes[at]:final_floor_scalar_code(v+at);
            any|=code;all&=code;
        }
        final_clip_profile.tested_triangles++;
        if(!any) {
            if(used){render_math_scope_end(&math);ge_submit_draw(GU_TRIANGLES,VFMT&~GU_INDEX_16BIT,used,NULL,publish_vertices(packet,used));total+=used;used=0;}
            if(!run)first=j;
            run+=3;continue;
        }
        if(run){render_math_scope_end(&math);final_floor_safe_run(v,idx,first,run,vertices,&resident);total+=run;run=0;}
        final_clip_profile.clipped_triangles++;
        if(all)continue;
        JgsVert tri[3],out[27];for(unsigned k=0;k<3;k++)memcpy(tri+k,v+indices[k],sizeof *tri);
        if(!profile_active){stamp=vfpu_profile_clip_begin();profile_active=1;}
        final_floor_math_open(&math);
        unsigned n=(unsigned)skyclip_triangle_inner(tri,final_floor_view,out,&vectors);
        if(used+n>192) {render_math_scope_end(&math);ge_submit_draw(GU_TRIANGLES,VFMT&~GU_INDEX_16BIT,used,NULL,publish_vertices(packet,used));total+=used;used=0;}
        memcpy(packet+used,out,n*sizeof *out);used+=n;
    }
    render_math_scope_end(&math);
    if(run){final_floor_safe_run(v,idx,first,run,vertices,&resident);total+=run;}
    if(used){ge_submit_draw(GU_TRIANGLES,VFMT&~GU_INDEX_16BIT,used,NULL,publish_vertices(packet,used));total+=used;}
    if(profile_active)vfpu_profile_clip_end(stamp,vectors);
    final_clip_profile.output_triangles+=total/3;return total/3;
}
static unsigned draw_selected_material(const DrawVert *v,const unsigned short *idx,unsigned count,
                                      unsigned vertices,const float planes[6][4],unsigned mask)
{
    if(planes==final_floor_view)return draw_floor_material(v,idx,count,vertices);
    return draw_clipped_material(v,idx,count,planes,mask);
}
typedef struct {float depth;unsigned triangle;} MaterialDepth;
static int material_depth_compare(const void *a,const void *b)
{
    const MaterialDepth *x=a,*y=b;
    if(x->depth!=y->depth)return x->depth>y->depth ? -1:1;
    return (x->triangle>y->triangle)-(x->triangle<y->triangle);
}
static const unsigned short *material_depth_indices(const DrawVert *v,const unsigned short *indices,unsigned count)
{
    /* PVR sorts translucent faces; the GE uses submission order. Sort the
       posed faces of small two-sided doors, including reflected/open leaves.
       Eye position is a constant offset and does not affect this ordering. */
    unsigned triangles=count/3;
    MaterialDepth *depth=sceGuGetMemory(triangles*sizeof *depth);
    unsigned short *ordered=sceGuGetMemory(count*sizeof *ordered);
    for(unsigned t=0;t<triangles;t++){
        float sum[3]={0,0,0};
        for(unsigned k=0;k<3;k++){
            const DrawVert *p=v+indices[t*3+k];sum[0]+=p->x;sum[1]+=p->y;sum[2]+=p->z;
        }
        depth[t]=(MaterialDepth){sum[0]*material_sort_forward[0]+sum[1]*material_sort_forward[1]+sum[2]*material_sort_forward[2],t};
    }
    qsort(depth,triangles,sizeof *depth,material_depth_compare);
    for(unsigned t=0;t<triangles;t++)memcpy(ordered+t*3,indices+depth[t].triangle*3,3*sizeof *ordered);
    return ordered;
}
#ifdef JGR_CEL_MIXED_REFERENCE
static int cel_mixed_enabled;
#else
static int cel_mixed_enabled=1;
#endif
#ifdef JGR_CEL_SHARED_REFERENCE
static int cel_shared_cut_enabled;
#else
static int cel_shared_cut_enabled=1;
#endif
/* A finite mixed-sign triangle crosses exactly two directed edges. The
   reference visits those same edges once for each side, computing identical
   positions/UVs twice. Compute each once, retain the two side colours, and
   reference the already-shaded original vertices for the remaining corners.
   The two polygons use the reference's exact order and triangulation. */
#include "source_cel_shared.h"
#ifndef JGR_FINAL_CEL_VFPU_EMIT
#define JGR_FINAL_CEL_VFPU_EMIT 0
#endif
#if JGR_FINAL_CEL_VFPU_EMIT
static int final_cel_vfpu_emit_enabled=1;
#endif
void chars_final_cel_vfpu_mode(int enabled)
{
#if JGR_FINAL_CEL_VFPU_EMIT
    final_cel_vfpu_emit_enabled=(unsigned)enabled<=2u ? enabled:0;
#else
    (void)enabled;
#endif
}
#ifndef JGR_CEL_DIRECT_INDEXED
#define JGR_CEL_DIRECT_INDEXED 1
#endif
static unsigned cel_shared_triangle(const DrawVert tri[3],const float shade[3],const unsigned short original[3],
    DrawVert boundary[4],unsigned short ordered[9],unsigned first,const float rgb[2][3],SourceCelPalette *colors)
{
    return source_cel_shared_triangle(tri,shade,original,boundary,ordered,first,rgb,
        recovery_active ? recovery_offset:NULL,colors);
}
/* Final's alternating lit/unlit runs otherwise submit an indexed draw for
   each uncut run and an expanded draw for each cut run. Keep the original
   shared vertices, append only the cut output, and index both in source
   order. This changes packets, never the cel boundary or material state. */
static int draw_cel_mixed(Chars *c,const JgcBatch *b,DrawVert *v,const unsigned short *indices,
                          const float rgb[2][3],SourceCelPalette *colors)
{
    if(!cel_mixed_enabled || !source_lighting_enabled || v!=cached_skin_vertices || !b->n_idx)return 0;
    unsigned cuts=0,old_draws=0,used=0;int uncut_run=0;
    for(unsigned j=0;j<b->n_idx;j+=3) {
        int side=cel_values[indices[j]]>0;
        int cut=side!=(cel_values[indices[j+1]]>0) || side!=(cel_values[indices[j+2]]>0);
        if(!cut) {
            if(used){old_draws++;used=0;}
            uncut_run=1;continue;
        }
        if(uncut_run){old_draws++;uncut_run=0;}
        if(used+9>192){old_draws++;used=0;}
        used+=9;cuts++;
    }
    old_draws+=!!used+!!uncut_run;
    unsigned nvertices=b->n_vert+cuts*(cel_shared_cut_enabled ? 4u:9u),nindices=b->n_idx+cuts*6u;
    unsigned vertex_bytes=nvertices*sizeof *v,index_bytes=(nindices*2u+15u)&~15u;
    /* Each old draw used at least four GE words, the new indexed draw six.
       Shared cuts also replace nine expanded vertices with four boundary
       vertices. Credit that real 120-byte saving per cut before charging
       the 16 KiB net-extra budget. Without this credit, memory-saving
       packets unnecessarily fall back to repeated cuts and tiny draws.
       Both paths publish the same source prefix; omitted old allocation
       padding is additional slack, not part of the saving claimed here. */
    unsigned saved_commands=old_draws>=2 ? old_draws*16u-24u:0;
    unsigned saved_vertices=cel_shared_cut_enabled ? cuts*5u*sizeof *v:0;
    unsigned saved=saved_commands+saved_vertices;
    unsigned extra=index_bytes+32u>saved ? index_bytes+32u-saved:0;
    if(old_draws<4 || nvertices>65535u || nindices>65535u ||
       extra>16u*1024u-cel_mixed_frame_bytes || vertex_bytes+index_bytes>64u*1024u)return 0;
#if defined(__PSP__) || defined(JGR_TEST_CEL_LIST)
    if(vertex_bytes+index_bytes+128u+64u*1024u>gfx_list_available())return 0;
#endif
    /* Resolve the same boundary values in the same order before allocating.
       Finite mixed signs produce exactly nine vertices, including source
       zero-edge degenerates. Unusual inputs retain the reference emitter. */
#if JGR_FINAL_CEL_RESOLVED_PREPASS
    int resolved_prepass=final_cel_resolved_prepass_enabled && cel_prepared_all_resolved &&
        cel_source.bank==c && cel_source.batch==b;
#ifdef JGR_TEST_CEL_RESOLVED_PREPASS
    if(resolved_prepass){cel_prepass_skipped_batches++;cel_prepass_skipped_cuts+=cuts;}
#endif
    if(!resolved_prepass)
#endif
    for(unsigned j=0;j<b->n_idx;j+=3) {
        int side=cel_values[indices[j]]>0;
        if(side==(cel_values[indices[j+1]]>0) && side==(cel_values[indices[j+2]]>0))continue;
        for(unsigned k=0;k<3;k++) {
#ifdef JGR_TEST_CEL_RESOLVED_PREPASS
            cel_prepass_checked_corners++;
#endif
            unsigned at=indices[j+k];int sign=cel_values[at]>0;
            float value=cel_boundary_value(at);
            if(!isfinite(value) || (value>0)!=sign)return 0;
        }
    }
    /* One allocation owns both streams until the existing frame fence.
       The prefix is padded to align its 16-bit index stream without a
       second allocation or mutable cached GPU data. */
    unsigned aligned_vertices=(vertex_bytes+15u)&~15u;
    unsigned char *memory=sceGuGetMemory(aligned_vertices+index_bytes);
    if(!memory)return 0;
    DrawVert *resident=(DrawVert *)memory;
    unsigned short *ordered=(unsigned short *)(memory+aligned_vertices);
#if JGR_FINAL_CEL_VFPU_EMIT
    SourceCelVfpuScope cel_math;cel_math.active=0;
    int vector_scope=JGR_CEL_DIRECT_INDEXED && final_cel_vfpu_emit_enabled && cel_shared_cut_enabled &&
        c->indexed_prepare && !c->lighting && !recovery_active && !scene_light[0] && !scene_light[1];
    int vector_emit=vector_scope && final_cel_vfpu_emit_enabled==1;
    if(vector_scope) {
        source_cel_vfpu_begin(&cel_math);
        source_cel_vfpu_copy(resident,v,b->n_vert*sizeof *v);
    } else
#endif
    for(unsigned i=0;i<b->n_vert*sizeof *v/4u;i++) {
        unsigned word;memcpy(&word,(const unsigned char *)v+4*i,4);((unsigned *)resident)[i]=word;
    }
    unsigned next=b->n_vert,out=0;
    const unsigned char *source=chars_batch_vertices(c,b);
    for(unsigned j=0;j<b->n_idx;j+=3) {
        int side=cel_values[indices[j]]>0;
        if(side==(cel_values[indices[j+1]]>0) && side==(cel_values[indices[j+2]]>0)) {
            for(unsigned k=0;k<3;k++)ordered[out++]=indices[j+k];
            continue;
        }
#if JGR_CEL_DIRECT_INDEXED
        if(cel_shared_cut_enabled) {
            unsigned mask=(cel_values[indices[j]]>0)|((cel_values[indices[j+1]]>0)<<1)|((cel_values[indices[j+2]]>0)<<2);
            unsigned at0=indices[j+source_cel_shared_edge0(mask)],at1=indices[j+source_cel_shared_edge1(mask)];
            const SkinVert *src0=(const SkinVert *)(source+at0*c->vert_stride),*src1=(const SkinVert *)(source+at1*c->vert_stride);
            unsigned material0=c->lighting ? c->lighting[b->vert_off+at0].material:src0->color;
            unsigned material1=c->lighting ? c->lighting[b->vert_off+at1].material:src1->color;
            material0=(v[at0].color&0xff000000u)|(material0&0xffffffu);
            material1=(v[at1].color&0xff000000u)|(material1&0xffffffu);
#if JGR_FINAL_CEL_VFPU_EMIT
            if(vector_emit)next+=source_cel_shared_indexed_vfpu(v,cel_values,indices+j,mask,material0,material1,resident+next,ordered+out,next,rgb,NULL,colors);
            else
#endif
                next+=source_cel_shared_indexed(v,cel_values,indices+j,mask,material0,material1,resident+next,ordered+out,next,
                    rgb,recovery_active ? recovery_offset:NULL,colors);
            out+=9;continue;
        }
#endif
        DrawVert tri[3];float values[3];
        for(unsigned k=0;k<3;k++) {
            unsigned at=indices[j+k];tri[k]=v[at];values[k]=cel_values[at];
            const SkinVert *src=(const SkinVert *)(source+at*c->vert_stride);
            unsigned material=c->lighting ? c->lighting[b->vert_off+at].material:src->color;
            tri[k].color=(tri[k].color&0xff000000u)|(material&0xffffffu);
        }
        if(cel_shared_cut_enabled) {
            next+=cel_shared_triangle(tri,values,indices+j,resident+next,ordered+out,next,rgb,colors);out+=9;
        } else {
            unsigned count=source_cel_triangle_cached(tri,values,resident+next,rgb,recovery_active ? recovery_offset:NULL,colors);
            for(unsigned k=0;k<count;k++)ordered[out++]=(unsigned short)(next++);
        }
    }
#if JGR_FINAL_CEL_VFPU_EMIT
    source_cel_vfpu_end(&cel_math);
#endif
    final_cel_packet_capture(c,b,resident,ordered,next,out,cuts);
    ge_submit_draw(GU_TRIANGLES,VFMT,out,ordered,resident);c->drawn_tris+=out/3;
    cel_cut_triangles+=cuts;cel_mixed_batches++;cel_mixed_saved_draws+=old_draws-1u;
    cel_mixed_frame_bytes+=extra;cel_mixed_extra_bytes+=extra;
    if(cel_shared_cut_enabled)cel_shared_cut_triangles+=cuts;
    return 1;
}
static void draw_skin(Chars *c,const JgcBatch *b,DrawVert *v,const unsigned short *indices)
{
    if(background_planes) {
#ifdef JGR_SKY_BATCH_REFERENCE
        for(unsigned j=0;j+2<b->n_idx;j+=3) {
            JgsVert tri[3],out[27];
            for(int k=0;k<3;k++)memcpy(tri+k,v+indices[j+k],sizeof *tri);
            int n=skyclip_triangle(tri,background_planes,out);if(!n)continue;
            void *dest=publish_vertices(out,n);
            ge_submit_draw(GU_TRIANGLES,VFMT & ~GU_INDEX_16BIT,n,NULL,dest);c->drawn_tris+=n/3;
        }
#else
        c->drawn_tris+=draw_clipped_material(v,indices,b->n_idx,background_planes,63);
#endif
        return;
    }
    if(!cel_values) {
        if(material_planes) {
            c->drawn_tris+=draw_selected_material(v,indices,b->n_idx,b->n_vert,material_planes,material_clip_mask);return;
        }
        if(v==cached_skin_vertices)v=publish_vertices(v,b->n_vert);
        ge_submit_draw(GU_TRIANGLES,VFMT,b->n_idx,indices,v);c->drawn_tris+=b->n_idx/3;return;
    }
    /* Small batches bound the largest GU allocation; all storage remains
       resident until this display list has completed. */
    float rgb[2][3]={{254.f/255.f,180.f/255.f,85.f/255.f},{0,112.f/255.f,173.f/255.f}};
    if(scene_light[1])for(int side=0;side<2;side++)for(int j=0;j<3;j++)rgb[side][j]=((scene_light[1]->color[side]>>(16-j*8))&255)/255.f;
    SourceCelPalette colors={0};
#ifndef JGR_CEL_INDEX_REFERENCE
    /* Keep shared vertices indexed if no triangle crosses the light edge.
       Coloring each unique vertex once gives precisely the expanded path's
       colors/UVs/positions, without copying every triangle into GU memory. */
    int split=0;cel_batches++;
    for(unsigned j=0;j+2<b->n_idx;j+=3) {
        int side=cel_values[indices[j]]>0;
        if(side!=(cel_values[indices[j+1]]>0) || side!=(cel_values[indices[j+2]]>0)){split=1;break;}
    }
    if(!split || !material_planes) {
        for(unsigned i=0;i<b->n_vert;i++)v[i].color=source_cel_colour(v[i].color,cel_values[i]>0,rgb,recovery_active ? recovery_offset:NULL,&colors);
        if(material_planes)c->drawn_tris+=draw_selected_material(v,indices,b->n_idx,b->n_vert,material_planes,material_clip_mask);
        else {
            if(split && draw_cel_mixed(c,b,v,indices,rgb,&colors))return;
            DrawVert *resident=v==cached_skin_vertices ? publish_vertices(v,b->n_vert):v;
            unsigned first=0,used=0;DrawVert expanded[192];
            for(unsigned j=0;j<b->n_idx;j+=3) {
                int side=cel_values[indices[j]]>0;
                int cut=side!=(cel_values[indices[j+1]]>0) || side!=(cel_values[indices[j+2]]>0);
                if(!cut) {
                    if(used){ge_submit_draw(GU_TRIANGLES,VFMT&~GU_INDEX_16BIT,used,NULL,publish_vertices(expanded,used));c->drawn_tris+=used/3;used=0;}
                    continue;
                }
                if(j>first){ge_submit_draw(GU_TRIANGLES,VFMT,j-first,indices+first,resident);c->drawn_tris+=(j-first)/3;}
                DrawVert tri[3];float values[3];cel_cut_triangles++;
                for(unsigned k=0;k<3;k++) {
                    unsigned index=indices[j+k];tri[k]=v[index];values[k]=cel_boundary_value(index);
                    const SkinVert *src=(const SkinVert *)(chars_batch_vertices(c,b)+index*c->vert_stride);
                    unsigned material=c->lighting ? c->lighting[b->vert_off+index].material:src->color;
                    /* Retain a material-pass alpha fade, if present. */
                    tri[k].color=(tri[k].color&0xff000000u)|(material&0xffffffu);
                }
                if(used+9>192){ge_submit_draw(GU_TRIANGLES,VFMT&~GU_INDEX_16BIT,used,NULL,publish_vertices(expanded,used));c->drawn_tris+=used/3;used=0;}
                used+=source_cel_triangle_cached(tri,values,expanded+used,rgb,recovery_active ? recovery_offset:NULL,&colors);
                first=j+3;
            }
            if(used){ge_submit_draw(GU_TRIANGLES,VFMT&~GU_INDEX_16BIT,used,NULL,publish_vertices(expanded,used));c->drawn_tris+=used/3;}
            if(b->n_idx>first){ge_submit_draw(GU_TRIANGLES,VFMT,b->n_idx-first,indices+first,resident);c->drawn_tris+=(b->n_idx-first)/3;}
        }
        return;
    }
#endif
    for(unsigned first=0;first<b->n_idx;first+=192) {
        unsigned end=first+192;if(end>b->n_idx)end=b->n_idx;
        /* Most polygons do not cross the light boundary. Reserve only the
           actual clipped output, not nine vertices for every source tri:
           that worst-case reservation overflowed the PSP display list. */
        DrawVert expanded[192*3];unsigned n=0;
        for(unsigned j=first;j+2<end;j+=3) {
            DrawVert tri[3];float values[3];
            for(unsigned k=0;k<3;k++){unsigned index=indices[j+k];tri[k]=v[index];values[k]=cel_values[index];}
            if((values[0]>0)!=(values[1]>0) || (values[1]>0)!=(values[2]>0))
                for(unsigned k=0;k<3;k++)values[k]=cel_boundary_value(indices[j+k]);
            n+=source_cel_triangle_cached(tri,values,expanded+n,rgb,recovery_active ? recovery_offset:NULL,&colors);
        }
        if(n) {
            if(material_planes){c->drawn_tris+=draw_selected_material(expanded,NULL,n,n,material_planes,material_clip_mask);continue;}
            DrawVert *resident=publish_vertices(expanded,n);
            ge_submit_draw(GU_TRIANGLES,VFMT&~GU_INDEX_16BIT,n,NULL,resident);
        }
        c->drawn_tris+=n/3;
    }
}

static unsigned material_applied,material_reused;
void chars_material_profile(unsigned out[2])
{out[0]=material_applied;out[1]=material_reused;material_applied=material_reused=0;}
static void bind_tex_image(Chars *c,int t)
{
    const JgcTex *tx=c->tex+t;
    sceGuClutLoad(32,chars_texture_address(c,tx,tx->off_clut));
    lod_bind_image(c,(unsigned)t);
}
static void bind_tex(Chars *c, int t)
{
    JgcTex *tx = &c->tex[t];
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0); sceGuClutLoad(32, chars_texture_address(c,tx,tx->off_clut));
    sceGuTexMode(GU_PSM_T8, 0, 0, 1); lod_bind_image(c,(unsigned)t);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA); sceGuTexFilter(GU_LINEAR, GU_LINEAR); sceGuTexWrap(GU_REPEAT, GU_REPEAT);
}

int getenv_nocull = 0, getenv_nohull = 0, getenv_hullcw = 0;   /* debug toggles (files nocull / nohull / hullcw) */
/* 06dd7c class-3 actors write (fade-depth)/(far-fade) to the material alpha
   and draw additively. Constant GE blend factors leave texture alpha tests,
   bone packets and vertex colours untouched; every state_begin and opaque
   material batch between chars_fade_begin/end applies the same factors. */
static void fade_blend(void)
{
    if(fade_active){sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,fade_colour,0xffffffu-fade_colour);}
    else sceGuDisable(GU_BLEND);
}
void chars_fade_begin(float opacity)
{
    if(!(opacity<1.f)){fade_active=0;return;}
    unsigned alpha=opacity>0.f ? (unsigned)(opacity*255.f+.5f):0u;
    fade_colour=alpha*0x010101u;fade_active=1;
}
void chars_fade_end(void){if(fade_active){fade_active=0;sceGuDisable(GU_BLEND);}}
static void state_begin(void)
{
    sceGuEnable(GU_TEXTURE_2D); sceGuEnable(GU_DEPTH_TEST); sceGuDepthMask(GU_FALSE);
    fade_blend(); sceGuEnable(GU_ALPHA_TEST); sceGuAlphaFunc(GU_GREATER, 0x20, 0xff);
    sceGuEnable(GU_CULL_FACE); sceGuFrontFace(GU_CCW);   /* PLAY skaters are wound the other way round from the stage banks; their black outline hulls (no-UV strips, near-black diffuse) must show back faces only */
}
static void state_end(void)
{
    sceGuDisable(GU_ALPHA_TEST); sceGuEnable(GU_CULL_FACE); sceGuFrontFace(GU_CW); sceGuDisable(GU_TEXTURE_2D);
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();
}
static int cur_tex = -1;
int chars_bind_material_texture(Chars *c,unsigned texture)
{
    if(!c || !c->h || texture>=c->h->n_tex)return 0;
    bind_tex(c,texture);cur_tex=-1;return 1;
}
static int posed_actor_visible(const Chars *c,const JgcChar *ch,float mats[][16],int allow_occlusion)
{
    if(!world_planes || !c->render_radius)return 1;
    float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
    int found=0;
    /* Transform owner-local cubes through the actual animated hierarchy.
       This includes limb extension, nonuniform scale and outline hulls.
       No fixed standing sphere or distance cutoff can eat visible limbs. */
    for(unsigned bi=0;bi<ch->nbones;bi++) {
        float radius=c->render_radius[ch->bone0+bi];if(radius<0)continue;
        found=1;
        for(int axis=0;axis<3;axis++) {
            const float *m=mats[bi]+axis*4;
            float extent=radius*(fabsf(m[0])+fabsf(m[1])+fabsf(m[2]));
            extent+=.01f+(fabsf(m[3])+extent)*1e-5f;
            if(!isfinite(extent) || !isfinite(m[3]))return 1;
            float lo=m[3]-extent,hi=m[3]+extent;
            if(lo<low[axis])low[axis]=lo;
            if(hi>high[axis])high[axis]=hi;
        }
    }
    if(!found)return 1;
    for(int i=0;i<6;i++) {
        const float *p=world_planes[i];float d=p[3];
        for(int axis=0;axis<3;axis++)d+=p[axis]*(p[axis]<0 ? low[axis]:high[axis]);
        if(d<-.01f)return 0;
    }
    if(allow_occlusion && ped_occlusion_query) {
        float center[3],extent[3];
        for(unsigned axis=0;axis<3;axis++) {
            /* A rounded midpoint must never shrink the bound. Include the
               larger endpoint distance plus a scale-aware rounding guard. */
            center[axis]=low[axis]*.5f+high[axis]*.5f;
            float left=center[axis]-low[axis],right=high[axis]-center[axis];
            extent[axis]=left>right ? left:right;
            extent[axis]+=.01f+(fabsf(center[axis])+extent[axis])*1e-5f;
            if(!isfinite(center[axis]) || !isfinite(extent[axis]) || extent[axis]<0)return 1;
        }
        if(ped_occlusion_query(ped_occlusion_context,center,extent))return 0;
    }
    return 1;
}
int chars_actor_bounds(const Chars *c,int ci,const float base[16],float center[3],float extent[3])
{
    if(!c || !c->h || (unsigned)ci>=c->h->n_chars || !base || !center || !extent ||
       !c->motion_radius || c->motion_owner || base[12]!=0.f || base[13]!=0.f || base[14]!=0.f || base[15]!=1.f)return 0;
    float radius=c->motion_radius[ci];if(!isfinite(radius) || radius<0.f)return 0;
    for(int axis=0;axis<3;axis++) {
        const float *m=base+axis*4;
        extent[axis]=radius*sqrtf(m[0]*m[0]+m[1]*m[1]+m[2]*m[2])+.05f;
        center[axis]=m[3];
        if(!isfinite(extent[axis]) || !isfinite(center[axis]))return 0;
    }
    return 1;
}
static int unposed_actor_visible(const Chars *c,unsigned ci,const float base[16])
{
    float center[3],extent[3];
    if(!world_planes || !chars_actor_bounds(c,(int)ci,base,center,extent))return 1;
    for(int i=0;i<6;i++) {
        const float *p=world_planes[i];float d=p[3];
        for(int axis=0;axis<3;axis++)d+=p[axis]*center[axis]+fabsf(p[axis])*extent[axis];
        if(d<-.05f)return 0;
    }
    return 1;
}
int chars_sphere_visible(const float center[3],float radius)
{
    if(!world_planes || !isfinite(radius) || radius<0)return 1;
    for(int j=0;j<3;j++)if(!isfinite(center[j]))return 1;
    for(int i=0;i<6;i++) {
        const float *p=world_planes[i];float d=p[3];
        for(int j=0;j<3;j++)d+=p[j]*center[j]+fabsf(p[j])*radius;
        if(d<-.05f)return 0;
    }
    return 1;
}
static const float *g_xf;   /* full root matrix override (chars_draw_one_xf) */
static void material_guard_codes(const Chars *,const JgcChar *,float [][16],const float [6][4],unsigned char *,unsigned char *);
static int posed_batch_visible(const Chars *c,const JgcChar *ch,const JgcBatch *batch,const unsigned char *outside)
{
    unsigned any,all;batch_owner_codes(c,ch,batch,NULL,outside,&any,&all);
    return !all;
}
int chars_actor_visible(const Chars *c,int ci,const float root[16])
{return !c || !c->h || (unsigned)ci>=c->h->n_chars ? 1:unposed_actor_visible(c,(unsigned)ci,root);}
static const CharPose *g_pose;
static int pose_span_enabled=1;
void chars_pose_span_mode(int enabled){pose_span_enabled=!!enabled;}
#include "char_ped_shared.inc"
static void draw_posed(Chars *c, unsigned ci, float x, float y, float z, float yawrad, int mi, float frame, int mi2, float frame2, float weight, int overlay, float overlay_frame, float overlay_weight)
{
    unsigned posed_started=render_clock();
    const float (*saved_material_planes)[4]=material_planes;
    int floor_clip=final_floor_bank==c && ci<5 && !material_planes && c->vert_stride>=28;
    memset(cel_local_valid,0,sizeof cel_local_valid);
    /* STAGELAST exceeds 64 nodes; DEMO sets reach 381. The 64-bone player
       attachment limit is separate from these full scene hierarchies.
       Scratch is render-thread-only, like the surrounding GU state. */
    static float mats[CHAR_RENDER_MAX_BONES][16] __attribute__((aligned(16)));
    GePalette palette={0};
    JgcChar *ch = &c->chars[ci];
    final_indexed_begin(c,ch);
    if (ch->nbones > CHAR_RENDER_MAX_BONES) return;
    {
        unsigned started=render_clock();
        float base[16] __attribute__((aligned(16))), yawm[16];
        if (g_xf) memcpy(base, g_xf, 64);
        else { m_ident(base); base[3] = x; base[7] = y; base[11] = z;
        float cy = cosf(yawrad), sy = sinf(yawrad);
        m_ident(yawm); yawm[0] = cy; yawm[2] = sy; yawm[8] = -sy; yawm[10] = cy; chars_affine_multiply(base, base, yawm); }
        if(!g_pose && overlay<0 && (mi2<0 || (weight>=0.f && weight<=1.f)) && !unposed_actor_visible(c,ci,base)) {
            bounds_us+=render_clock()-started;actors_culled++;return;
        }
        /* Original dense pedestrian walk (0ba8d8) consumes each node's pose
           directly. ME already built these locals: acquire the whole span
           once, without copying unused channels or looking up every bone.
           Blended/custom poses retain their channel-based path. */
        const float *locals=pose_span_enabled && !g_pose && mi2<0 && overlay<0 ?
            chars_pose_locals(c,ch,mi,frame):NULL;
        RenderMathScope math;render_math_scope_begin(&math);
        if(locals)for(unsigned bi=0;bi<ch->nbones;bi++) {
            const JgcBone *b=c->bones+ch->bone0+bi;
            m_mul(mats[bi],b->parent>=0 ? mats[b->parent-(int)ch->bone0]:base,locals+bi*16);
        }
        else for (unsigned bi = 0; bi < ch->nbones; bi++) {
            JgcBone *b = &c->bones[ch->bone0 + bi]; CharPose pose;
            if (g_pose) pose=g_pose[bi];else chars_sample_pose(c,ch,mi,frame,bi,&pose);
            if (mi2>=0) { CharPose other; chars_sample_pose(c,ch,mi2,frame2,bi,&other); chars_blend_pose(&pose,&other,weight); }
            if (overlay>=0) chars_overlay_pose(c,ch,overlay,overlay_frame,bi,overlay_weight,&pose);
            float local[16] __attribute__((aligned(16)));
            /* Keep the old path selectable for a frozen hardware A/B.
               A failed span lookup already proves no local is ready. */
            const float *prepared=!pose_span_enabled && !g_pose && mi2<0 && overlay<0 ?
                chars_pose_local(c,ch,mi,frame,bi):NULL;
            if(!prepared){node_matrix(local,pose.ev,pose.pos,pose.ang,pose.scl);prepared=local;}
            m_mul(mats[bi], b->parent >= 0 ? mats[b->parent - (int)ch->bone0] : base, prepared);
        }
        /* Ordinary native pedestrians own these registers until their point
           transform finishes. All other renderers retain the original scope. */
        int shared_math=ped_math_enabled && ped_packet_scope && ped_shared_enabled &&
            !ped_transform_mode && c==ped_native_bank && ped_native_eligible(c) &&
            !g_pose && mi2<0 && overlay<0;
        if(!shared_math)render_math_scope_end(&math);
        pose_us+=render_clock()-started;posed_bones+=ch->nbones;
        started=render_clock();int visible=posed_actor_visible(c,ch,mats,
            ped_packet_scope && !g_pose && !g_xf && mi2<0 && overlay<0);
        bounds_us+=render_clock()-started;
        if(!visible){render_math_scope_end(&math);actors_culled++;return;}
        unsigned char cross[CHAR_RENDER_MAX_BONES],outside[CHAR_RENDER_MAX_BONES];
        if(batch_planes)material_guard_codes(c,ch,mats,batch_planes,cross,outside);
        unsigned char floor_cross[CHAR_RENDER_MAX_BONES],floor_outside[CHAR_RENDER_MAX_BONES];
        if(floor_clip)material_guard_codes(c,ch,mats,final_floor_guard,floor_cross,floor_outside);
        if(lod_level){lod_profile.texture_actors++;if(lod_level==2)lod_profile.hull_actors++;}
        lod_profile.actors++;
        int native_drawn=draw_ped_shared(c,ci,mats,&math);
        render_math_scope_end(&math);
        if(native_drawn) {
            posed_total_us+=render_clock()-posed_started;return;
        }
        ped_transform_begin(c,ch);
        /* The PLAY banks contain enlarged, near-black no-UV shells for the
           cel outline.  Draw every bone's shell before any body surface.
           Per-bone body-then-shell ordering let a later arm/shoulder shell
           overwrite the torso and produced large black wedges. */
        unsigned plan_kind=event_actor_material ? 1:0;
        DrawPlan *plan=draw_plan_get(c,ci,plan_kind);
        unsigned group_limit=plan ? plan->group_limit:ge_group_limit(c,ch);
        int texture_on=-1,cull_on=-1,front_face=-1,event_flags=-1,texture_setup=0;
        for (int pass = 0; pass < (event_actor_material ? 3:2); pass++) {
            int want_hull = pass == 0;
            unsigned skip=0;
            if (want_hull && getenv_nohull) continue;
            if(want_hull && lod_level==2) {
                DrawPlanCursor hulls=draw_plan_begin(plan,plan_kind,pass);
                unsigned bi,bt;DrawPlanEntry *entry;
                while(draw_plan_next(&hulls,c,ch,&bi,&bt,&entry))lod_profile.hull_triangles+=c->batch[bt].n_idx/3;
                continue;
            }
            float ident[16]; if (c->vert_stride >= 28) { m_ident(ident); gu_model(ident); }
            DrawPlanCursor it=draw_plan_begin(plan,plan_kind,pass);
            unsigned bi,bt,last_bone=~0u;DrawPlanEntry *entry;
            while(draw_plan_next(&it,c,ch,&bi,&bt,&entry)) {
                    skin_batch_bone=bi;
                    if(c->vert_stride==24 && bi!=last_bone){gu_model(mats[bi]);last_bone=bi;}
                    JgcBatch *B = &c->batch[bt];
                    if(bt<skip)continue;
                    if(batch_planes && !posed_batch_visible(c,ch,B,outside))continue;
                    if(floor_clip) {
                        unsigned any,all;batch_owner_codes(c,ch,B,floor_cross,floor_outside,&any,&all);
                        final_floor_batch_codes(c,ch,B,mats,&any,&all);
                        final_clip_profile.batches++;
                        if(all){final_clip_profile.outside_batches++;continue;}
                        material_planes=any ? final_floor_view:NULL;material_clip_mask=63;
                        if(any)final_clip_profile.clipped_batches++;else final_clip_profile.safe_batches++;
                    }
                    /* Typed EVENT bodies use Ninja mode 0, just like PLAY.
                       Their strip alpha bit must not turn the entire officer
                       into a depthless translucent object. Retain explicit
                       additive siren materials after the solid body. */
                    int additive=event_actor_material && (B->flags&17)==17 && ((B->flags>>8)&7)==1;
                    int ef=additive ? (int)(B->flags&0x1f1):0;
                    if(event_actor_material && event_flags!=ef) {
                        event_flags=ef;
                        if(additive) {
                            static const int factors[8]={GU_FIX,GU_FIX,GU_OTHER_COLOR,GU_ONE_MINUS_OTHER_COLOR,
                                GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,GU_DST_ALPHA,GU_ONE_MINUS_DST_ALPHA};
                            unsigned src=(B->flags>>5)&7;
                            sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,factors[src],GU_FIX,src==1 ? 0xffffff:0,0xffffff);
                            sceGuDisable(GU_ALPHA_TEST);sceGuDepthMask(GU_TRUE);
                        } else {
                            fade_blend();sceGuEnable(GU_ALPHA_TEST);sceGuDepthMask(GU_FALSE);
                        }
                    }
                    int textured=!(B->flags&4),culled=!(B->flags&2) && !getenv_nocull;
                    if(textured!=texture_on) {
                        if(textured)sceGuEnable(GU_TEXTURE_2D);else sceGuDisable(GU_TEXTURE_2D);
                        texture_on=textured;material_applied++;
                    } else material_reused++;
                    if(textured && B->tex!=cur_tex) {
                        if(!texture_setup){bind_tex(c,B->tex);texture_setup=1;}
                        else bind_tex_image(c,B->tex);
                        cur_tex=B->tex;
                    }
                    if(culled!=cull_on) {
                        if(culled)sceGuEnable(GU_CULL_FACE);else sceGuDisable(GU_CULL_FACE);
                        cull_on=culled;material_applied++;
                    } else material_reused++;
                    int face=(B->flags&8) && getenv_hullcw ? GU_CW:GU_CCW;
                    if(culled && face!=front_face){sceGuFrontFace(face);front_face=face;material_applied++;}
                    else if(culled)material_reused++;
                    unsigned limit=entry && entry->group_end ? entry->group_end:group_limit;
                    if(entry && entry->group_end==bt+1)plan_profile.group_skipped++;
                    {unsigned ge_started=render_clock();
                     int ge_handled=(!batch_planes && !floor_clip && limit>bt+1 && draw_ge_skin_range(c,ch,B,mats,&palette,limit,&skip,entry ? &entry->group_end:NULL)) ||
                        draw_ge_skin(c,ch,B,mats,&palette);
                     ge_skin_us+=render_clock()-ge_started;
                     if(ge_handled)continue;}
                    void *v = c->vert_stride >= 28 ? (void *)skin_batch(c, ch, B, mats) : chars_batch_vertices(c,B);
                    if(c->vert_stride>=28){unsigned skin_started=render_clock();draw_skin(c,B,v,chars_batch_indices(c,B));draw_skin_us+=render_clock()-skin_started;}
                    else {ge_submit_draw(GU_TRIANGLES,VFMT,B->n_idx,chars_batch_indices(c,B),v);c->drawn_tris+=B->n_idx/3;}
            }
        }
    }
    material_planes=saved_material_planes;
    ped_transform_active=0;
    posed_total_us+=render_clock()-posed_started;
}

void chars_draw_range(Chars *c, const float planes[6][4], int first, int last)
{
    state_begin(); cur_tex = -1;
    for (int ci = first; ci <= last && ci < (int)c->h->n_chars; ci++) {
        JgcChar *ch = &c->chars[ci]; int vis = 1;
        for (int i = 0; i < 6 && vis; i++) if (planes[i][0]*ch->x + planes[i][1]*(ch->y + 10) + planes[i][2]*ch->z + planes[i][3] < -25.f) vis = 0;
        if (!vis) continue;
        int mi = ch->motion0 + ((c->motion_sel % (int)ch->nmotions) + (int)ch->nmotions) % (int)ch->nmotions;
        draw_posed(c, ci, ch->x, ch->y, ch->z, ch->yaw * 3.14159265f / 180.f, mi, c->frame, -1, 0.f, 0.f, -1, 0.f, 0.f);
    }
    state_end();
}
void chars_draw(Chars *c, const float planes[6][4]) { c->drawn_tris = 0; chars_draw_range(c, planes, 0, (int)c->h->n_chars - 1); }
/* Original effect meshes are not cel-outline characters. The effect export
   records draw-path-specific culling (spark mode 1 has no face culling),
   blend factors and vertex alpha. Do not apply the character hull pass or
   the 0x20 alpha cutoff to their fading vertex colours. */
static float material_matrices[CHAR_RENDER_MAX_BONES][16] __attribute__((aligned(16)));
unsigned chars_prepare_movie_light(Chars *c,unsigned ci,const CharPose *poses,const unsigned char *stable,const CharLight *light)
{
    if(c->vert_stride!=40 || ci>=c->h->n_chars || !light)return 0;
    final_indexed_forget(c);
    const JgcChar *ch=c->chars+ci;float root[16];m_ident(root);
    if(ch->nbones>CHAR_RENDER_MAX_BONES)return 0;
    RenderMathState math;render_math_begin(&math);
    for(unsigned i=0;i<ch->nbones;i++) {
        float local[16] __attribute__((aligned(16)));node_matrix(local,poses[i].ev,poses[i].pos,poses[i].ang,poses[i].scl);
        int parent=c->bones[ch->bone0+i].parent-(int)ch->bone0;
        m_mul(material_matrices[i],parent>=0 ? material_matrices[parent]:root,local);
    }
    render_math_end(&math);
    float direction[3],rgb[3];memcpy(direction,light->direction,12);
    if(!light->raw_direction){float n=sqrtf(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);if(n>0)for(int j=0;j<3;j++)direction[j]/=n;}
    for(int j=0;j<3;j++)rgb[j]=((light->color[0]>>(16-j*8))&255)/255.f;
    unsigned prepared=0;
    for(unsigned bi=0;bi<ch->nbones;bi++) {
        const JgcBone *bone=c->bones+ch->bone0+bi;
        for(unsigned j=0;j<bone->nbatch;j++) {
            JgcBatch *b=c->batch+bone->batch0+j;
            if(((b->flags>>12)&3)!=1 || (b->flags&(8|2048)))continue;
            SkinVert *v=(SkinVert *)chars_batch_vertices(c,b);int safe=1;
            for(unsigned k=0;k<b->n_vert;k++) {
                const SkinVert *p=(const void*)((const unsigned char*)v+k*40);
                unsigned owner=p->bone<ch->nbones ? p->bone:0;
                if(!stable[owner]){safe=0;break;}
            }
            if(!safe)continue;
            unsigned owner=~0u;float local[3]={0};
            for(unsigned k=0;k<b->n_vert;k++) {
                SkinVert *p=(void*)((unsigned char*)v+k*40);
                if(!(p->pad&1))continue;
                unsigned o=p->bone<ch->nbones ? p->bone:0;
                if(owner!=o){owner=o;const float *m=material_matrices[o];
                    for(int r=0;r<3;r++){double d=(double)m[r]*direction[0];d+=(double)m[4+r]*direction[1];d+=(double)m[8+r]*direction[2];local[r]=(float)d;}}
                p->color=source_light_normal_colour(p->color,rgb,local,(const float*)((unsigned char*)p+28),light->diffuse,light->ambient);
            }
            b->flags&=~0x3000u;prepared+=b->n_vert;
        }
    }
    return prepared;
}
/* Reuse the bank's conservative owner-local cubes: only batches crossing
   the guard band need CPU skinning/clipping. A shared outside-plane bit
   rejects the entire convex batch; otherwise preserve every triangle. */
static void material_guard_codes(const Chars *c,const JgcChar *ch,float mats[][16],const float planes[6][4],unsigned char *cross,unsigned char *outside)
{
    for(unsigned i=0;i<ch->nbones;i++) {
        cross[i]=63;outside[i]=0;
#ifdef JGR_MATERIAL_CLIP_REFERENCE
        if(planes==material_planes)continue;
#endif
        if(!c->render_radius)continue;
        float radius=c->render_radius[ch->bone0+i];if(radius<0)continue;
        const float *m=mats[i];cross[i]=0;
        for(unsigned p=0;p<6;p++) {
            const float *q=planes[p];
            float d=q[0]*m[3]+q[1]*m[7]+q[2]*m[11]+q[3],extent=0;
            for(unsigned axis=0;axis<3;axis++)extent+=fabsf(q[0]*m[axis]+q[1]*m[4+axis]+q[2]*m[8+axis]);
            extent=radius*extent+.05f+(fabsf(d)+fabsf(q[3])+radius*extent)*1e-5f;
            if(!isfinite(d) || !isfinite(extent)){cross[i]=63;outside[i]=0;break;}
            if(d<=extent)cross[i]|=1u<<p;
            if(d < -extent)outside[i]|=1u<<p;
        }
    }
}
static int material_bind_pose;
void chars_draw_material_pass(Chars *c,int ci,const float *xf,const CharPose *poses,int motion,float frame,float alpha,const unsigned short *texture_map,int only_pass)
{
    if ((unsigned)ci>=c->h->n_chars || alpha<=0.f || only_pass < -1 || only_pass > 1) return;
    memset(cel_local_valid,0,sizeof cel_local_valid);
    GePalette palette={0};
    const JgcChar *ch=&c->chars[ci];float (*mats)[16]=material_matrices;
    final_indexed_begin(c,ch);
    if (ch->nbones>CHAR_RENDER_MAX_BONES || c->vert_stride<28) return;
    unsigned started=render_clock();
    if(!poses && !unposed_actor_visible(c,(unsigned)ci,xf)){bounds_us+=render_clock()-started;actors_culled++;return;}
    int mi=!material_bind_pose && ch->nmotions ? (int)ch->motion0+((motion%(int)ch->nmotions)+(int)ch->nmotions)%(int)ch->nmotions : -1;
    mats=prepare_hierarchy(c,(unsigned)ci,xf,poses,mi,frame,-1,0,0,-1,0,0,material_matrices,only_pass>=0);
    pose_us+=render_clock()-started;
    started=render_clock();int visible=posed_actor_visible(c,ch,mats,0);
    bounds_us+=render_clock()-started;
    if(!visible){actors_culled++;return;}
    const float (*saved_clip_planes)[4]=material_planes;
    int floor_clip=final_floor_bank==c && ci<5 && !material_planes;
    const float (*clip_planes)[4]=floor_clip ? final_floor_view:material_planes;
    unsigned char cross[CHAR_RENDER_MAX_BONES],outside[CHAR_RENDER_MAX_BONES];
    if(clip_planes && (material_selective || floor_clip))material_guard_codes(c,ch,mats,floor_clip ? final_floor_guard:clip_planes,cross,outside);
    unsigned char visible_cross[CHAR_RENDER_MAX_BONES],visible_outside[CHAR_RENDER_MAX_BONES];
    if(batch_planes)material_guard_codes(c,ch,mats,batch_planes,visible_cross,visible_outside);
    float identity[16];m_ident(identity);gu_model(identity);cur_tex=-1;
    if(background_planes)sceGuDisable(GU_DEPTH_TEST);else sceGuEnable(GU_DEPTH_TEST);
    sceGuDisable(GU_ALPHA_TEST);
    /* PVR submits opaque geometry before its translucent list, regardless
       of hierarchy order. This applies to rooms as well as HUD masks:
       garage walls must not overwrite the already-drawn chain-link alpha. */
    unsigned plan_kind=alpha<1.f ? 3:2;
    DrawPlan *plan=draw_plan_get(c,ci,plan_kind);
    for(int pass=0;pass<2;pass++) {
        if(only_pass>=0 && pass!=only_pass)continue;
        DrawPlanCursor it=draw_plan_begin(plan,plan_kind,pass);
        unsigned i,j;DrawPlanEntry *entry;
        while(draw_plan_next(&it,c,ch,&i,&j,&entry)) {
            const JgcBatch *b=&c->batch[j];skin_batch_bone=i;
            if(batch_planes && !posed_batch_visible(c,ch,b,visible_outside))continue;
            int transparent=pass;
            material_planes=clip_planes;
            material_clip_mask=63;
            if(floor_clip)final_clip_profile.batches++;
            if(clip_planes && (material_selective || floor_clip)){
                unsigned any,all;batch_owner_codes(c,ch,b,cross,outside,&any,&all);
                if(floor_clip)final_floor_batch_codes(c,ch,b,mats,&any,&all);
                if(all){if(floor_clip)final_clip_profile.outside_batches++;continue;}
                if(!any)material_planes=NULL;
                else material_clip_mask=floor_clip ? 63:any;
                if(floor_clip){if(any)final_clip_profile.clipped_batches++;else final_clip_profile.safe_batches++;}
            }
            if (transparent) {
                static const int factors[8]={GU_FIX,GU_FIX,GU_OTHER_COLOR,GU_ONE_MINUS_OTHER_COLOR,
                    GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,GU_DST_ALPHA,GU_ONE_MINUS_DST_ALPHA};
                unsigned src=(b->flags&16) ? (b->flags>>5)&7 : 4;
                unsigned dst=(b->flags&16) ? (b->flags>>8)&7 : 5;
                sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,factors[src],factors[dst],src==1 ? 0xffffff : 0,dst==1 ? 0xffffff : 0);
            }
            else fade_blend();
            sceGuDepthMask(transparent || background_planes ? GU_TRUE : GU_FALSE);
            if (b->flags&4) { sceGuDisable(GU_TEXTURE_2D);cur_tex=-1; }
            else if(external_pixels) {
                sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_4444,0,0,0);
                sceGuTexImage(0,external_width,external_height,external_width,external_pixels);
                sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
                sceGuTexWrap(GU_CLAMP,GU_CLAMP);cur_tex=-1;
            } else {
                unsigned t=texture_map ? texture_map[b->tex]:b->tex;
                if(t>=c->h->n_tex)continue;
                sceGuEnable(GU_TEXTURE_2D);if (cur_tex!=(int)t) { bind_tex(c,t);cur_tex=t; }
            }
            if (b->flags&2) sceGuDisable(GU_CULL_FACE);
            else {
                sceGuEnable(GU_CULL_FACE);sceGuFrontFace(GU_CCW);
                /* JGC flag 8 comes from Ninja strip bit 0 (ignore light),
                   not an instruction to reverse the face. Original mode-2
                   dispatch tables 2ab3f0/2ac3f0 use the same triangle
                   emitter with bit 0 set or clear. Only strip bit 0x10
                   selects their two-sided emitters. The generic material
                   pass must not apply the character hull debug heuristic. */
            }
            int sorted=transparent && material_sort_forward;
#if JGR_ACTOR_TIMING_FORCE
            unsigned ge_started=render_clock();
            int ge_handled=!sorted && alpha==1.f && draw_ge_skin(c,ch,b,mats,&palette);
            ge_skin_us+=render_clock()-ge_started;
            if(ge_handled)continue;
#else
            if(!sorted && alpha==1.f && draw_ge_skin(c,ch,b,mats,&palette))continue;
#endif
            if(final_cel_packet_draw(c,ch,b,mats,alpha,sorted))continue;
            DrawVert *v=skin_batch(c,ch,b,mats);
            if (alpha<1.f) for (unsigned k=0;k<b->n_vert;k++)
                v[k].color=(v[k].color&0xffffffu)|((unsigned)((v[k].color>>24)*alpha)<<24);
            const unsigned short *indices=chars_batch_indices(c,b);
            if(sorted)indices=material_depth_indices(v,indices,b->n_idx);
#if JGR_ACTOR_TIMING_FORCE
            unsigned skin_started=render_clock();
#endif
            draw_skin(c,b,v,indices);
            final_cel_packet_end();
#if JGR_ACTOR_TIMING_FORCE
            draw_skin_us+=render_clock()-skin_started;
#endif
        }
    }
    material_planes=saved_clip_planes;
    material_clip_mask=63;
    sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);state_end();
}
void chars_draw_material(Chars *c,int ci,const float *xf,const CharPose *poses,int motion,float frame,float alpha,const unsigned short *texture_map)
{ chars_draw_material_pass(c,ci,xf,poses,motion,frame,alpha,texture_map,-1); }
void chars_draw_material_alpha_clipped(Chars *c,int ci,const float *xf,float alpha,const unsigned short *texture_map,const float planes[6][4])
{
    material_planes=planes;material_selective=1;
    chars_draw_material_pass(c,ci,xf,NULL,-1,0,alpha,texture_map,-1);
    material_planes=NULL;material_selective=0;
}
void chars_draw_material_pass_clipped(Chars *c,int ci,const float *xf,int motion,float frame,int pass,const float planes[6][4])
{
    material_planes=planes;material_selective=1;
    chars_draw_material_pass(c,ci,xf,NULL,motion,frame,1.f,NULL,pass);
    material_planes=NULL;material_selective=0;
}
void chars_draw_material_pose_clipped(Chars *c,int ci,const float *xf,const CharPose *poses,int pass,const float planes[6][4])
{
    material_planes=planes;material_selective=1;
    chars_draw_material_pass(c,ci,xf,poses,0,0,1.f,NULL,pass);
    material_planes=NULL;material_selective=0;
}
void chars_draw_material_bind_clipped(Chars *c,int ci,const float *xf,int pass,const float planes[6][4])
{
    /* Bind geometry can use the load-time conservative motion bound before
       sampling a single bone. No intermediate caller-owned pose array. */
    material_bind_pose=1;
    chars_draw_material_pass_clipped(c,ci,xf,0,0,pass,planes);
    material_bind_pose=0;
}
void chars_draw_effect(Chars *c,int ci,const float *xf,float frame,float alpha)
{ chars_draw_material(c,ci,xf,NULL,0,frame,alpha,NULL); }
#include "char_arrow.inc"
void chars_draw_background(Chars *c,int ci,const float *xf,const float planes[6][4])
{
    if(!planes)return;
    background_planes=planes;chars_draw_material(c,ci,xf,NULL,0,0,1,NULL);
    background_planes=NULL;sceGuEnable(GU_DEPTH_TEST);sceGuDepthMask(GU_FALSE);
}
void chars_draw_one(Chars *c, int ci, float x, float y, float z, float yaw, int motion, float frame)
{
    if (ci < 0 || ci >= (int)c->h->n_chars) return;
    JgcChar *ch = &c->chars[ci]; int nm = (int)ch->nmotions; int mi = nm > 0 ? ch->motion0 + ((motion % nm) + nm) % nm : -1;   /* nm == 0: rest pose (static banks such as the garage) */
    state_begin(); cur_tex = -1; draw_posed(c, ci, x, y, z, yaw, mi, frame, -1, 0.f, 0.f, -1, 0.f, 0.f); state_end();
}
void chars_draw_one_faded(Chars *c,int ci,float x,float y,float z,float yaw,int motion,float frame,float opacity)
{
    if(opacity>=1.f){chars_draw_one(c,ci,x,y,z,yaw,motion,frame);return;}
    if(!(opacity>0.f) || ci<0 || ci>=(int)c->h->n_chars)return;
    chars_fade_begin(opacity);chars_draw_one(c,ci,x,y,z,yaw,motion,frame);chars_fade_end();
}
void chars_draw_one_blend(Chars *c, int ci, float x, float y, float z, float yaw,
                         int motion, float frame, int motion2, float frame2, float weight,
                         int overlay, float overlay_frame, float overlay_weight)
{
    if (ci<0 || ci>=(int)c->h->n_chars) return;
    JgcChar *ch=&c->chars[ci]; int nm=(int)ch->nmotions;
    int mi=nm>0 ? ch->motion0+((motion%nm)+nm)%nm : -1;
    int mi2=nm>0 && motion2>=0 ? (int)ch->motion0+motion2%nm : -1;
    int oi=nm>0 && overlay>=0 ? (int)ch->motion0+overlay%nm : -1;
    state_begin(); cur_tex=-1;
    draw_posed(c,ci,x,y,z,yaw,mi,frame,mi2,frame2,weight,oi,overlay_frame,overlay_weight); state_end();
}
/* draw with an explicit root matrix (row-major 4x4 as used by draw_posed): translate * rotations * scale, e.g. the police cordon effects */
void chars_draw_one_xf(Chars *c, int ci, const float *xf, int motion, float frame) { g_xf = xf; chars_draw_one(c, ci, 0.f, 0.f, 0.f, 0.f, motion, frame); g_xf = 0; }
void chars_draw_one_rest(Chars *c,int ci,const float *xf)
{
    if((unsigned)ci>=c->h->n_chars)return;
    g_xf=xf;state_begin();cur_tex=-1;
    draw_posed(c,ci,0,0,0,0,-1,0,-1,0,0,-1,0,0);
    state_end();g_xf=NULL;
}
void chars_draw_one_xf_blend(Chars *c,int ci,const float *xf,int motion,float frame,
                           int motion2,float frame2,float weight,int overlay,float overlay_frame,float overlay_weight)
{
    g_xf=xf;
    chars_draw_one_blend(c,ci,0.f,0.f,0.f,0.f,motion,frame,motion2,frame2,weight,overlay,overlay_frame,overlay_weight);
    g_xf=0;
}
static const Chars *pose_batch_bank;
void chars_pose_batch_begin(Chars *c)
{state_begin();cur_tex=-1;pose_batch_bank=c;}
void chars_pose_batch_end(void)
{state_end();pose_batch_bank=NULL;cur_tex=-1;}
void chars_draw_one_pose(Chars *c,int ci,const float *xf,const CharPose *pose,
                         int overlay,float overlay_frame,float overlay_weight)
{
    g_pose=pose;g_xf=xf;
    if(pose_batch_bank==c && overlay<0) {
        if((unsigned)ci<c->h->n_chars)draw_posed(c,ci,0,0,0,0,-1,0,-1,0,0,-1,0,0);
    } else chars_draw_one_blend(c,ci,0.f,0.f,0.f,0.f,0,0.f,-1,0.f,0.f,overlay,overlay_frame,overlay_weight);
    g_pose=0;g_xf=0;
}

void chars_draw_event_actor(Chars *c,int ci,const float *xf,const CharPose *pose)
{
    event_actor_material=1;
    chars_draw_one_pose(c,ci,xf,pose,-1,0,0);
    event_actor_material=0;
    sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);
}
void chars_draw_vehicle(Chars *c,int ci,const float *xf,int motion,float frame)
{
    event_actor_material=1;
    chars_draw_one_xf(c,ci,xf,motion,frame);
    event_actor_material=0;
    sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);
}

void chars_free(Chars *c)
{gfx_wait_idle();
    if(final_floor_bank==c)chars_final_floor_clip(NULL,NULL);
    if(static_cache_bank==c)static_cache_release();
    final_indexed_forget(c);
    chars_pose_forget();
    forget_packets(c);
    forget_lod(c);
    owner_storage_bytes-=c->batch_owner_bytes;free(c->batch_owner_offsets);
    release_char_pages(c);
    chars_free_lighting(c);free(c->lighting_source);free(c->render_radius);free(c->motion_radius);free(c->texture_data);free(c->motion_data);free(c->index_data);free(c->vertex_data);free(c->base);memset(c,0,sizeof *c);
}
