/* A small render-only blocker set. All per-pedestrian work is scalar plane
   tests: no collision traversal, allocation, division, clocks or VFPU scope. */
typedef struct {
    float point[4][3],center[3],radius,inverse_area;
    unsigned short owner,count,first_batch,last_batch;
} StagePedProxyRoof;
typedef struct {float low[3],high[3];unsigned model;} StagePedProxyBody;
typedef struct {unsigned key,n_tex,n_obj,n_batch,n_vert,n_idx,kind;} StagePedProxyBank;
#if JGR_PORTABLE_ASSETS
#include "source_data_stage_ped_proxy_data.inc"
#else
#include "generated/source_data/stage_ped_proxy_data.inc"
#endif
#define STAGE_PED_PROXY_ROOFS 8u
#define STAGE_PED_PROXY_BUSES (STAGE_PED_PROXY_MAX-STAGE_PED_PROXY_ROOFS)
typedef struct {float plane[5][4],score;unsigned count,kind,id;} StagePedProxyCone;
typedef struct {
    const Stage *city;const unsigned char *base;float eye[3];
    unsigned roofs,buses;StagePedProxyCone cone[STAGE_PED_PROXY_MAX];
} StagePedProxyState;
static StagePedProxyState stage_ped_proxies;
typedef char StagePedProxyBudgetFits[(sizeof stage_ped_proxy_roofs+sizeof stage_ped_proxy_bodies+
    sizeof stage_ped_proxy_banks+sizeof stage_ped_proxies<=STAGE_PED_PROXY_BYTES) ? 1:-1];
