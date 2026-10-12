/* Native pedestrian adapter: source cache definitions and authored strips
   survive conversion. Only the GE's interleaved output format is adapted.
   Model plans/indices occupy pinned existing GE pages; posed output belongs
   to this frame. No sidecar access or allocation from the heap during play. */
#include "char_ped_native.h"
#define PED_NATIVE_FILE_BYTES (48u*1024u)
#define PED_NATIVE_MAX_POSITIONS 256u
#define PED_NATIVE_MAX_VERTICES 512u
#define PED_NATIVE_MAX_GROUPS 128u
#define PED_NATIVE_MAX_INDICES 2048u
typedef struct {float x,y,z,w;} PedNativePoint;
typedef struct {unsigned u,v,color;unsigned short position,position_offset;} PedNativeAttribute;
typedef struct {unsigned short owner,first,count,pad;} PedNativeRun;
typedef struct {
    unsigned short npositions,nvertices,ngroups,nindices,nruns,source_vertices,nbones,pad;
    unsigned positions,attributes,runs,groups,indices,bytes;
} PedNativePlan;
static unsigned char ped_native_file[PED_NATIVE_FILE_BYTES] __attribute__((aligned(64)));
static PedNativePoint ped_native_world[PED_NATIVE_MAX_POSITIONS] __attribute__((aligned(16)));
static const Chars *ped_native_bank;
static unsigned ped_native_bytes,ped_native_status;
static int ped_shared_enabled=1,ped_emit_enabled=1,ped_math_enabled=1;
static CharPedSharedProfile ped_shared_window,ped_shared_total;
#ifdef __PSP__
typedef char ped_native_budget[(sizeof ped_native_file+sizeof ped_native_world+
    sizeof ped_shared_window+sizeof ped_shared_total+sizeof ped_identity+128<=CHAR_PED_SHARED_BYTES) ? 1:-1];
