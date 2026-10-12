/* Original city selection: 030df4, 039224, 0482b0, 0f4510, 0f544c.
   JRS1 retains the executable's sector lists and alternate-model choices.
   The PSP still owns GU submission, VFPU bounds and guard-band clipping. */
#include "kogane_water_visibility.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_kogane_tunnel_materials.inc"
#else
#include "generated/source_data/kogane_tunnel_materials.inc"
#endif
#include "object_view.h"
typedef struct {char magic[4];unsigned stage,sectors,records,near_objects,objects,batches,key;} StageRenderHeader;
typedef struct {
    unsigned short near_first,near_count,far_first,far_count;
    float position[3];unsigned short sector,kind;float lod2;unsigned source;
} StageRenderRecord;
static struct {
    const Stage *owner;unsigned char *data,*choice,*prop_hidden;
    const StageRenderHeader *h;const unsigned *neighbours;const StageRenderRecord *record;
    unsigned short *qualified;
    unsigned qualified_count,prop_count,sector_rejected,prop_sector_rejected;
    unsigned sector_mask[4],qualification_builds;
    int qualification_valid,qualification_restrict;
    int sector,view_valid,special,previous_special,background_visible;
    unsigned water_mask;int water_bound;
    float eye[3],forward[3],horizontal_tangent;
    float center[3],radius[3],wedge_offset;
} source_render;
/* Legacy JGS inferred blending from texture pixels. These original opaque
   CNK strips share an alpha atlas with overlays, leaving holes if blended. */
static int stage_batch_alpha(const Stage *s,const JgsBatch *b)
{
    if(b->tex==59 && source_render.owner==s &&
       source_render.h->stage==2 && source_render.h->key==KOGANE_TUNNEL_STAGE_KEY) {
        unsigned index=(unsigned)(b-s->batch);
        for(unsigned i=0;i<sizeof kogane_tunnel_opaque/sizeof *kogane_tunnel_opaque;i++)
            if(index==kogane_tunnel_opaque[i])return 0;
    }
    return (b->flags&JGS_B_ALPHA)!=0;
}
static int source_sectors=1,source_lod=1,source_ranges=1;
void stage_set_source_rendering(int sectors,int lod,int ranges)
{if(source_sectors!=sectors || source_lod!=lod || source_ranges!=ranges)stage_prepared_forget();
 source_sectors=sectors;source_lod=lod;source_ranges=ranges;}
