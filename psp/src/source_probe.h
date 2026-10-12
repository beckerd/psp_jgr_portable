#ifndef JGR_SOURCE_PROBE_H
#define JGR_SOURCE_PROBE_H
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "source_fmac.h"

#ifndef JGR_PROBE_NATIVE_COUNT
#define JGR_PROBE_NATIVE_COUNT() ((void)0)
#define JGR_PROBE_EXACT_COUNT() ((void)0)
#endif

/* 0b648c/0b42d4/0b45ec and their Ninja helpers. The raw strip order and
   original flags are inputs: do not infer winding from the ray direction. */
typedef struct {
    float start[3],direction[3],end[3];uint32_t metadata;
    float center[3],radius;
} SourceProbe;
typedef struct {float point[3],normal[3],distance;} SourceProbeHit;
/* Bound native binary32 products/additions relative to both of the source
   dot kernels. The 32-epsilon allowance includes offset addition, final
   multiplication and subnormal flush; unordered/overflow cases cannot
   resolve a predicate. Deliberately keep the original math near edges. */
static inline float probe_estimate(const float *a,const float *b,float offset,float *error)
{
    float v[3];
#ifdef JGR_PROBE_COLLISION_BATCH
    collision_dot_terms(v,a,b);
#else
    for(int k=0;k<3;k++)v[k]=a[k]*b[k];
#endif
    *error=(((fabsf(v[0])+fabsf(v[1]))+fabsf(v[2]))+fabsf(offset))*0x1p-18f+0x1p-100f;
    return ((v[0]+v[1])+v[2])+offset;
}
static inline float probe_fipr(const float *a,const float *b)
{
#ifndef JGR_FMAC_REFERENCE
    /* Preserve the source's binary64 sum order. A finite zero product can
       be omitted because the final +0 already canonicalizes exact zero.
       With one nonzero product, one hardware multiply gives the same final
       binary32 rounding (including a nonzero product underflowing to -0). */
    int terms=0,last=0;
    for(int k=0;k<3;k++)if(a[k]!=0.f && b[k]!=0.f){terms|=1<<k;last=k;}
    if(!terms)return 0.f;
    if(!(terms&(terms-1)))return a[last]*b[last];
    float fast;
    if(terms==7) {if(source_fast_dot3(a,b,&fast))return fast;}
    else {
        int first=terms&1 ? 0:1;
        if(source_fast_dot2(a[first],b[first],a[last],b[last],&fast))return fast;
    }
    double v=0.;
    if(terms&1)v=(double)a[0]*b[0];
    if(terms&2)v+=(double)a[1]*b[1];
    if(terms&4)v+=(double)a[2]*b[2];
    return (float)(v+0.);
#else
    double v=(double)a[0]*b[0];v+=(double)a[1]*b[1];v+=(double)a[2]*b[2];v+=0.;return (float)v;
#endif
}
static inline float probe_ninja_dot(const float *a,const float *b)
{return source_fmac(a[2],b[2],source_fmac(a[0],b[0],a[1]*b[1]));}
static inline int source_probe_setup(SourceProbe *q,const float *a,const float *b)
{
    memset(q,0,sizeof *q);float d[3];
    for(int k=0;k<3;k++)if(!isfinite(a[k]) || !isfinite(b[k]))return 0;
    for(int k=0;k<3;k++)d[k]=b[k]-a[k];
    float dd=probe_fipr(d,d),length=sqrtf(dd);
    if(length==0.f || !isfinite(length))return 0;
    memcpy(q->start,a,12);memcpy(q->end,b,12);q->metadata=65535;
    q->radius=length*.5f;float inverse=1.f/sqrtf(dd);
    for(int k=0;k<3;k++){q->center[k]=(b[k]+a[k])*.5f;q->direction[k]=d[k]*inverse;}
    return 1;
}
static inline void probe_plane(const float *a,const float *b,const float *c,float *n)
{
#ifdef JGR_PROBE_COLLISION_BATCH
    collision_plane(n,a,b,c,1);
#else
    float u[3],v[3];for(int k=0;k<3;k++){u[k]=b[k]-a[k];v[k]=c[k]-b[k];}
    n[0]=u[1]*v[2]-u[2]*v[1];n[1]=u[2]*v[0]-u[0]*v[2];n[2]=u[0]*v[1]-u[1]*v[0];
#endif
}
static inline int probe_inside(const float *vertices,const float *point,const float *n)
{
    int axis=0;if(fabsf(n[1])>fabsf(n[0]))axis=1;
    if(fabsf(n[2])>fabsf(n[axis]))axis=2;
    int x=(axis+1)%3,y=(axis+2)%3;float v[3][2];
    for(int i=0;i<3;i++){v[i][0]=vertices[i*3+x]-point[x];v[i][1]=vertices[i*3+y]-point[y];}
    for(int i=0;i<3;i++) {
        int j=(i+1)%3;float a=v[i][0]*v[j][1],b=v[i][1]*v[j][0];
        if(n[axis]>0.f ? b>a : a>b)return 0;
    }
    return 1;
}
/* Query-independent plane math. Reuse only while the vertices/flags are
   unchanged; keep the two different source dot-product rounding kernels. */