#endif
void chars_ped_shared_mode(int enabled){ped_shared_enabled=!!enabled;}
void chars_ped_emit_mode(int enabled){ped_emit_enabled=!!enabled;}
void chars_ped_math_mode(int enabled){ped_math_enabled=!!enabled;}
int chars_ped_math_enabled(void){return ped_math_enabled;}
void chars_ped_shared_totals(CharPedSharedProfile *out){*out=ped_shared_total;}
void chars_ped_shared_profile(CharPedSharedProfile *out)
{*out=ped_shared_window;ped_shared_window=(CharPedSharedProfile){0};}
#define PED_SHARED_ADD(field,n) do {unsigned amount=(n);ped_shared_window.field+=amount;ped_shared_total.field+=amount;} while(0)
static void forget_ped_shared(const Chars *c)
{
    if(ped_native_bank==c){ped_native_bank=NULL;ped_native_bytes=0;ped_native_status=0;}
}
static unsigned ped_native_hash_bytes(unsigned h,const void *ptr,unsigned n)
{
    const unsigned char *p=ptr;
    for(unsigned i=0;i<n;i++){h^=p[i];h*=16777619u;}return h;
}
static unsigned ped_native_geometry_hash(const Chars *c)
{
    unsigned h=2166136261u;
    for(unsigned i=0;i<c->h->n_batches;i++) {
        const JgcBatch *b=c->batch+i;
        h=ped_native_hash_bytes(h,b,sizeof *b);
        h=ped_native_hash_bytes(h,chars_batch_vertices(c,b),b->n_vert*c->vert_stride);
        h=ped_native_hash_bytes(h,chars_batch_indices(c,b),b->n_idx*2u);
    }
    return h;
}
static const SkinVert *ped_native_source(const Chars *c,const JgcChar *ch,unsigned vertex)
{
    for(unsigned bi=0;bi<ch->nbones;bi++) {
        const JgcBone *bone=c->bones+ch->bone0+bi;
        for(unsigned j=0;j<bone->nbatch;j++) {
            const JgcBatch *b=c->batch+bone->batch0+j;
            if(vertex>=b->vert_off && vertex-b->vert_off<b->n_vert)
                return (const SkinVert *)(chars_batch_vertices(c,b)+(vertex-b->vert_off)*c->vert_stride);
        }
    }
    return NULL;
}
static int ped_native_span(unsigned offset,unsigned count,unsigned stride,unsigned bytes)
{return !(offset&3u) && offset>=sizeof(PedNativeHeader) && offset<=bytes && count<=(bytes-offset)/stride;}
static int ped_native_validate(const Chars *c,unsigned bytes)
{
    if(bytes<sizeof(PedNativeHeader) || c->vert_stride!=28)return 0;
    const PedNativeHeader *h=(const PedNativeHeader *)ped_native_file;
    if(memcmp(h->magic,"JPN1",4) || h->version!=1 || h->bytes!=bytes || h->reserved[0] || h->reserved[1] ||
       h->nmodels!=c->h->n_chars || !h->nmodels || h->nmodels>64 ||
       !ped_native_span(h->off_models,h->nmodels,sizeof(PedNativeModel),bytes) ||
       h->geometry_hash!=ped_native_geometry_hash(c))return 0;
    unsigned end=h->off_models+h->nmodels*sizeof(PedNativeModel);
    const PedNativeModel *models=(const PedNativeModel *)(ped_native_file+h->off_models);
    for(unsigned ci=0;ci<h->nmodels;ci++) {
        const PedNativeModel *p=models+ci;const JgcChar *ch=c->chars+ci;
        if(p->reserved || p->nbones!=ch->nbones || !p->nbones ||
           ch->bone0>c->h->n_bones || ch->nbones>c->h->n_bones-ch->bone0 ||
           !p->npositions || p->npositions>PED_NATIVE_MAX_POSITIONS ||
           !p->nvertices || p->nvertices>PED_NATIVE_MAX_VERTICES ||
           !p->ngroups || p->ngroups>PED_NATIVE_MAX_GROUPS ||
           !p->nindices || p->nindices>PED_NATIVE_MAX_INDICES || p->source_vertices<p->npositions)return 0;
        unsigned offsets[4]={p->off_positions,p->off_vertices,p->off_groups,p->off_indices};
        unsigned counts[4]={p->npositions,p->nvertices,p->ngroups,p->nindices};
        unsigned sizes[4]={sizeof(PedNativePosition),sizeof(PedNativeVertex),sizeof(PedNativeGroup),2};
        for(unsigned k=0;k<4;k++) {
            if(offsets[k]<end || !ped_native_span(offsets[k],counts[k],sizes[k],bytes))return 0;
            end=offsets[k]+counts[k]*sizes[k];
        }
        for(unsigned bi=0;bi<ch->nbones;bi++) {
            const JgcBone *bone=c->bones+ch->bone0+bi;
            if(bone->batch0>c->h->n_batches || bone->nbatch>c->h->n_batches-bone->batch0)return 0;
        }
        const PedNativePosition *pos=(const PedNativePosition *)(ped_native_file+p->off_positions);
        const PedNativeVertex *vert=(const PedNativeVertex *)(ped_native_file+p->off_vertices);
        const PedNativeGroup *groups=(const PedNativeGroup *)(ped_native_file+p->off_groups);
        const unsigned short *idx=(const unsigned short *)(ped_native_file+p->off_indices);
        for(unsigned i=0;i<p->npositions;i++) {
            if(pos[i].owner>=ch->nbones || (i && pos[i].owner<pos[i-1].owner))return 0;
            const SkinVert *v=ped_native_source(c,ch,pos[i].source_vertex);
            if(!v || (v->bone<ch->nbones ? v->bone:0)!=pos[i].owner ||
               !isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z))return 0;
        }
        for(unsigned i=0;i<p->nvertices;i++) {
            if(vert[i].position>=p->npositions)return 0;
            const SkinVert *v=ped_native_source(c,ch,vert[i].source_vertex);
            const PedNativePosition *def=pos+vert[i].position;
            const SkinVert *origin=ped_native_source(c,ch,def->source_vertex);
            if(!v || (v->bone<ch->nbones ? v->bone:0)!=def->owner || memcmp(&v->x,&origin->x,12))return 0;
        }
        for(unsigned i=0;i<p->ngroups;i++) {
            const PedNativeGroup *g=groups+i;
            if(!g->nvertices || g->first_vertex>p->nvertices || g->nvertices>p->nvertices-g->first_vertex ||
               g->first_index>p->nindices || g->nindices>p->nindices-g->first_index || !g->nindices ||
               g->strip>1 || (!g->strip && g->nindices%3) || (g->flags&2048) ||
               (!(g->flags&4) && g->tex>=c->h->n_tex))return 0;
            const unsigned short *indices=idx+g->first_index;unsigned triangles=0;
            for(unsigned j=0;j<g->nindices;j++) {
                if(indices[j]>=g->nvertices)return 0;
                if(g->strip && j>=2 && indices[j]!=indices[j-1] && indices[j]!=indices[j-2] && indices[j-1]!=indices[j-2])triangles++;
            }
            if(!g->strip)triangles=g->nindices/3;
            if(triangles!=g->triangles)return 0;
        }
    }
    return 1;
}
static int ped_native_sibling(char *out,unsigned capacity,const char *path)
{
    unsigned n=strlen(path);if(n+16>=capacity)return 0;
    const char *base=strrchr(path,'/');base=base ? base+1:path;
    if(strncmp(base,"people",6))return 0;
    const char *suffix=strstr(base,"_compact.jgc");
    if(!suffix)suffix=strstr(base,".jgc");
    if(!suffix || (strcmp(suffix,"_compact.jgc") && strcmp(suffix,".jgc")))return 0;
    unsigned prefix=suffix-path;memcpy(out,path,prefix);strcpy(out+prefix,"_native.jpn");return 1;
}
static void load_ped_native(Chars *c,const char *path)
{
    char sidecar[512];if(!ped_native_sibling(sidecar,sizeof sidecar,path))return;
    FILE *f=fopen(sidecar,"rb");if(!f)return;
    /* There is one resident city people bank. Tests may keep old banks
       alive: they safely use the ordinary GE renderer after replacement. */
    if(ped_native_bank)for(unsigned i=0;i<512;i++)
        if(ge_packets[i].bank==ped_native_bank && (ge_packets[i].flags&GE_PACKET_NATIVE))ge_packets[i].bank=NULL;
    ped_native_bank=NULL;ped_native_bytes=0;ped_native_status=1;
    long length=-1;
    if(!fseek(f,0,SEEK_END))length=ftell(f);
    if(length>0 && length<=PED_NATIVE_FILE_BYTES && !fseek(f,0,SEEK_SET) &&
       fread(ped_native_file,1,(unsigned)length,f)==(unsigned)length && ped_native_validate(c,(unsigned)length)) {
        ped_native_bank=c;ped_native_bytes=(unsigned)length;ped_native_status=2;
    }
    fclose(f);
    printf("PED_NATIVE_BANK path=%s status=%u bytes=%u\n",sidecar,ped_native_status,ped_native_bytes);
}
static unsigned ped_native_align(unsigned bytes){return (bytes+15)&~15u;}
static int ped_native_group_visible(const PedNativeGroup *g)
{return !(g->flags&8) || (!getenv_nohull && lod_level!=2);}
static PedNativePlan *ped_native_plan(Chars *c,unsigned ci)
{
#if CHAR_GE_SKINNING
    const PedNativeHeader *h=(const PedNativeHeader *)ped_native_file;
    const PedNativeModel *model=(const PedNativeModel *)(ped_native_file+h->off_models)+ci;
    const JgcChar *ch=c->chars+ci;
    GePacket *slot=packet_slot(c,(const JgcBatch *)model,ch->nbones,0,0);
    if(!slot)return NULL;
    if(slot->bank==c && slot->batch==(const JgcBatch *)model && slot->flags==GE_PACKET_NATIVE && slot->pages) {
        memset(ge_packet_pinned+slot->page,1,slot->pages);PED_SHARED_ADD(reused,1);return slot->vertices;
    }
    const PedNativePosition *definitions=(const PedNativePosition *)(ped_native_file+model->off_positions);
    const PedNativeVertex *references=(const PedNativeVertex *)(ped_native_file+model->off_vertices);
    unsigned nruns=0;
    for(unsigned i=0;i<model->npositions;i++)nruns+=!i || definitions[i].owner!=definitions[i-1].owner;
    PedNativePlan p={.npositions=model->npositions,.nvertices=model->nvertices,.ngroups=model->ngroups,
        .nindices=model->nindices,.nruns=nruns,.source_vertices=model->source_vertices,.nbones=model->nbones};
    p.positions=ped_native_align(sizeof p);
    p.attributes=p.positions+p.npositions*sizeof(PedNativePoint);
    p.runs=p.attributes+p.nvertices*sizeof(PedNativeAttribute);
    p.groups=ped_native_align(p.runs+p.nruns*sizeof(PedNativeRun));
    p.indices=p.groups+p.ngroups*sizeof(PedNativeGroup);
    p.bytes=ped_native_align(p.indices+p.nindices*2);
    unsigned page,pages;unsigned char *blob=packet_memory(p.bytes,&page,&pages,slot);
    if(!blob)return NULL;
    *(PedNativePlan *)blob=p;
    PedNativePoint *positions=(PedNativePoint *)(blob+p.positions);
    PedNativeAttribute *attributes=(PedNativeAttribute *)(blob+p.attributes);
    PedNativeRun *runs=(PedNativeRun *)(blob+p.runs);unsigned run=0;
    for(unsigned i=0;i<p.npositions;i++) {
        const SkinVert *v=ped_native_source(c,ch,definitions[i].source_vertex);
        positions[i]=(PedNativePoint){v->x,v->y,v->z,0};
        if(!i || definitions[i].owner!=definitions[i-1].owner)
            runs[run++]=(PedNativeRun){definitions[i].owner,i,0,0};
        runs[run-1].count++;
    }
    for(unsigned i=0;i<p.nvertices;i++) {
        const SkinVert *v=ped_native_source(c,ch,references[i].source_vertex);
        memcpy(&attributes[i].u,&v->u,8);attributes[i].color=v->color;
        attributes[i].position=references[i].position;
        /* The old padding holds a byte offset for the streaming emitter.
           Keep the source position ID too, so the reference stays unchanged. */
        attributes[i].position_offset=references[i].position*sizeof(PedNativePoint);
    }
    memcpy(blob+p.groups,ped_native_file+model->off_groups,p.ngroups*sizeof(PedNativeGroup));
    memcpy(blob+p.indices,ped_native_file+model->off_indices,p.nindices*2);
#ifdef __PSP__
    sceKernelDcacheWritebackRange(blob,(p.bytes+63)&~63u);
#endif
    *slot=(GePacket){.bank=c,.batch=(const JgcBatch *)model,.vertices=blob,.nbones=ch->nbones,
        .flags=GE_PACKET_NATIVE,.page=page,.pages=pages};
    PED_SHARED_ADD(built,1);return (PedNativePlan *)blob;
#else
    (void)c;(void)ci;return NULL;
#endif
}
static void ped_native_matrix(float matrix[12],const float *source)
{
    for(unsigned j=0;j<12;j++) {
        unsigned word;memcpy(&word,source+j,4);
#ifdef __PSP__
        word&=0xffffff00u; /* Exact GE matrix-command input precision. */
#endif
        memcpy(matrix+j,&word,4);
    }
}
#if defined(__PSP__)
static inline void ped_native_load_matrix(const float matrix[12])
{
    __asm__ volatile("lv.q C000, 0 + %0\nlv.q C010, 16 + %0\nlv.q C020, 32 + %0\n"
        : : "m"(*(const float (*)[12])matrix) : "memory");
}
static inline void ped_native_vfpu(const float *point,PedNativePoint *out,int aligned)
{
    if(aligned)__asm__ volatile("lv.q C100, %0\n" : : "m"(*(const PedNativePoint *)point) : "memory");
    else __asm__ volatile("lv.s S100, 0 + %0\nlv.s S101, 4 + %0\nlv.s S102, 8 + %0\n"
        : : "m"(*(const float (*)[3])point) : "memory");
    __asm__ volatile(
        "vscl.t C200, R000, S100\nvscl.t C210, R001, S101\nvadd.t C200, C200, C210\n"
        "vscl.t C210, R002, S102\nvadd.t C200, C200, C210\nvadd.t C200, C200, R003\n"
        "vzero.s S203\nsv.q C200, %0\n" : "=m"(*out) : : "memory");
}
#endif
static inline void ped_native_scalar(const float *m,const float *point,PedNativePoint *out)
{
    out->x=m[0]*point[0]+m[1]*point[1]+m[2]*point[2]+m[3];
    out->y=m[4]*point[0]+m[5]*point[1]+m[6]*point[2]+m[7];
    out->z=m[8]*point[0]+m[9]*point[1]+m[10]*point[2]+m[11];out->w=0;
}
static void ped_native_transform(const PedNativePlan *p,float mats[][16],const RenderMathScope *scope)
{
    const unsigned char *blob=(const unsigned char *)p;
    const PedNativePoint *positions=(const PedNativePoint *)(blob+p->positions);
    const PedNativeRun *runs=(const PedNativeRun *)(blob+p->runs);
    float matrix[12] __attribute__((aligned(16)));
#if (defined(__PSP__) && !defined(JGR_SCALAR_RENDER_MATH)) || defined(JGR_TEST_PED_MATH_SCOPE)
    RenderMathScope own;own.active=0;
    if(!render_math_scope_active(scope))render_math_scope_begin(&own);
#else
    (void)scope;
#endif
    for(unsigned r=0;r<p->nruns;r++) {
        const PedNativeRun *run=runs+r;ped_native_matrix(matrix,mats[run->owner]);
#if defined(__PSP__) && !defined(JGR_SCALAR_RENDER_MATH)
        ped_native_load_matrix(matrix);
#endif
        for(unsigned i=run->first;i<run->first+run->count;i++) {
#if defined(__PSP__) && !defined(JGR_SCALAR_RENDER_MATH)
            ped_native_vfpu(&positions[i].x,ped_native_world+i,1);
#else
            ped_native_scalar(matrix,&positions[i].x,ped_native_world+i);
#endif
        }
    }
#if (defined(__PSP__) && !defined(JGR_SCALAR_RENDER_MATH)) || defined(JGR_TEST_PED_MATH_SCOPE)
    render_math_scope_end(&own);
#endif
}
/* The normal path contains no recovery branches, colour lookup, source-bank
   pointers or floating-point copies: one packed attribute and cache lookup. */