static void stage_render_forget(const Stage *s)
{
    stage_capture_forget();
    stage_prepared_forget();
    stage_water_forget(s);
    if(source_render.owner==s){free(source_render.data);memset(&source_render,0,sizeof source_render);}
}
unsigned stage_render_near_objects(const Stage *s)
{return source_render.owner==s ? source_render.h->near_objects:s->h ? s->h->n_obj:0;}
int stage_render_load(Stage *s,const char *path)
{
    stage_prepared_forget();
    gfx_wait_idle();
    if(source_render.owner)stage_render_forget(source_render.owner);
    if(!s || !s->base)return -2;
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0777);if(fd<0)return -1;
    int result=-2;unsigned char *data=NULL;StageRenderHeader header;
    SceOff size=sceIoLseek(fd,0,PSP_SEEK_END);sceIoLseek(fd,0,PSP_SEEK_SET);
    if(size<(SceOff)sizeof header || size>128*1024 || sceIoRead(fd,&header,sizeof header)!=sizeof header)goto done;
    if(memcmp(header.magic,"JRS1",4) || header.stage<1 || header.stage>6 || header.stage==4 ||
       !header.sectors || header.sectors>128 || !header.records || header.records>4096 ||
       header.objects!=s->h->n_obj || header.batches!=s->h->n_batch || header.near_objects>header.objects ||
       sizeof header+header.sectors*16+header.records*sizeof(StageRenderRecord)!=(unsigned)size)goto done;
    if(visibility_hash(2166136261u,s->base,s->h->off_vert)!=header.key){result=-3;goto done;}
    data=malloc((unsigned)size+header.records*4);if(!data){result=-4;goto done;}
    memcpy(data,&header,sizeof header);unsigned got=sizeof header;
    while(got<(unsigned)size){int n=sceIoRead(fd,data+got,(unsigned)size-got);if(n<=0)goto done;got+=n;asset_progress();}
    const StageRenderRecord *records=(const void *)(data+sizeof header+header.sectors*16);
    for(unsigned i=0;i<header.records;i++) {
        const StageRenderRecord *r=records+i;
        int special=r->kind>=6 && r->kind<=8;
        if(r->sector>=header.sectors || r->kind>9 || (r->kind==8 && header.stage!=2) ||
           (r->kind==9 && r->far_count && !(r->lod2>0.f)) || (!r->near_count && !special) ||
           r->near_first+r->near_count>(r->kind==9 ? header.objects:header.near_objects) || r->far_first<header.near_objects ||
           r->far_first+r->far_count>header.objects || !isfinite(r->lod2) || r->lod2<0)goto done;
        for(unsigned k=0;k<3;k++)if(!isfinite(r->position[k]))goto done;
        for(unsigned o=r->near_first;o<r->near_first+r->near_count;o++)
            if(s->obj[o].pad!=i+1 || s->obj[o].flags&JGS_O_FAR)goto done;
        for(unsigned o=r->far_first;o<r->far_first+r->far_count;o++)
            if(s->obj[o].pad!=i+1 || !(s->obj[o].flags&JGS_O_FAR))goto done;
    }
    /* Restored task-owned props append after the immutable city/far bank.
       Only a verified kind-9 record may own a near mesh past the original
       near boundary; preserve that boundary for sky/water ownership. */
    for(unsigned i=0;i<header.objects;i++) {
        const JgsObj *o=s->obj+i;
        if(o->pad>header.records)goto done;
        if(i<header.near_objects)continue;
        if(!o->pad)goto done;
        const StageRenderRecord *r=records+o->pad-1;
        if(!(o->flags&JGS_O_FAR)) {
            if(r->kind!=9 || i<r->near_first || i>=r->near_first+r->near_count)goto done;
        } else if(r->kind==9 && (i<r->far_first || i>=r->far_first+r->far_count)) {
            goto done;
        }
    }
    source_render.owner=s;source_render.data=data;source_render.h=(const void *)data;
    source_render.neighbours=(const void *)(data+sizeof header);source_render.record=records;
    source_render.choice=data+size;memset(source_render.choice,1,header.records);
    source_render.prop_hidden=source_render.choice+header.records;memset(source_render.prop_hidden,0,header.records);
    source_render.qualified=(unsigned short *)(source_render.prop_hidden+header.records);
    source_render.sector=-1;source_render.wedge_offset=350.f;source_render.background_visible=1;
    source_render.center[0]=header.stage==1 ? 800.f:header.stage==2 ? 300.f:650.f;
    source_render.radius[0]=header.stage==1 ? 1200.f:header.stage==2 ? 800.f:1000.f;
    source_render.center[1]=header.stage==1 ? 1250.f:header.stage==2 ? 500.f:1500.f;
    source_render.radius[1]=header.stage==1 ? 1750.f:header.stage==2 ? 1000.f:2000.f;
    source_render.center[2]=header.stage==3 ? 400.f:header.stage>=5 ? 150.f:110.f;
    source_render.radius[2]=header.stage==3 ? 600.f:header.stage>=5 ? 200.f:header.stage==2 ? 300.f:120.f;
    if(header.stage==2 && header.near_objects==1051)
        printf("WATER_SURFACE load=%d bytes=66624\n",stage_water_load(s,path));
    data=NULL;result=0;
