#ifndef STAGE_ACTIVE_OCCLUDERS
#define STAGE_ACTIVE_OCCLUDERS 4
#endif
#ifndef STAGE_OCCLUSION_BATCH_PRUNING
#define STAGE_OCCLUSION_BATCH_PRUNING 1
#endif
/* Offline-certified opaque coverage, inset inside the source mesh. The
   fixed arena is borrowed from gfx's existing list budget, not the heap.
   It owns no GPU pointers and is replaced only during fenced city loading. */
typedef struct {float center[3],extent[3];} StageBox;
typedef struct {float point[4][3],area[3],center[3],radius;unsigned short owner,count;} StageOccluder;
/* An exact, immutable alternate LOD can supply the same certified patch:
   low three bits of the JVO5 count field are corners; the rest
   are alternate owner + 1 (zero means no alternate). */
static unsigned visibility_corners(const StageOccluder *o){return o->count&7u;}
static unsigned visibility_visible_owner(const StageOccluder *o,const signed char *visible,unsigned capacity)
{
    if(o->owner<capacity && visible[o->owner]>=0)return o->owner;
    unsigned alias=o->count>>3;
    if(alias && alias-1<capacity && visible[alias-1]>=0)return alias-1;
    return capacity;
}
typedef struct {float center[3],radius;unsigned short first,count;} StageVisibilityGroup;
typedef struct {char magic[4];unsigned objects,occluders,key;int grid_x,grid_z;unsigned grid_w,grid_h,groups;} StageVisibilityHeader;
static unsigned char visibility_arena[STAGE_VISIBILITY_BYTES] __attribute__((aligned(64)));
static const unsigned char *visibility_owner;
/* Only the first bounded draw-mask entries can use finer occlusion. Store
   three extents per batch; centers already live in the resident bounds.
   Optional, city-owned, at most 48 KiB, and no per-frame allocations. */
#define STAGE_OCCLUSION_BATCHES 4096u
static float (*visibility_batch_extents)[3];
static unsigned visibility_batch_count;
typedef struct {unsigned key,bytes;const unsigned char *bits;} StageDecalBank;
#include "stage_depth_data.inc"
static const unsigned char *stage_decal_bits;
static int visibility_enabled=1;
static int geometry_occlusion=1;
static int plane_occlusion=1;
/* Cache candidate identities only. Live-camera cones, depth separation and
   complete object bounds are still tested on every draw. */