static void __attribute__((noinline)) ped_native_emit_reference(unsigned *out,const PedNativeAttribute *a,unsigned count)
{
    for(unsigned i=0;i<count;i++,out+=6) {
        const PedNativePoint *p=ped_native_world+a[i].position;
        unsigned x,y,z;memcpy(&x,&p->x,4);memcpy(&y,&p->y,4);memcpy(&z,&p->z,4);
        out[0]=a[i].u;out[1]=a[i].v;out[2]=a[i].color;
        out[3]=x;out[4]=y;out[5]=z;
    }
}
static inline const PedNativePoint *ped_native_emit_point(const PedNativeAttribute *a)
{
    /* Compiled once from a checked position ID. Every offset is a multiple
       of 16 and the cache base is aligned; preserve that proof for GCC or it
       emits paired unaligned loads for these otherwise aligned XYZ words. */
    return __builtin_assume_aligned((const unsigned char *)ped_native_world+a->position_offset,16);
}
static void __attribute__((noinline)) ped_native_emit_optimized(unsigned *restrict out,
    const PedNativeAttribute *restrict a,unsigned count)
{
    const PedNativeAttribute *end=a+(count&~3u);
    for(;a!=end;a+=4,out+=24) {
        /* Resolve several cache references before consuming them, letting
           Allegrex overlap their loads. Four vertices share loop overhead;
           integer copies retain every float/colour bit and need no VFPU save. */
        const PedNativePoint *p0=ped_native_emit_point(a),*p1=ped_native_emit_point(a+1);
        const PedNativePoint *p2=ped_native_emit_point(a+2),*p3=ped_native_emit_point(a+3);
        unsigned x0,y0,z0;memcpy(&x0,&p0->x,4);memcpy(&y0,&p0->y,4);memcpy(&z0,&p0->z,4);
        out[0]=a[0].u;out[1]=a[0].v;out[2]=a[0].color;out[3]=x0;out[4]=y0;out[5]=z0;
        unsigned x1,y1,z1;memcpy(&x1,&p1->x,4);memcpy(&y1,&p1->y,4);memcpy(&z1,&p1->z,4);
        out[6]=a[1].u;out[7]=a[1].v;out[8]=a[1].color;out[9]=x1;out[10]=y1;out[11]=z1;
        unsigned x2,y2,z2;memcpy(&x2,&p2->x,4);memcpy(&y2,&p2->y,4);memcpy(&z2,&p2->z,4);
        out[12]=a[2].u;out[13]=a[2].v;out[14]=a[2].color;out[15]=x2;out[16]=y2;out[17]=z2;
        unsigned x3,y3,z3;memcpy(&x3,&p3->x,4);memcpy(&y3,&p3->y,4);memcpy(&z3,&p3->z,4);
        out[18]=a[3].u;out[19]=a[3].v;out[20]=a[3].color;out[21]=x3;out[22]=y3;out[23]=z3;
    }
    if(count&3u)ped_native_emit_reference(out,a,count&3u);
}
static void ped_native_emit(unsigned *out,const PedNativeAttribute *a,unsigned count)
{
    if(ped_emit_enabled)ped_native_emit_optimized(out,a,count);
    else ped_native_emit_reference(out,a,count);
}
static void ped_native_emit_recovery(unsigned *out,const PedNativeAttribute *a,unsigned count)
{
    for(unsigned i=0;i<count;i++,out+=6) {
        const PedNativePoint *p=ped_native_world+a[i].position;
        unsigned x,y,z;memcpy(&x,&p->x,4);memcpy(&y,&p->y,4);memcpy(&z,&p->z,4);
        out[0]=a[i].u;out[1]=a[i].v;out[2]=recovery_colour(a[i].color);
        out[3]=x;out[4]=y;out[5]=z;
    }
}
static int ped_native_eligible(const Chars *c)
{
    return ge_packets_active && c->vert_stride==28 && !event_actor_material &&
        !batch_planes && !background_planes && !material_planes && !material_sort_forward &&
        hud_reciprocal_scale==1.f && hud_depth_shift==0.f && !spray_hud_lighting &&
        !(source_lighting_enabled && c->lighting);
}
static int draw_ped_shared(Chars *c,unsigned ci,float mats[][16],RenderMathScope *math)
{
    if(!ped_packet_scope || !ped_shared_enabled || ped_transform_mode)return 0;
    if(c!=ped_native_bank || !ped_native_eligible(c))goto fallback;
    unsigned started=render_clock();PedNativePlan *p=ped_native_plan(c,ci);
    packet_us+=render_clock()-started;if(!p)goto fallback;
    const unsigned char *blob=(const unsigned char *)p;
    const PedNativeAttribute *attributes=(const PedNativeAttribute *)(blob+p->attributes);
    const PedNativeGroup *groups=(const PedNativeGroup *)(blob+p->groups);
    const unsigned short *indices=(const unsigned short *)(blob+p->indices);
    unsigned bytes=p->nvertices*sizeof(DrawVert);
#if defined(__PSP__) || defined(JGR_TEST_PED_LIST)
    if(bytes+p->ngroups*256u+64u*1024u>gfx_list_available())goto fallback;
#endif
    started=render_clock();ped_native_transform(p,mats,math);
    /* Only scalar bounds and integer model-plan preparation separate the
       hierarchy from this transform. Restore before allocation/submission;
       a rejected preflight leaves cleanup to draw_posed's fallback path. */
    render_math_scope_end(math);
    unsigned elapsed=render_clock()-started;PED_SHARED_ADD(transform_us,elapsed);cpu_skin_us+=elapsed;
    started=render_clock();DrawVert *out=sceGuGetMemory(bytes);if(!out)goto fallback;
    unsigned visible_groups=0,written_vertices=0;
    for(unsigned i=0;i<p->ngroups;i++)visible_groups+=ped_native_group_visible(groups+i);
    if(visible_groups==p->ngroups) {
        if(recovery_active)ped_native_emit_recovery((unsigned *)out,attributes,p->nvertices);
        else ped_native_emit((unsigned *)out,attributes,p->nvertices);
        written_vertices=p->nvertices;
    } else for(unsigned i=0;i<p->ngroups;i++) {
        const PedNativeGroup *g=groups+i;if(!ped_native_group_visible(g))continue;
        if(recovery_active)ped_native_emit_recovery((unsigned *)(out+g->first_vertex),attributes+g->first_vertex,g->nvertices);
        else ped_native_emit((unsigned *)(out+g->first_vertex),attributes+g->first_vertex,g->nvertices);
        written_vertices+=g->nvertices;
    }
    elapsed=render_clock()-started;PED_SHARED_ADD(gather_us,elapsed);cpu_skin_us+=elapsed;
    started=render_clock();gu_model(ped_identity);
    int texture_on=-1,cull_on=-1,front_face=-1,texture_setup=0;
    for(unsigned i=0;i<p->ngroups;i++) {
        const PedNativeGroup *g=groups+i;
        if(!ped_native_group_visible(g)) {
            if(!getenv_nohull && lod_level==2)lod_profile.hull_triangles+=g->triangles;
            continue;
        }
        int textured=!(g->flags&4),culled=!(g->flags&2) && !getenv_nocull;
        if(textured!=texture_on) {
            if(textured)sceGuEnable(GU_TEXTURE_2D);else sceGuDisable(GU_TEXTURE_2D);
            texture_on=textured;material_applied++;
        } else material_reused++;
        if(textured && g->tex!=cur_tex) {
            if(!texture_setup){bind_tex(c,g->tex);texture_setup=1;}else bind_tex_image(c,g->tex);
            cur_tex=g->tex;
        }
        if(culled!=cull_on) {
            if(culled)sceGuEnable(GU_CULL_FACE);else sceGuDisable(GU_CULL_FACE);
            cull_on=culled;material_applied++;
        } else material_reused++;
        int face=(g->flags&8) && getenv_hullcw ? GU_CW:GU_CCW;
        if(culled && face!=front_face){sceGuFrontFace(face);front_face=face;material_applied++;}
        else if(culled)material_reused++;
        ge_submit_draw(g->strip ? GU_TRIANGLE_STRIP:GU_TRIANGLES,VFMT,g->nindices,
            indices+g->first_index,out+g->first_vertex);
        c->drawn_tris+=g->triangles;ped_shared_skin_draws++;ped_shared_skin_vertices+=g->nvertices;
        cpu_skin_batches++;PED_SHARED_ADD(draws,1);PED_SHARED_ADD(triangles,g->triangles);PED_SHARED_ADD(indices,g->nindices);
    }
    elapsed=render_clock()-started;PED_SHARED_ADD(submit_us,elapsed);ge_skin_us+=elapsed;
    cpu_skin_vertices+=p->npositions;PED_SHARED_ADD(actors,1);PED_SHARED_ADD(source_vertices,p->source_vertices);
    PED_SHARED_ADD(unique_positions,p->npositions);PED_SHARED_ADD(vertex_bytes,written_vertices*sizeof(DrawVert));return 1;
fallback:
    PED_SHARED_ADD(fallback_actors,1);return 0;
}
/* Transform-only comparison retains every weighted packet, index, strip and
   palette width. Only its XYZ and the uploaded bone matrices change. */