static void stage_ped_proxy_forget(void)
{stage_ped_proxies.city=NULL;stage_ped_proxies.base=NULL;stage_ped_proxies.roofs=stage_ped_proxies.buses=0;}
static void stage_ped_proxy_bind(Stage *s)
{
    s->ped_proxy_bank=0;
    if(!s->base || !s->h)return;
    for(unsigned i=0;i<sizeof stage_ped_proxy_banks/sizeof stage_ped_proxy_banks[0];i++) {
        const StagePedProxyBank *bank=stage_ped_proxy_banks+i;const JgsHeader *h=s->h;
        if(h->n_tex!=bank->n_tex || h->n_obj!=bank->n_obj || h->n_batch!=bank->n_batch ||
           h->n_vert!=bank->n_vert || h->n_idx!=bank->n_idx)continue;
        unsigned key=visibility_hash(2166136261u,s->base,h->off_vert);
        for(unsigned j=0;j<h->n_batch;j++) {
            const JgsBatch *b=s->batch+j;
            key=visibility_hash(key,stage_batch_vertices(s,b),b->n_vert*sizeof(JgsVert));
            key=visibility_hash(key,stage_batch_indices(s,b),b->n_idx*sizeof(unsigned short));
            if(!(j&63u))asset_progress();
        }
        if(key==bank->key){s->ped_proxy_bank=i+1;return;}
    }
}
static unsigned stage_ped_proxy_kind(const Stage *s)
{
    if(!s || !s->base || !s->h || !s->ped_proxy_bank ||
       s->ped_proxy_bank>sizeof stage_ped_proxy_banks/sizeof stage_ped_proxy_banks[0])return 0;
    return stage_ped_proxy_banks[s->ped_proxy_bank-1].kind;
}
static int stage_ped_proxy_context(const Stage *s)
{
    return s && s->base && stage_ped_proxies.city==s && stage_ped_proxies.base==s->base &&
        stage_prepared.ready && !stage_prepared.building && stage_prepared.owner==s &&
        stage_prepared.base==s->base && stage_prepared.cullmode==0;
}
static int stage_ped_proxy_cone(StagePedProxyCone *out,const float points[4][3],unsigned count,
                               float score,unsigned kind,unsigned id)
{
    StageOccluder surface={0};StageOcclusionCone cone;
    float a[3],b[3];surface.count=count;
    memcpy(surface.point,points,sizeof surface.point);
    for(unsigned k=0;k<3;k++){a[k]=points[1][k]-points[0][k];b[k]=points[2][k]-points[0][k];}
    visibility_cross(surface.area,a,b);
    float length=sqrtf(visibility_dot(surface.area,surface.area));
    if(!isfinite(length) || !(length>.001f))return 0;
    for(unsigned k=0;k<3;k++)a[k]=stage_ped_proxies.eye[k]-points[0][k];
    /* A camera on/near the source plane has no stable shadow cone. */
    if(fabsf(visibility_dot(surface.area,a))<=length)return 0;
    if(!visibility_cone(&surface,stage_ped_proxies.eye,&cone))return 0;
    for(unsigned i=0;i<cone.count;i++)for(unsigned k=0;k<4;k++)
        if(!isfinite(cone.plane[i][k]))return 0;
    memcpy(out->plane,cone.plane,sizeof out->plane);
    out->count=cone.count;out->score=score;out->kind=kind;out->id=id;return 1;
}
void stage_ped_proxy_prepare(const Stage *s,const float eye[3])
{
    stage_ped_proxy_forget();
    if(stage_ped_proxy_kind(s)!=1 || !eye || !stage_prepared.ready || stage_prepared.building ||
       stage_prepared.owner!=s || stage_prepared.base!=s->base || stage_prepared.cullmode!=0)return;
    float camera[3],difference=0;
    if(!visibility_camera(stage_prepared.clip,camera))return;
    for(unsigned k=0;k<3;k++) {
        if(!isfinite(eye[k]) || fabsf(eye[k])>1e7f)return;
        float d=camera[k]-eye[k];difference+=d*d;
    }
    if(!(difference<.0625f))return;
    stage_ped_proxies.city=s;stage_ped_proxies.base=s->base;memcpy(stage_ped_proxies.eye,eye,12);
    unsigned selected[STAGE_PED_PROXY_ROOFS],used=0;float scores[STAGE_PED_PROXY_ROOFS];
    for(unsigned i=0;i<sizeof stage_ped_proxy_roofs/sizeof stage_ped_proxy_roofs[0];i++) {
        const StagePedProxyRoof *r=stage_ped_proxy_roofs+i;
        if(r->owner>=s->h->n_obj || r->owner>=STAGE_VISIBILITY_CACHE ||
           r->last_batch>=s->h->n_batch || r->last_batch>=STAGE_VISIBILITY_CACHE ||
           stage_prepared.objects[r->owner]<0)continue;
        int visible=1;
        for(unsigned b=r->first_batch;b<=r->last_batch;b++)
            if(stage_prepared.batches[b]<0){visible=0;break;}
        if(!visible)continue;
        float dist2=0;
        for(unsigned k=0;k<3;k++){float d=r->center[k]-eye[k];dist2+=d*d;}
        if(!isfinite(dist2) || dist2>1000.f*1000.f)continue;
        for(unsigned p=0;p<6;p++) {
            const float *plane=stage_prepared.visible[p];
            if(visibility_dot(plane,r->center)+plane[3]<-r->radius){visible=0;break;}
        }
        if(!visible)continue;
        float score=dist2*r->inverse_area;unsigned at=used;
        while(at && score<scores[at-1])at--;
        if(at==STAGE_PED_PROXY_ROOFS)continue;
        if(used<STAGE_PED_PROXY_ROOFS)used++;
        for(unsigned j=used-1;j>at;j--){scores[j]=scores[j-1];selected[j]=selected[j-1];}
        scores[at]=score;selected[at]=i;
    }
    for(unsigned i=0;i<used;i++) {
        const StagePedProxyRoof *r=stage_ped_proxy_roofs+selected[i];
        if(stage_ped_proxy_cone(stage_ped_proxies.cone+stage_ped_proxies.roofs,
            r->point,r->count,scores[i],1,selected[i]))stage_ped_proxies.roofs++;
    }
}
void stage_ped_proxy_add_object(const Stage *city,const Stage *bank,unsigned model,
                              float x,float y,float z,float yaw)
{
    if(!stage_ped_proxy_context(city) || stage_ped_proxy_kind(bank)!=2 || model>=bank->h->n_obj ||
       !isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(yaw) ||
       fabsf(x)>1e7f || fabsf(y)>1e7f || fabsf(z)>1e7f)return;
    const StagePedProxyBody *body=NULL;
    for(unsigned i=0;i<sizeof stage_ped_proxy_bodies/sizeof stage_ped_proxy_bodies[0];i++)
        if(stage_ped_proxy_bodies[i].model==model){body=stage_ped_proxy_bodies+i;break;}
    if(!body)return;
    float co=cosf(yaw),si=sinf(yaw),eye[3],dx=stage_ped_proxies.eye[0]-x,dz=stage_ped_proxies.eye[2]-z;
    eye[0]=co*dx-si*dz;eye[1]=stage_ped_proxies.eye[1]-y;eye[2]=si*dx+co*dz;
    int inside=1;
    for(unsigned k=0;k<3;k++) {
        /* Restore the original visual envelope for the inside-camera
           exclusion, including the wheel/roof strips omitted by the proxy. */
        float padding=k==1 ? 5.f:3.f;
        if(eye[k]<body->low[k]-padding || eye[k]>body->high[k]+padding)inside=0;
    }
    if(inside)return;
    /* The visible faces of an inset box cover its shadow without any
       per-pedestrian slab divisions. Bus faces have eight reserved slots. */
    for(unsigned axis=0;axis<3;axis++) {
        float plane;
        if(eye[axis]<body->low[axis]-1.f)plane=body->low[axis];
        else if(eye[axis]>body->high[axis]+1.f)plane=body->high[axis];
        else continue;
        unsigned u=(axis+1)%3,v=(axis+2)%3;float points[4][3],center[3],score=0;
        for(unsigned k=0;k<3;k++)center[k]=(body->low[k]+body->high[k])*.5f;
        center[axis]=plane;
        for(unsigned k=0;k<3;k++){float d=center[k]-eye[k];score+=d*d;}
        score/=(body->high[u]-body->low[u])*(body->high[v]-body->low[v]);
        unsigned slot=stage_ped_proxies.buses;
        if(slot==STAGE_PED_PROXY_BUSES) {
            slot=0;
            for(unsigned j=1;j<STAGE_PED_PROXY_BUSES;j++)
                if(stage_ped_proxies.cone[STAGE_PED_PROXY_ROOFS+j].score>
                   stage_ped_proxies.cone[STAGE_PED_PROXY_ROOFS+slot].score)slot=j;
            if(score>=stage_ped_proxies.cone[STAGE_PED_PROXY_ROOFS+slot].score)continue;
        }
        for(unsigned j=0;j<4;j++) {
            float local[3];local[axis]=plane;
            local[u]=(j==1 || j==2) ? body->high[u]:body->low[u];
            local[v]=j>=2 ? body->high[v]:body->low[v];
            points[j][0]=co*local[0]+si*local[2]+x;points[j][1]=local[1]+y;
            points[j][2]=-si*local[0]+co*local[2]+z;
        }
        if(stage_ped_proxy_cone(stage_ped_proxies.cone+STAGE_PED_PROXY_ROOFS+slot,
           points,4,score,2,model) && stage_ped_proxies.buses<STAGE_PED_PROXY_BUSES)
            stage_ped_proxies.buses++;
    }
}
unsigned stage_ped_proxy_count(const Stage *city)
{return stage_ped_proxy_context(city) ? stage_ped_proxies.roofs+stage_ped_proxies.buses:0;}
int stage_ped_proxy_occluded(const Stage *city,const float torso[3],unsigned *tested)
{
    if(tested)*tested=0;
    if(!torso || !stage_ped_proxy_context(city))return 0;
    for(unsigned k=0;k<3;k++)if(!isfinite(torso[k]) || fabsf(torso[k])>1e7f)return 0;
    for(unsigned kind=0;kind<2;kind++) {
        unsigned count=kind ? stage_ped_proxies.buses:stage_ped_proxies.roofs;
        unsigned first=kind ? STAGE_PED_PROXY_ROOFS:0;
        for(unsigned i=0;i<count;i++) {
            const StagePedProxyCone *cone=stage_ped_proxies.cone+first+i;int covered=1;
            if(tested)(*tested)++;
            for(unsigned p=0;p<cone->count;p++) {
                const float *n=cone->plane[p];float a=n[0]*torso[0],b=n[1]*torso[1],c=n[2]*torso[2];
                float d=((a+b)+c)+n[3],margin=.2f+1e-6f*(fabsf(a)+fabsf(b)+fabsf(c)+fabsf(n[3]));
                if(!isfinite(d) || !(d>margin)){covered=0;break;}
            }
            if(covered)return 1;
        }
    }
    return 0;
}