typedef struct {
    float n[3],offset,nn,length,inverse,unit[3];unsigned valid,unit_valid;
} SourceProbePlane;
static inline void source_probe_plane(SourceProbePlane *p,const float *v,uint32_t flags)
{
    p->unit_valid=0;
    probe_plane(v,flags&0x80000000u ? v+6:v+3,flags&0x80000000u ? v+3:v+6,p->n);
    const float *n=p->n;p->valid=n[0]!=0.f || n[1]!=0.f || n[2]!=0.f;
    if(!p->valid)return;
    p->offset=(-(n[0]*v[0])-n[1]*v[1])-n[2]*v[2];
    p->nn=probe_ninja_dot(n,n);p->length=sqrtf(p->nn);p->inverse=1.f/p->length;
}
static inline int source_probe_triangle_prepared(const SourceProbe *q,const float *v,uint32_t flags,
                                                SourceProbePlane *plane,float *direction_length,SourceProbeHit *hit)
{
    const float *n=plane->n;if(!plane->valid)return 0;
    if(!(flags&0x80000000u)) {
        float side;
#ifndef JGR_PROBE_PREDICATE_REFERENCE
        float error;side=probe_estimate(q->direction,n,0.f,&error);
        if(!isfinite(side) || !isfinite(error) || fabsf(side)<=error) {
            JGR_PROBE_EXACT_COUNT();side=probe_fipr(q->direction,n);
        } else JGR_PROBE_NATIVE_COUNT();
#else
        side=probe_fipr(q->direction,n);
#endif
        if(flags&0x40000000u ? side>0.f : side<0.f)return 0;
    }
    float offset=plane->offset,nn=plane->nn,inverse=plane->inverse;
    int center_uncertain=1;
#ifndef JGR_PROBE_PREDICATE_REFERENCE
    float error,center=probe_estimate(n,q->center,offset,&error);
    float distance=fabsf(center)*inverse,bound=error*fabsf(inverse)+fabsf(distance)*0x1p-18f;
    if(isfinite(distance) && isfinite(bound)) {
        if(distance-q->radius>bound){JGR_PROBE_NATIVE_COUNT();return 0;}
        if(q->radius-distance>bound){JGR_PROBE_NATIVE_COUNT();center_uncertain=0;}
    }
#endif
    if(center_uncertain){JGR_PROBE_EXACT_COUNT();if(fabsf(probe_ninja_dot(n,q->center)+offset)*inverse>q->radius)return 0;}
    float nd=probe_ninja_dot(n,q->direction),at=probe_ninja_dot(n,q->start)+offset;
    if(*direction_length<0.f)*direction_length=sqrtf(probe_ninja_dot(q->direction,q->direction));
    float lengths=*direction_length*plane->length;
    float point[3];
    if(fabsf(nd/lengths)<.025f) {
        if(fabsf(at)*inverse!=0.f)return 0;
        float t=-at/nn;for(int k=0;k<3;k++)point[k]=source_fmac(t,n[k],q->start[k]);
    } else {
        float t=-at/nd;for(int k=0;k<3;k++)point[k]=source_fmac(t,q->direction[k],q->start[k]);
    }
    float a[3],b[3];for(int k=0;k<3;k++){a[k]=point[k]-q->start[k];b[k]=point[k]-q->end[k];}
    /* The projected-edge test uses only ordinary floats. Reject outside
       triangles before the source-precision segment dot; both are pure
       predicates and every accepted point retains the original math. */
    if(!probe_inside(v,point,n))return 0;
    float segment;
#ifndef JGR_PROBE_PREDICATE_REFERENCE
    float segment_error;segment=probe_estimate(a,b,0.f,&segment_error);
    if(!isfinite(segment) || !isfinite(segment_error) || fabsf(segment)<=segment_error) {
        JGR_PROBE_EXACT_COUNT();segment=probe_fipr(a,b);
    } else JGR_PROBE_NATIVE_COUNT();
#else
    segment=probe_fipr(a,b);
#endif
    if(segment>0.f)return 0;
    /* Invalid input is not a source contract. Avoid publishing NaNs into
       player motion on overflow/corrupt provider data. */
    for(int k=0;k<3;k++)if(!isfinite(point[k]))return 0;
    if(!plane->unit_valid) {
        float oriented[3];for(int k=0;k<3;k++)oriented[k]=flags&0x40000000u ? n[k]:n[k]*-1.f;
        inverse=1.f/sqrtf(probe_fipr(oriented,oriented));
        for(int k=0;k<3;k++)plane->unit[k]=oriented[k]*inverse;
        plane->unit_valid=1;
    }
    for(int k=0;k<3;k++){hit->point[k]=point[k];hit->normal[k]=plane->unit[k];}
    hit->distance=sqrtf(probe_fipr(a,a));return 1;
}
/* Uncached adapter for independent callers and moving geometry. */
static inline int source_probe_triangle(const SourceProbe *q,const float *v,uint32_t flags,SourceProbeHit *hit)
{
    SourceProbePlane plane;float direction_length=-1.f;source_probe_plane(&plane,v,flags);
    return source_probe_triangle_prepared(q,v,flags,&plane,&direction_length,hit);
}

#endif