#define STAGE_OCCLUSION_CANDIDATES 128
static struct {
    float eye[3],forward[3];unsigned ids[STAGE_ACTIVE_OCCLUDERS],count,valid;
    unsigned short hidden[STAGE_ACTIVE_OCCLUDERS][STAGE_OCCLUSION_CANDIDATES];
    unsigned short hidden_count[STAGE_ACTIVE_OCCLUDERS];
    struct {unsigned short object,batch;} batch[STAGE_ACTIVE_OCCLUDERS][STAGE_OCCLUSION_CANDIDATES];
    unsigned short batch_count[STAGE_ACTIVE_OCCLUDERS];
} visibility_selection;
void stage_set_visibility(int enabled)
{if(visibility_enabled!=!!enabled){stage_prepared_forget();visibility_enabled=!!enabled;}}
void stage_set_plane_occlusion(int enabled)
{if(plane_occlusion!=!!enabled){stage_prepared_forget();plane_occlusion=!!enabled;visibility_selection.valid=0;}}
static void stage_visibility_forget(const Stage *s)
{stage_capture_forget();stage_prepared_forget();if(s && visibility_owner==s->base){visibility_owner=NULL;free(visibility_batch_extents);visibility_batch_extents=NULL;visibility_batch_count=0;}}
static void visibility_prepare_batches(const Stage *s)
{
    if(!s->batch_bounds)return;
    unsigned count=s->h->n_batch<STAGE_OCCLUSION_BATCHES ? s->h->n_batch:STAGE_OCCLUSION_BATCHES;
    visibility_batch_extents=malloc(count*sizeof *visibility_batch_extents);
    if(!visibility_batch_extents)return;
    visibility_batch_count=count;
    for(unsigned b=0;b<count;b++)for(unsigned k=0;k<3;k++)visibility_batch_extents[b][k]=INFINITY;
    for(unsigned o=0;o<s->h->n_obj;o++)for(unsigned b=s->obj[o].first_batch;b<s->obj[o].first_batch+s->obj[o].n_batch && b<count;b++) {
        const StageInstance *instance=stage_object_instance(s,o);
        float *extent=visibility_batch_extents[b];const StageBatchBound *bound=s->batch_bounds+b;
        extent[0]=extent[1]=extent[2]=INFINITY;
        const JgsBatch *batch=s->batch+b;
        if(!batch->n_vert || !isfinite(bound->r))continue;
        float maximum[3]={0};const JgsVert *v=stage_batch_vertices(s,batch);
        for(unsigned j=0;j<batch->n_vert;j++) {
            float point[3];stage_instance_point(instance,v+j,point);
            for(unsigned k=0;k<3;k++) {
                float d=fabsf(point[k]-(&bound->cx)[k]);
                if(!isfinite(d))goto next;
                if(d>maximum[k])maximum[k]=d;
            }
        }
        for(unsigned k=0;k<3;k++)extent[k]=maximum[k]+.05f+(fabsf((&bound->cx)[k])+maximum[k])*1e-5f;
next:;
    }
}
static unsigned visibility_hash(unsigned hash,const void *data,unsigned bytes)
{
    const unsigned char *p=data;
    while(bytes--)hash=(hash^*p++)*16777619u;
    return hash;
}
int stage_visibility_load(Stage *s,const char *path)
{
    stage_prepared_forget();
    gfx_wait_idle();visibility_owner=NULL;stage_decal_bits=NULL;visibility_selection.valid=0;
    free(visibility_batch_extents);visibility_batch_extents=NULL;visibility_batch_count=0;
    if(!s || !s->base)return -2;
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0777);if(fd<0)return -1;
    SceOff size=sceIoLseek(fd,0,PSP_SEEK_END);sceIoLseek(fd,0,PSP_SEEK_SET);
    int result=-2;unsigned got=0;
    if(size<(SceOff)sizeof(StageVisibilityHeader) || size>STAGE_VISIBILITY_BYTES)goto done;
    while(got<(unsigned)size) {
        int n=sceIoRead(fd,visibility_arena+got,(unsigned)size-got);
        if(n<=0)goto done;
        got+=n;
    }
    const StageVisibilityHeader *h=(const void *)visibility_arena;
    if(memcmp(h->magic,"JVO5",4) || h->objects!=s->h->n_obj || h->objects>4096 || h->occluders>1024 || !h->groups || h->groups>h->objects ||
       !h->grid_w || !h->grid_h || h->grid_w>64 || h->grid_h>64 ||
       (unsigned)size>STAGE_VISIBILITY_BYTES-h->objects*sizeof(float) ||
       sizeof *h+h->objects*sizeof(StageBox)+h->occluders*sizeof(StageOccluder)+h->grid_w*h->grid_h*128+h->groups*sizeof(StageVisibilityGroup)+h->objects*2!=(unsigned)size)goto done;
    unsigned key=visibility_hash(2166136261u,s->base,s->h->off_vert);
    for(unsigned i=0;i<s->h->n_batch;i++) {
        const JgsBatch *b=s->batch+i;
        key=visibility_hash(key,stage_batch_vertices(s,b),b->n_vert*sizeof(JgsVert));
        key=visibility_hash(key,stage_batch_indices(s,b),b->n_idx*sizeof(unsigned short));
        if(!(i&63))asset_progress();
    }
    if(key!=h->key){result=-3;goto done;}
    const StageBox *box=(const void *)(h+1);
    for(unsigned i=0;i<h->objects;i++)for(unsigned k=0;k<3;k++)
        if(!isfinite(box[i].center[k]) || !isfinite(box[i].extent[k]) || box[i].extent[k]<0)goto done;
    const StageOccluder *occ=(const void *)(box+h->objects);
    for(unsigned i=0;i<h->occluders;i++) {
        unsigned corners=visibility_corners(occ+i),alias=occ[i].count>>3;
        if(occ[i].owner>=h->objects || corners<3 || corners>4)goto done;
        if(alias) {
            if(alias>h->objects || alias-1==occ[i].owner)goto done;
            const JgsObj *a=s->obj+occ[i].owner,*b=s->obj+alias-1;
            if(!a->pad || a->pad!=b->pad || !((a->flags^b->flags)&JGS_O_FAR) ||
               ((a->flags|b->flags)&JGS_O_SKY))goto done;
        }
        for(unsigned j=0;j<4;j++)for(unsigned k=0;k<3;k++)if(!isfinite(occ[i].point[j][k]))goto done;
        for(unsigned k=0;k<3;k++)if(!isfinite(occ[i].area[k]))goto done;
        for(unsigned k=0;k<3;k++)if(!isfinite(occ[i].center[k]))goto done;
        if(!(occ[i].radius>=0) || !isfinite(occ[i].radius))goto done;
    }
    const unsigned short *grid=(const void *)(occ+h->occluders);
    for(unsigned i=0;i<h->grid_w*h->grid_h*64;i++)if(grid[i]!=65535 && grid[i]>=h->occluders)goto done;
    const StageVisibilityGroup *groups=(const void *)(grid+h->grid_w*h->grid_h*64);
    const unsigned short *members=(const void *)(groups+h->groups);
    unsigned covered=0;
    for(unsigned i=0;i<h->groups;i++) {
        if(groups[i].first!=covered || !groups[i].count || groups[i].count>8 || covered+groups[i].count>h->objects)goto done;
        for(int k=0;k<3;k++)if(!isfinite(groups[i].center[k]))goto done;
        if(!isfinite(groups[i].radius) || groups[i].radius<0)goto done;
        covered+=groups[i].count;
    }
    if(covered!=h->objects)goto done;
    for(unsigned i=0;i<h->objects;i++)if(members[i]>=h->objects)goto done;
    /* Bind only to the complete geometry fingerprint already verified above.
       The metadata is read-only; no new load-time allocation or asset edits. */
    for(unsigned i=0;i<sizeof stage_decal_banks/sizeof stage_decal_banks[0];i++)
        if(stage_decal_banks[i].key==key && stage_decal_banks[i].bytes==(s->h->n_batch+7)/8)
            stage_decal_bits=stage_decal_banks[i].bits;
    visibility_prepare_batches(s);visibility_owner=s->base;result=0;