done:
    free(data);sceIoClose(fd);return result;
}
void stage_render_sector(Stage *s,int sector)
{
    if(source_render.owner!=s || sector<0 || (unsigned)sector>=source_render.h->sectors || source_render.sector==sector)return;
    stage_prepared_forget();
    int previous=source_render.sector;source_render.sector=sector;
    if(source_render.h->stage==2 && source_render.water_bound && previous>=0)
        source_render.water_mask=kogane_water_sector(source_render.water_mask,sector);
    if(source_render.h->stage==1) { /* 02fd20: order matters on transition. */
        if(sector==3)source_render.wedge_offset=2000.f;
        if(previous==3 || previous==38 || previous==40 || previous==42)source_render.wedge_offset=350.f;
        if(sector==38 || sector==40 || sector==42)source_render.wedge_offset=600.f;
    }
    if(source_render.h->stage==2) { /* 03a72c; unlisted sectors retain values. */
        switch(sector) {
        case 1:case 2:case 3:case 4:case 111:case 112:
            source_render.center[0]=source_render.center[1]=1500.f;
            source_render.radius[0]=source_render.radius[1]=2000.f;break;
        case 37:case 62:case 63:case 66:case 74:case 76:
            source_render.center[0]=source_render.center[1]=1000.f;
            source_render.radius[0]=source_render.radius[1]=1500.f;break;
        case 6:case 8:case 10:case 12:case 17:case 36:case 43:case 46:
        case 47:case 50:case 101:case 105:case 106:case 113:
            source_render.center[0]=300.f;source_render.radius[0]=500.f;
            source_render.center[1]=500.f;source_render.radius[1]=1000.f;break;
        }
    }
}
void stage_render_mission(Stage *s,int sector,int area)
{
    if(source_render.owner!=s)return;
    stage_prepared_forget();
    if(source_render.h->stage!=2){stage_render_sector(s,sector);return;}
    source_render.sector=-1;
    source_render.water_bound=source_render.h->stage==2 && source_render.h->near_objects==1051;
    source_render.water_mask=kogane_water_begin((unsigned)area);
    if(water_surface.owner==s){water_surface.frame=0;water_surface.remainder=0;}
    stage_render_sector(s,sector);
}
int stage_render_medium_circle(const Stage *s,float *ahead,float *radius)
{
    if(source_render.owner!=s)return 0;
    /* 0c3498 is slot 0 (Shibuya 800/1200 in the original RAM); slot 1 is
       the large 0c34c0 circle that Kogane and Grind Square buildings and
       Benten's sector tables select. Traffic 06b920 and set objects
       091f64 both test 0c3498. */
    *ahead=source_render.center[0];*radius=source_render.radius[0];return 1;
}
void stage_render_view(Stage *s,const float eye[3],const float forward[3],float tangent,const float player[3])
{
    if(source_render.owner!=s)return;
    stage_prepared_forget();
    source_render.view_valid=0;
    for(unsigned k=0;k<3;k++)if(!isfinite(eye[k]) || !isfinite(forward[k]))return;
    float length=sqrtf(forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2]);
    if(!(length>.001f) || !isfinite(tangent) || !(tangent>0))return;
    memcpy(source_render.eye,eye,12);for(unsigned k=0;k<3;k++)source_render.forward[k]=forward[k]/length;
    source_render.horizontal_tangent=tangent;source_render.view_valid=1;
    /* 02fb6c registers 175a40; 0c6f9c/0c7048 test the player's position
       against its six original planes. 030dc8 then selects sector 3. */
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_stage_render_inc__volume_0.inc"
#else

#include "generated/source_data/stage_render_inc__volume_0.inc"

#endif

    source_render.special=source_render.h->stage==1 && player!=NULL;
    if(source_render.special)for(unsigned p=0;p<6;p++) {
        float d=0;for(unsigned k=0;k<3;k++)d+=(player[k]-volume[p*3+k])*volume[18+p*3+k];
        if(!isfinite(d) || d>0){source_render.special=0;break;}
    }
}
static int source_circle(const StageRenderRecord *r,const float eye[3],float fx,float fz,float offset,float radius)
{
    float dx=r->position[0]-eye[0]-fx*offset,dz=r->position[2]-eye[2]-fz*offset;
    return dx*dx+dz*dz<radius*radius;
}
void stage_render_background(Stage *s,int visible)
{if(source_render.owner==s && source_render.background_visible!=(visible!=0)){
 stage_prepared_forget();source_render.background_visible=visible!=0;}}
