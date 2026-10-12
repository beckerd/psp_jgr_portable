#ifndef JGR_STAGE_PORTAL_H
#define JGR_STAGE_PORTAL_H
#include "source_probe.h"

/* 0c1ce8's 0x70-byte records: bounds, triangle strip, explicit normal,
   translation, then four unused zero words. Shibuya (02f9f0), Kogane
   (039ef0), and Benten (032308) register these every update, independently
   of mission progress. */
typedef struct {
    float min[3],max[3],vertex[4][3],normal[3],translation[3];
} StagePortal;
#if JGR_PORTABLE_ASSETS
#include "source_data_benten_portals.inc"
#else
#include "generated/source_data/benten_portals.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_shibuya_portals.inc"
#else
#include "generated/source_data/shibuya_portals.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_kogane_portals.inc"
#else
#include "generated/source_data/kogane_portals.inc"
#endif

static inline float portal_cross(float *out,const float *a,const float *b)
{
    /* Ninja 278a40 returns the cross-product length. */
    out[0]=source_fmac(-a[2],b[1],a[1]*b[2]);
    out[1]=source_fmac(-a[0],b[2],a[2]*b[0]);
    out[2]=source_fmac(-a[1],b[0],a[0]*b[1]);
    return sqrtf(probe_ninja_dot(out,out));
}
static inline int portal_triangle(const SourceProbe *q,const float *v,const float *n)
{
    /* 0c1fbc: front-facing line/plane intersection, then three cross
       products. Edge hits are accepted; mission_quad's strict quad test
       is deliberately NOT used here. */
    if(probe_fipr(q->direction,n)>0.f)return 0;
    float nn=probe_ninja_dot(n,n),length=sqrtf(nn);
    float offset=(-(n[0]*v[0])-n[1]*v[1])-n[2]*v[2];
    if(fabsf(probe_ninja_dot(n,q->center)+offset)/length>q->radius)return 0;
    float nd=probe_ninja_dot(n,q->direction),at=probe_ninja_dot(n,q->start)+offset;
    int parallel=fabsf(nd/(sqrtf(probe_ninja_dot(q->direction,q->direction))*length))<.025f;
    if(parallel && fabsf(at)/length!=0.f)return 0;
    float t=-at/(parallel ? nn:nd),point[3],a[3],b[3];
    for(int k=0;k<3;k++) {
        point[k]=source_fmac(t,parallel ? n[k]:q->direction[k],q->start[k]);
        if(!isfinite(point[k]))return 0;
        a[k]=point[k]-q->start[k];b[k]=point[k]-q->end[k];
    }
    if(probe_fipr(a,b)>0.f)return 0;
    float p[3][3],c[3][3];
    for(int i=0;i<3;i++)for(int k=0;k<3;k++)p[i][k]=v[i*3+k]-point[k];
    if(portal_cross(c[0],p[1],p[2])==0.f || portal_cross(c[1],p[2],p[0])==0.f ||
       portal_cross(c[2],p[0],p[1])==0.f)return 1;
    float d[3]={probe_fipr(c[0],c[1]),probe_fipr(c[1],c[2]),probe_fipr(c[2],c[0])};
    return (d[0]<=0 && d[1]<=0 && d[2]<=0) || (d[0]>=0 && d[1]>=0 && d[2]>=0);
}
static inline int stage_portal_hit(const StagePortal *p,const float *from,const float *to)
{
    /* 0c80e0/0c814c reject disjoint segment bounds before exact math. */
    for(int k=0;k<3;k++)if(fmaxf(from[k],to[k])<p->min[k] || fminf(from[k],to[k])>p->max[k])return 0;
    SourceProbe q;if(!source_probe_setup(&q,from,to))return 0;
    q.radius+=1.f; /* 0c1d2c */
    return portal_triangle(&q,p->vertex[0],p->normal) || portal_triangle(&q,p->vertex[1],p->normal);
}
static inline int stage_portal_query(int stage,const float *from,const float *to,float *translation)
{
    memset(translation,0,12);
    const StagePortal *portals=stage==1 ? shibuya_portals:stage==2 ? kogane_portals:stage==3 ? benten_portals:NULL;
    const int count=stage==2 ? sizeof kogane_portals/sizeof kogane_portals[0]:4;
    if(!portals)return 0;
    for(int i=0;i<count;i++)if(stage_portal_hit(&portals[i],from,to)) {
        memcpy(translation,portals[i].translation,12);return i+1;
    }
    return 0;
}
#endif