done:
    sceIoClose(fd);return result;
}
static float visibility_dot(const float a[3],const float b[3])
{return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static void visibility_cross(float out[3],const float a[3],const float b[3])
{out[0]=a[1]*b[2]-a[2]*b[1];out[1]=a[2]*b[0]-a[0]*b[2];out[2]=a[0]*b[1]-a[1]*b[0];}
static int visibility_camera(const float clip[16],float eye[3])
{
    /* Camera is the intersection of homogeneous x=0, y=0 and w=0.
       Orthographic/singular debug matrices simply have no perspective cone. */
    float a[3],b[3],c[3];
    visibility_cross(a,clip+4,clip+12);visibility_cross(b,clip+12,clip);visibility_cross(c,clip,clip+4);
    float det=visibility_dot(clip,a);if(!isfinite(det) || fabsf(det)<1e-6f)return 0;
    for(int k=0;k<3;k++) {
        eye[k]=-(clip[3]*a[k]+clip[7]*b[k]+clip[15]*c[k])/det;
        if(!isfinite(eye[k]))return 0;
    }
    return 1;
}
typedef struct {float plane[5][4],far_w,depth[4];unsigned count;} StageOcclusionCone;
#define STAGE_VISIBLE_OBJECTS 512u
typedef struct {
    const Stage *owner;const unsigned char *base;
    int cullmode,previous_special;unsigned ready,pending,building;
    float visible[6][4],clip[16],planes[6][4],guard[6][4];
    signed char objects[STAGE_VISIBILITY_CACHE],batches[STAGE_VISIBILITY_CACHE];
    StageOcclusionCone cones[STAGE_ACTIVE_OCCLUDERS];
    unsigned cone_ids[STAGE_ACTIVE_OCCLUDERS],cone_count;float depth_offset;
    unsigned short visible_objects[STAGE_VISIBLE_OBJECTS];
    unsigned visible_count,visible_valid;
} StagePreparedDraw;
typedef char StagePreparedBudgetFits[(sizeof(StagePreparedDraw)<=STAGE_PREPARED_BYTES) ? 1:-1];
static unsigned char stage_prepared_arena[STAGE_PREPARED_BYTES] __attribute__((aligned(64)));
#define stage_prepared (*(StagePreparedDraw *)(void *)stage_prepared_arena)
static int visibility_cone(const StageOccluder *o,const float eye[3],StageOcclusionCone *out)
{
    unsigned count=visibility_corners(o);float center[3]={0},delta[3];
    for(unsigned i=0;i<count;i++)for(int k=0;k<3;k++)center[k]+=o->point[i][k]/count;
    for(int k=0;k<3;k++)delta[k]=center[k]-eye[k];
    float length=sqrtf(visibility_dot(o->area,o->area));if(!(length>1e-4f))return 0;
    float sign=visibility_dot(o->area,delta)>0 ? 1.f:-1.f;
    for(int k=0;k<3;k++)out->plane[0][k]=o->area[k]*sign/length;
    /* The subject must lie strictly beyond the source surface. */
    out->plane[0][3]=-visibility_dot(out->plane[0],o->point[0])-.1f;
    for(unsigned i=0;i<count;i++) {
        float a[3],b[3],*plane=out->plane[i+1];
        for(int k=0;k<3;k++){a[k]=o->point[i][k]-eye[k];b[k]=o->point[(i+1)%count][k]-eye[k];}
        visibility_cross(plane,a,b);length=sqrtf(visibility_dot(plane,plane));
        if(!(length>1e-4f))return 0;
        sign=visibility_dot(plane,delta)>0 ? 1.f:-1.f;
        for(int k=0;k<3;k++)plane[k]*=sign/length;
        plane[3]=-visibility_dot(plane,eye);
    }
    out->count=count+1;return 1;
}
#ifndef __PSP__
static int visibility_contains(const StageOcclusionCone *cone,const StageBox *box)
{
    float margin=.02f+2e-5f*(fabsf(box->center[0])+fabsf(box->center[1])+fabsf(box->center[2]));
    for(unsigned p=0;p<cone->count;p++) {
        const float *n=cone->plane[p];
        float radius=fabsf(n[0])*box->extent[0]+fabsf(n[1])*box->extent[1]+fabsf(n[2])*box->extent[2];
        if(!(visibility_dot(n,box->center)+n[3]>radius+margin))return 0;
    }
    return 1;
}
#endif
/* Keep the forward part of a certified surface when it crosses the near
   plane. Dropping the whole occluder in that case loses nearby floors and
   walls precisely when they cover much of the view. The clipped polygon
   stays inside the original inset; it never supplies new coverage. */
static int visibility_front_patch(const StageOccluder *source,const float clip[16],StageOccluder *out)
{
    float points[5][3];unsigned count=0,source_count=visibility_corners(source);
    float near[4];for(unsigned k=0;k<4;k++)near[k]=clip[8+k]+clip[12+k];
    for(unsigned i=0;i<source_count;i++) {
        const float *a=source->point[(i+source_count-1)%source_count],*b=source->point[i];
        float da=visibility_dot(near,a)+near[3]-.2f,db=visibility_dot(near,b)+near[3]-.2f;
        if(!isfinite(da) || !isfinite(db))return 0;
        if((da>=0)!=(db>=0)) {
            if(count>=5)return 0;
            float t=da/(da-db);
            for(unsigned k=0;k<3;k++)points[count][k]=a[k]+t*(b[k]-a[k]);
            count++;
        }
        if(db>=0){if(count>=5)return 0;memcpy(points[count++],b,12);}
    }
    if(count<3)return 0;
    /* A clipped quad can have five corners. Four consecutive corners still
       form a convex subset: remove the smallest projected corner triangle. */
    unsigned omit=5;
    if(count==5) {
        float xy[5][2],best=INFINITY;
        for(unsigned i=0;i<5;i++) {
            float w=visibility_dot(clip+12,points[i])+clip[15];if(!(w>1.f))return 0;
            for(unsigned k=0;k<2;k++)xy[i][k]=(visibility_dot(clip+4*k,points[i])+clip[4*k+3])/w;
        }
        for(unsigned i=0;i<5;i++) {
            unsigned a=(i+4)%5,b=(i+1)%5;
            float area=fabsf((xy[a][0]-xy[i][0])*(xy[b][1]-xy[i][1])-(xy[a][1]-xy[i][1])*(xy[b][0]-xy[i][0]));
            if(area<best){best=area;omit=i;}
        }
        if(omit==5)return 0;
    }
    *out=*source;out->count=0;
    for(unsigned i=0;i<count;i++)if(i!=omit)memcpy(out->point[out->count++],points[i],12);
    return 1;
}
static float visibility_screen_score(const StageOccluder *o,const float clip[16])
{
    StageOccluder front;if(plane_occlusion){if(!visibility_front_patch(o,clip,&front))return 0;o=&front;}
    float lo[2]={1,1},hi[2]={-1,-1};
    for(unsigned i=0;i<visibility_corners(o);i++) {
        float w=visibility_dot(clip+12,o->point[i])+clip[15];
        if(!(w>1.f) || visibility_dot(clip+8,o->point[i])+clip[11]+w<.1f)return 0;
        float inv=1.f/w;
        for(int k=0;k<2;k++) {
            float v=(visibility_dot(clip+k*4,o->point[i])+clip[k*4+3])*inv;
            if(v<lo[k])lo[k]=v;
            if(v>hi[k])hi[k]=v;
        }
    }
    float area=1;
    for(int k=0;k<2;k++) {
        if(lo[k]<-1)lo[k]=-1;
        if(hi[k]>1)hi[k]=1;
        if(hi[k]<=lo[k])return 0;
        area*=hi[k]-lo[k];
    }
    return area;
}
/* Perspective depth is A + B/w, B < 0. For a point P behind the surface,
   intersect its eye ray with that plane: w_hit = w(P)*d/(n.(P-eye)),
   where d = n.(surface-eye). Requiring the hit to be more than four PSP
   depth steps in front of P gives this linear half-space. Test its minimum
   over the complete AABB, not just the center. The four-step guard covers
   paint bias and rounding; retain the surface's .1-world-unit safety inset.
   This avoids the farthest-corner approximation on oblique walls/floors. */
static int visibility_depth_plane(StageOcclusionCone *cone,const float eye[3],const float clip[16],float offset)
{
    float distance=-visibility_dot(cone->plane[0],eye)-cone->plane[0][3]-.1f;
    if(!(distance>.001f) || !isfinite(distance))return 0;
    float ratio=(8.f/65535.f)*distance/-offset;
    for(unsigned k=0;k<4;k++) {
        cone->depth[k]=cone->plane[0][k]-ratio*clip[12+k];
        if(!isfinite(cone->depth[k]))return 0;
    }
    return 1;
}
static int visibility_depth_inside(const StageOcclusionCone *cone,const StageBox *box,float near_w,float offset)
{
    if(!plane_occlusion)return near_w>cone->far_w &&
        -offset*(near_w-cone->far_w)>(4.f/65535.f)*near_w*cone->far_w;
    if(!(near_w>1.f))return 0;
    float radius=0,scale=1;
    for(unsigned k=0;k<3;k++){float a=fabsf(cone->depth[k]);radius+=a*box->extent[k];scale+=a;}
    float margin=(.02f+2e-5f*(fabsf(box->center[0])+fabsf(box->center[1])+fabsf(box->center[2])))*scale;
    return visibility_dot(cone->depth,box->center)+cone->depth[3]>radius+margin;
}
#if STAGE_OCCLUSION_BATCH_PRUNING
/* A batch cannot fit inside the occlusion volume if even its containing
   object does not overlap that volume. This is only a broad rejection of
   unnecessary tests; the normal batch bounds/depth proof still hides it. */
static int visibility_object_overlap(const StageOcclusionCone *cone,const StageBox *box,
                                     const ClipMathPlanes *sides,float margin)
{
    float radius=0,scale=1;
    for(unsigned k=0;k<3;k++){float a=fabsf(cone->depth[k]);radius+=a*box->extent[k];scale+=a;}
    if(visibility_dot(cone->depth,box->center)+cone->depth[3]+radius < -margin*scale)return 0;
    /* Negated extents change the existing four-plane VFPU operation from
       each box minimum to its maximum, with a fail-open rounding margin. */
    const float extent[3]={-box->extent[0],-box->extent[1],-box->extent[2]};
    return clip_math_box_inside4(sides,box->center,extent,-margin,(1u<<(cone->count-1))-1);
}
#endif
unsigned stage_actor_occluders(const Stage *s)
{
    return s && s->base && visibility_enabled && visibility_owner==s->base &&
        stage_prepared.ready && stage_prepared.owner==s && stage_prepared.base==s->base &&
        !stage_prepared.cullmode ? stage_prepared.cone_count:0;
}
int stage_actor_occluded(const Stage *s,const float center[3],const float extent[3])
{
    unsigned count=stage_actor_occluders(s);
    if(!count || !center || !extent)return 0;
    StageBox box;
    for(unsigned k=0;k<3;k++) {
        if(!isfinite(center[k]) || !isfinite(extent[k]) || extent[k]<0)return 0;
        box.center[k]=center[k];box.extent[k]=extent[k];
    }
    const float *clip=stage_prepared.clip;
    float near_w=visibility_dot(clip+12,center)+clip[15]-.2f;
    for(unsigned k=0;k<3;k++)near_w-=fabsf(clip[12+k])*extent[k];
    float margin=.02f+2e-5f*(fabsf(center[0])+fabsf(center[1])+fabsf(center[2]));
    for(unsigned i=0;i<count;i++) {
        const StageOcclusionCone *cone=stage_prepared.cones+i;
        if(!visibility_depth_inside(cone,&box,near_w,stage_prepared.depth_offset))continue;
        unsigned p;
        for(p=1;p<cone->count;p++) {
            const float *n=cone->plane[p];
            float radius=fabsf(n[0])*extent[0]+fabsf(n[1])*extent[1]+fabsf(n[2])*extent[2];
            if(!(visibility_dot(n,center)+n[3]>radius+margin))break;
        }
        if(p==cone->count)return 1;
    }
    return 0;
}
static void visibility_publish_actors(const Stage *s,const StageOccluder *occluders,
    const StageOccluder *const *chosen,const StageOcclusionCone *cones,const unsigned *cone_id,unsigned count,
    const signed char *visible,unsigned capacity,const signed char *batches,unsigned batch_capacity,float offset)
{
    if(!stage_prepared.building || stage_prepared.owner!=s || !batches)return;
    stage_prepared.cone_count=0;stage_prepared.depth_offset=offset;
    for(unsigned k=0;k<16;k++)if(!isfinite(stage_prepared.clip[k]))return;
    for(unsigned j=0;j<count;j++) {
        const StageOccluder *source=chosen[cone_id[j]];
        /* The sidecar identifies the source object, not its exact batch.
           Require every opaque batch to survive: no reliance on a chain of
           blockers whose source coverage itself disappeared this frame.
           If the primary's batches were hidden, its independently certified
           alternate can still provide the complete opaque surface. */
        unsigned alias=source->count>>3,complete=0;
        for(unsigned candidate=0;candidate<2;candidate++) {
            unsigned owner=candidate ? (alias ? alias-1:capacity):source->owner;
            if(owner>=capacity || visible[owner]<0)continue;
            const JgsObj *object=s->obj+owner;unsigned b;
            for(b=object->first_batch;b<object->first_batch+object->n_batch;b++)
                if(!(s->batch[b].flags&JGS_B_ALPHA) && (b>=batch_capacity || batches[b]<0))break;
            if(b==object->first_batch+object->n_batch){complete=1;break;}
        }
        if(!complete)continue;
        unsigned at=stage_prepared.cone_count++;
        stage_prepared.cones[at]=cones[j];stage_prepared.cone_ids[at]=(unsigned)(source-occluders);
    }
}
static int visibility_batch_hidden(const Stage *s,unsigned batch,const StageOcclusionCone *cone,
                                    const ClipMathPlanes *sides,const float clip[16],float offset)
{
    StageBox box;memcpy(box.center,&s->batch_bounds[batch].cx,12);memcpy(box.extent,visibility_batch_extents[batch],12);
    float near_w=visibility_dot(clip+12,box.center)+clip[15]-.2f;
    for(unsigned k=0;k<3;k++)near_w-=fabsf(clip[12+k])*box.extent[k];
    profile.occlusion_batch_tests++;
    if(!visibility_depth_inside(cone,&box,near_w,offset))return 0;
    float margin=.02f+2e-5f*(fabsf(box.center[0])+fabsf(box.center[1])+fabsf(box.center[2]));
    return clip_math_box_inside4(sides,box.center,box.extent,margin,(1u<<(cone->count-1))-1);
}
static void visibility_hide_batch(const Stage *s,unsigned batch,signed char *visible)
{visible[batch]=-1;profile.occlusion_batches++;profile.occlusion_batch_triangles+=s->batch[batch].n_idx/3;}
static void stage_visibility_apply_detail(const Stage *s,const float planes[6][4],const float clip[16],
    signed char *visible,unsigned capacity,signed char *batch_visible,unsigned batch_capacity)
{
    if(!visibility_enabled || visibility_owner!=s->base || !s->base)return;
    unsigned started=0;
#if defined(__PSP__) && JGR_DIAGNOSTICS
    started=sceKernelGetSystemTimeLow();
#endif
    const StageVisibilityHeader *h=(const void *)visibility_arena;
    const StageBox *boxes=(const void *)(h+1);
    const StageOccluder *occluders=(const void *)(boxes+h->objects);
    const unsigned short *grid=(const void *)(occluders+h->occluders);
    const StageVisibilityGroup *groups=(const void *)(grid+h->grid_w*h->grid_h*64);
    const unsigned short *members=(const void *)(groups+h->groups);
    float *near_distance=(void *)(visibility_arena+STAGE_VISIBILITY_BYTES-h->objects*sizeof(float));
    unsigned count=h->objects<capacity ? h->objects:capacity;
    profile.visibility_frames++;
    unsigned batches=0,indices=0;
    for(unsigned i=0;i<count;i++)if(visible[i]>=0) {
        const StageBox *b=boxes+i;
        for(unsigned j=0;j<6;j++) {
            const float *p=planes[j];
            float radius=fabsf(p[0])*b->extent[0]+fabsf(p[1])*b->extent[1]+fabsf(p[2])*b->extent[2];
            if(visibility_dot(p,b->center)+p[3]+radius<-.1f) {
                visible[i]=-1;profile.box_rejected++;break;
            }
        }
        if(visible[i]>=0) {
            const JgsObj *o=s->obj+i;batches+=o->n_batch;
            for(unsigned b=o->first_batch;b<o->first_batch+o->n_batch;b++)indices+=s->batch[b].n_idx;
        }
    }
    /* Kogane's large meshes are expensive even with fewer than 300
       materials. Skip only views light in both batches and geometry. */
    int world_occlusion=geometry_occlusion ? (batches>=64 || indices>=12288):batches>=300;
    if(!world_occlusion && !stage_prepared.building)goto done;
    float eye[3];if(!visibility_camera(clip,eye))goto done;
    int grid_x=(int)floorf(eye[0]*(1.f/512))-h->grid_x,grid_z=(int)floorf(eye[2]*(1.f/512))-h->grid_z;
    if((unsigned)grid_x>=h->grid_w || (unsigned)grid_z>=h->grid_h)goto done;
    const unsigned short *candidates=(const void *)(occluders+h->occluders);
    candidates+=(grid_z*h->grid_w+grid_x)*64;
    float depth_scale=visibility_dot(clip+8,clip+12)/visibility_dot(clip+12,clip+12);
    float depth_offset=visibility_dot(clip+8,eye)+clip[11];
    if(!(depth_offset<-.001f))goto done;
    for(int k=0;k<3;k++)if(fabsf(clip[8+k]-depth_scale*clip[12+k])>1e-4f)goto done;
    const StageOccluder *chosen[STAGE_ACTIVE_OCCLUDERS];float score[STAGE_ACTIVE_OCCLUDERS];unsigned used=0;
    float moved=0,turned=0;
    for(int k=0;k<3;k++) {
        float d=eye[k]-visibility_selection.eye[k];moved+=d*d;
        d=clip[12+k]-visibility_selection.forward[k];turned+=d*d;
    }
    int reuse=visibility_selection.valid && moved<1024.f && turned<.0025f;
    if(reuse) {
        used=visibility_selection.count;
        for(unsigned i=0;i<used;i++)chosen[i]=occluders+visibility_selection.ids[i];
    } else {
        profile.occlusion_searches++;
        ClipMathPlanes candidate_planes;RenderMathState candidate_state;
        clip_math_prepare(&candidate_planes,planes);render_math_begin(&candidate_state);clip_math_load(&candidate_planes);
        for(unsigned i=0;i<64;i++) {
            if(candidates[i]==65535)continue;
            const StageOccluder *o=occluders+candidates[i];
            if(visibility_visible_owner(o,visible,count)==count)continue;
            if(!clip_math_sphere_visible(&candidate_planes,o->center,o->radius))continue;
            float value=visibility_screen_score(o,clip);
            if(!(value>0) || !isfinite(value))continue;
            unsigned pos=used;if(pos==STAGE_ACTIVE_OCCLUDERS){pos--;if(value<=score[pos])continue;}
            if(used<STAGE_ACTIVE_OCCLUDERS)used++;
            while(pos && value>score[pos-1]){score[pos]=score[pos-1];chosen[pos]=chosen[pos-1];pos--;}
            score[pos]=value;chosen[pos]=o;
        }
        render_math_end(&candidate_state);
        visibility_selection.valid=1;visibility_selection.count=used;
        memset(visibility_selection.hidden_count,0,sizeof visibility_selection.hidden_count);
        memset(visibility_selection.batch_count,0,sizeof visibility_selection.batch_count);
        memcpy(visibility_selection.eye,eye,12);memcpy(visibility_selection.forward,clip+12,12);
        for(unsigned i=0;i<used;i++)visibility_selection.ids[i]=(unsigned)(chosen[i]-occluders);
    }
    StageOcclusionCone cones[STAGE_ACTIVE_OCCLUDERS];unsigned cone_id[STAGE_ACTIVE_OCCLUDERS],n=0;
    for(unsigned i=0;i<used;i++) {
        StageOccluder front;
        if(visibility_visible_owner(chosen[i],visible,count)==count)continue;
        if(plane_occlusion){if(!visibility_front_patch(chosen[i],clip,&front))continue;}
        else {front=*chosen[i];front.count=(unsigned short)visibility_corners(&front);}
        if(!visibility_cone(&front,eye,cones+n))continue;
        float far_w=0;unsigned j;
        for(j=0;j<front.count;j++) {
            float w=visibility_dot(clip+12,front.point[j])+clip[15];
            if(!(w>1.f) || visibility_dot(clip+8,front.point[j])+clip[11]+w<.1f)break;
            if(w>far_w)far_w=w;
        }
        if(j==front.count && (!plane_occlusion || visibility_depth_plane(cones+n,eye,clip,depth_offset))) {
            cones[n].far_w=far_w+.2f;cone_id[n++]=i;
        }
    }
    profile.occluders+=n;
    if(!world_occlusion) {
        visibility_publish_actors(s,occluders,chosen,cones,cone_id,n,visible,capacity,batch_visible,batch_capacity,depth_offset);
        goto done;
    }
    for(unsigned i=0;i<count;i++)if(visible[i]>=0) {
        const StageBox *box=boxes+i;
        float near_w=visibility_dot(clip+12,box->center)+clip[15]-.2f;
        for(int k=0;k<3;k++)near_w-=fabsf(clip[12+k])*box->extent[k];
        near_distance[i]=near_w;
    }
    ClipMathPlanes packed;RenderMathState state;render_math_begin(&state);
    for(unsigned j=0;j<n;j++) {
        float sides[6][4]={{0}};
        for(unsigned p=1;p<cones[j].count;p++)memcpy(sides[p-1],cones[j].plane[p],16);
        clip_math_prepare(&packed,sides);clip_math_load(&packed);
        /* Reuse only candidate identities, never a visibility result. A
           small camera change may reveal any of these boxes: retest its
           complete bounds and depth against the live cone before hiding it.
           Newly hidden objects can wait for the next candidate search. */
        unsigned slot=cone_id[j];
        if(reuse) {
            for(unsigned m=0;m<visibility_selection.hidden_count[slot];m++) {
                unsigned i=visibility_selection.hidden[slot][m];if(visible[i]<0)continue;
                const StageBox *box=boxes+i;float near_w=near_distance[i];
                if(!visibility_depth_inside(cones+j,box,near_w,depth_offset))continue;
                profile.occlusion_tests++;
                float margin=.02f+2e-5f*(fabsf(box->center[0])+fabsf(box->center[1])+fabsf(box->center[2]));
                if(clip_math_box_inside4(&packed,box->center,box->extent,margin,(1u<<(cones[j].count-1))-1)) {
                    visible[i]=-1;profile.occlusion_rejected++;
                }
            }
            if(plane_occlusion && batch_visible && visibility_batch_extents)for(unsigned m=0;m<visibility_selection.batch_count[slot];m++) {
                unsigned i=visibility_selection.batch[slot][m].object,b=visibility_selection.batch[slot][m].batch;
                if(i>=count || visible[i]<0 || b>=batch_capacity || batch_visible[b]<0)continue;
                if(visibility_batch_hidden(s,b,cones+j,&packed,clip,depth_offset))visibility_hide_batch(s,b,batch_visible);
            }
            continue;
        }
        for(unsigned g=0;g<h->groups;g++) {
            const StageVisibilityGroup *group=groups+g;
            if(!clip_math_sphere_visible(&packed,group->center,group->radius))continue;
            for(unsigned m=group->first;m<group->first+group->count;m++) {
                unsigned i=members[m];if(i>=count || visible[i]<0)continue;
                const StageBox *box=boxes+i;float near_w=near_distance[i];
                /* PSP depth is 16-bit and GEQUAL accepts ties: geometric
                   coverage alone is insufficient, even for an opaque wall. */
                profile.occlusion_tests++;
                float margin=.02f+2e-5f*(fabsf(box->center[0])+fabsf(box->center[1])+fabsf(box->center[2]));
                if(visibility_depth_inside(cones+j,box,near_w,depth_offset) &&
                   clip_math_box_inside4(&packed,box->center,box->extent,margin,(1u<<(cones[j].count-1))-1)) {
                    unsigned at=visibility_selection.hidden_count[slot];
                    if(at<STAGE_OCCLUSION_CANDIDATES) {
                        visibility_selection.hidden[slot][at]=i;visibility_selection.hidden_count[slot]++;
                        visible[i]=-1;profile.occlusion_rejected++;
                    }
                }
                if(visible[i]<0 || !plane_occlusion || !batch_visible || !visibility_batch_extents)continue;
                const JgsObj *object=s->obj+i;if(object->n_batch<2)continue;
#if STAGE_OCCLUSION_BATCH_PRUNING
                if(!visibility_object_overlap(cones+j,box,&packed,margin))continue;
#endif
                for(unsigned b=object->first_batch;b<object->first_batch+object->n_batch;b++) {
                    if(visibility_selection.batch_count[slot]>=STAGE_OCCLUSION_CANDIDATES)break;
                    /* Leave overlays/transparency on their existing path,
                       even if their supporting object is partly hidden. */
                    if(b>=batch_capacity || b>=visibility_batch_count || batch_visible[b]<0 || (s->batch[b].flags&JGS_B_ALPHA))continue;
                    if(!visibility_batch_hidden(s,b,cones+j,&packed,clip,depth_offset))continue;
                    unsigned at=visibility_selection.batch_count[slot]++;
                    visibility_selection.batch[slot][at].object=i;visibility_selection.batch[slot][at].batch=b;
                    visibility_hide_batch(s,b,batch_visible);
                }
            }
        }
    }
    render_math_end(&state);
    visibility_publish_actors(s,occluders,chosen,cones,cone_id,n,visible,capacity,batch_visible,batch_capacity,depth_offset);
done:
#if defined(__PSP__) && JGR_DIAGNOSTICS
    profile.visibility_us+=sceKernelGetSystemTimeLow()-started;
#else
    (void)started;
#endif
}
#ifndef __PSP__
/* Host geometry tests can exercise whole-object selection independently. */
static void stage_visibility_apply(const Stage *s,const float planes[6][4],const float clip[16],signed char *visible,unsigned capacity)
{stage_visibility_apply_detail(s,planes,clip,visible,capacity,NULL,0);}
#endif