static void stage_render_qualify(int restricted,const unsigned masks[4])
{
    if(source_render.qualification_valid && source_render.qualification_restrict==restricted &&
       !memcmp(source_render.sector_mask,masks,sizeof source_render.sector_mask))return;
    source_render.qualification_valid=1;source_render.qualification_restrict=restricted;
    memcpy(source_render.sector_mask,masks,sizeof source_render.sector_mask);
    source_render.qualified_count=source_render.prop_count=0;
    source_render.sector_rejected=source_render.prop_sector_rejected=0;
    source_render.qualification_builds++;
    /* Authored order is also draw/transition order. Only the membership
       changes here; view ranges and LOD remain live on every render.
       Special callbacks 6..8 bypass the ordinary sector gate. */
    for(unsigned i=0;i<source_render.h->records;i++) {
        const StageRenderRecord *r=source_render.record+i;
        if(r->kind==9)source_render.prop_count++;
        if(restricted && !(r->kind>=6 && r->kind<=8) && !(masks[r->sector/32]&(1u<<(r->sector%32)))) {
            source_render.choice[i]=0;source_render.sector_rejected++;
            if(r->kind==9)source_render.prop_sector_rejected++;
        } else source_render.qualified[source_render.qualified_count++]=(unsigned short)i;
    }
}
static void stage_render_prepare(Stage *s)
{
    if(source_render.owner!=s)return;
    unsigned started=0;
#if defined(__PSP__) && JGR_DIAGNOSTICS
    started=sceKernelGetSystemTimeLow();
#endif
    unsigned masks[4]={0};int sector=source_render.sector;
    int restrict_sectors=source_sectors && sector>=0;
    if(restrict_sectors) {
        unsigned seed=source_render.special ? 3:(unsigned)sector;
        memcpy(masks,source_render.neighbours+seed*4,16);
        /* 030df4's one-draw transition coverage after special rendering. */
        if(source_render.h->stage==1 && !source_render.special && source_render.previous_special) {
            
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_stage_render_inc__seeds_0.inc"
#else

#include "generated/source_data/stage_render_inc__seeds_0.inc"

#endif

            for(unsigned j=0;j<3;j++)for(unsigned k=0;k<4;k++)masks[k]|=source_render.neighbours[seeds[j]*4+k];
        }
    }
    source_render.previous_special=source_render.special;
    stage_render_qualify(restrict_sectors,masks);
    profile.prop_candidates+=source_render.prop_count;
    profile.sector_rejected+=source_render.sector_rejected;
    profile.prop_sector_rejected+=source_render.prop_sector_rejected;
    float fx=source_render.forward[0],fz=source_render.forward[2],fy=source_render.forward[1];
    float length=sqrtf(fx*fx+fz*fz);if(length>.001f){fx/=length;fz/=length;}
    const float *center=source_render.center,*radius=source_render.radius;
    ObjectView prop_view={0};
    if(source_ranges && source_render.view_valid)
        object_view_setup(&prop_view,source_render.h->stage,source_render.eye,source_render.forward,source_render.horizontal_tangent,1.f);
    for(unsigned q=0;q<source_render.qualified_count;q++) {
        unsigned i=source_render.qualified[q];
        const StageRenderRecord *r=source_render.record+i;unsigned char selected=1;
        if(r->kind==8) {
            source_render.choice[i]=source_render.background_visible ? 2:0;
            continue; /* 0392f8 has no sector/distance gate; normal frustum culling still applies. */
        }
        if(r->kind==6 || r->kind==7) {
            int enabled=source_sectors || source_lod || source_ranges;
            source_render.choice[i]=enabled && (r->kind==7 ? source_render.special:sector>=29 && sector<51) ? 2:0;
            continue;
        }
        if(selected && source_render.view_valid) {
            float dx=r->position[0]-source_render.eye[0],dy=r->position[1]-source_render.eye[1],dz=r->position[2]-source_render.eye[2];
            float distance=dx*dx+dy*dy+dz*dz;
            if(source_ranges && length>.001f) {
                int visible=1;
                if(r->kind>=1 && r->kind<=4) {
                    unsigned circle=r->kind==4 ? 0:r->kind-1;
                    visible=source_circle(r,source_render.eye,fx,fz,center[circle],radius[circle]);
                }
                if(r->kind==5 && distance>422500.f)visible=0;
                /* Verified rigid task families share the city-specific
                   small task-view circle and pickup wedge. The
                   placement remains subject to sector and GE bounds. */
                if(r->kind==9)visible=object_pickup_visible(&prop_view,r->position[0],r->position[1],r->position[2]);
                if(visible && (r->kind==4 || r->kind==5) && fy>=-.4f && fy<=.3f) {
                    /* 0c33bc's widened wedge, using the PSP's LIVE
                       horizontal FOV instead of the narrower DC viewport. */
                    float behind=source_render.wedge_offset;
                    float front=dx*fx+dz*fz+behind,side=dx*fz-dz*fx;
                    visible=front>0 && fabsf(side)<front*source_render.horizontal_tangent;
                }
                if(!visible){profile.range_rejected++;if(r->kind==9)profile.prop_range_rejected++;selected=0;}
            }
            if(selected && source_lod && r->lod2>0 && distance>=r->lod2)selected=2;
        }
        source_render.choice[i]=selected;
        if(r->kind==9 && selected)profile.prop_selected++;
        if(selected==1)profile.near_selected++;else if(selected==2)profile.far_selected++;
    }
    profile.selection_frames++;
#if defined(__PSP__) && JGR_DIAGNOSTICS
    profile.selection_us+=sceKernelGetSystemTimeLow()-started;
#else
    (void)started;
#endif
}
static int stage_render_object(const Stage *s,const JgsObj *o)
{
    if(source_render.owner==s && source_render.water_bound){
        /* Exact source-map roots, checked by the asset regression. */
        if(o==s->obj+261)return (source_render.water_mask&1)!=0;
        if(o==s->obj+262)return (source_render.water_mask&2)!=0;
    }
    if(source_render.owner!=s || !o->pad)return !(o->flags&JGS_O_FAR);
    if(source_render.prop_hidden[o->pad-1])return 0;
    return source_render.choice[o->pad-1]==((o->flags&JGS_O_FAR) ? 2:1);
}