static int ped_transform_begin(const Chars *c,const JgcChar *ch)
{
    ped_transform_active=0;if(!ped_packet_scope || !ped_transform_mode)return 0;
    if(!ped_native_eligible(c))goto fallback;
    unsigned vertices=0,batches=0,indices=0;
    for(unsigned bi=0;bi<ch->nbones;bi++) {
        const JgcBone *bone=c->bones+ch->bone0+bi;
        if(bone->batch0>c->h->n_batches || bone->nbatch>c->h->n_batches-bone->batch0)goto fallback;
        for(unsigned j=0;j<bone->nbatch;j++) {
            const JgcBatch *b=c->batch+bone->batch0+j;
            if(!b->n_vert || (b->flags&2048))goto fallback;
            vertices+=b->n_vert;indices+=b->n_idx;batches++;
        }
    }
#if defined(__PSP__) || defined(JGR_TEST_PED_LIST)
    /* Account for a completely cold GE packet fallback plus diagnostic
       output, and an additional recovery copy when that scope is active. */
    if(vertices*(recovery_active ? 96u:64u)+indices*2u+batches*256u+64u*1024u>gfx_list_available())goto fallback;
#endif
    ped_transform_active=ped_transform_mode;PED_SHARED_ADD(actors,1);return 1;
fallback:
    PED_SHARED_ADD(fallback_actors,1);return 0;
}
static int ped_transform_packet(unsigned char **vertices,unsigned count,unsigned nvert,unsigned source_nvert,
    const unsigned *bones,float mats[][16])
{
    unsigned started=render_clock(),weight_bytes=(count+3)&~3u,stride=weight_bytes+sizeof(DrawVert);
    unsigned char *live=sceGuGetMemory(nvert*stride);if(!live)return 0;
    for(unsigned i=0;i<nvert*stride/4;i++)((unsigned *)live)[i]=((const unsigned *)*vertices)[i];
    unsigned elapsed=render_clock()-started;PED_SHARED_ADD(gather_us,elapsed);cpu_skin_us+=elapsed;
    started=render_clock();float matrix[12] __attribute__((aligned(16)));
    PedNativePoint transformed __attribute__((aligned(16)));unsigned previous=~0u;
#ifdef __PSP__
    RenderMathState saved;if(ped_transform_active==2)render_math_begin(&saved);
#endif
    for(unsigned i=0;i<nvert;i++) {
        unsigned char *record=live+i*stride;unsigned slot=0;
        while(slot+1<count && !record[slot])slot++;
        if(bones[slot]!=previous) {
            previous=bones[slot];ped_native_matrix(matrix,mats[previous]);
#ifdef __PSP__
            if(ped_transform_active==2)ped_native_load_matrix(matrix);
#endif
        }
        DrawVert *v=(DrawVert *)(record+weight_bytes);
#ifdef __PSP__
        if(ped_transform_active==2)ped_native_vfpu(&v->x,&transformed,0);else
#endif
        ped_native_scalar(matrix,&v->x,&transformed);
        v->x=transformed.x;v->y=transformed.y;v->z=transformed.z;
    }
#ifdef __PSP__
    if(ped_transform_active==2)render_math_end(&saved);
#endif
    elapsed=render_clock()-started;PED_SHARED_ADD(transform_us,elapsed);cpu_skin_us+=elapsed;
    PED_SHARED_ADD(source_vertices,source_nvert);PED_SHARED_ADD(unique_positions,nvert);
    PED_SHARED_ADD(vertex_bytes,nvert*stride);cpu_skin_batches++;cpu_skin_vertices+=nvert;
    *vertices=live;return 1;
}
static void ped_transform_draw_profile(unsigned indices,unsigned triangles,unsigned submit)
{
    PED_SHARED_ADD(draws,1);PED_SHARED_ADD(triangles,triangles);PED_SHARED_ADD(indices,indices);PED_SHARED_ADD(submit_us,submit);
}