static const StageRenderRecord *stage_prop_record(const Stage *s,int handle)
{
    if(!s || source_render.owner!=s || handle<=0 || (unsigned)handle>source_render.h->records)return NULL;
    const StageRenderRecord *r=source_render.record+handle-1;
    return r->kind==9 ? r:NULL;
}
int stage_prop_bind(Stage *s,unsigned source,const float initial[16])
{
    if(!s || source_render.owner!=s || !initial)return 0;
    for(unsigned k=0;k<16;k++)if(!isfinite(initial[k]))return 0;
    for(unsigned i=0;i<source_render.h->records;i++) {
        const StageRenderRecord *r=source_render.record+i;
        if(r->kind!=9 || r->source!=source)continue;
        /* The rigid delta is based on exactly this authored placement;
           accepting a different anchor would rotate around the wrong prop. */
        for(unsigned k=0;k<3;k++)if(fabsf(initial[k*4+3]-r->position[k])>.001f)return 0;
        return (int)i+1;
    }
    return 0;
}
void stage_prop_hide(Stage *s,int handle,int hidden)
{
    if(!stage_prop_record(s,handle))return;
    unsigned char value=hidden!=0;
    if(source_render.prop_hidden[handle-1]==value)return;
    source_render.prop_hidden[handle-1]=value;
    /* Prepared world masks and published occlusion cones cannot retain
       an idle copy after its first hit or resurrect a retired copy. */
    stage_prepared_forget();
}
