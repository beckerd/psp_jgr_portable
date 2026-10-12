#ifndef JGR_COLLISION_MATH_H
#define JGR_COLLISION_MATH_H
/* Float-only collision geometry. Preserve every multiply/add boundary;
   VFPU dot/matrix reductions and approximate reciprocals are deliberately
   excluded. Source FIPR/FMAC rounding stays in source_probe/source_fmac. */
#include <math.h>
#if defined(__PSP__) && !defined(JGR_SCALAR_COLLISION_MATH)
#define JGR_COLLISION_VFPU 1
typedef struct {float v[32];unsigned condition;} __attribute__((aligned(16))) CollisionMathState;
static inline void collision_math_begin(CollisionMathState *s)
{
    __asm__ volatile("vnop\nmfvc %0, $131\n" : "=r"(s->condition) : : "memory");
    __asm__ volatile("vnop\n"
        "sv.q C000, 0 + %0\n"
        "sv.q C010, 16 + %0\n"
        "sv.q C020, 32 + %0\n"
        "sv.q C030, 48 + %0\n"
        "sv.q C100, 64 + %0\n"
        "sv.q C110, 80 + %0\n"
        "sv.q C120, 96 + %0\n"
        "sv.q C130, 112 + %0\n"
        : "=m"(s->v) : : "memory");
}
static inline void collision_math_end(const CollisionMathState *s)
{
    __asm__ volatile("mtvc %0, $131\nvnop\n" : : "r"(s->condition) : "memory");
    __asm__ volatile(
        "lv.q C000, 0 + %0\n"
        "lv.q C010, 16 + %0\n"
        "lv.q C020, 32 + %0\n"
        "lv.q C030, 48 + %0\n"
        "lv.q C100, 64 + %0\n"
        "lv.q C110, 80 + %0\n"
        "lv.q C120, 96 + %0\n"
        "lv.q C130, 112 + %0\n"
        : : "m"(s->v) : "memory");
}
/* lv.s/sv.s accept ordinary float alignment and avoid PSP-1000 unaligned
   vector-load COP1 clobbers. Every prefix is explicit and consumed. */
#define CM_LOAD(R,P) "lv.s S" R "0, 0(" P ")\nlv.s S" R "1, 4(" P ")\nlv.s S" R "2, 8(" P ")\n"
#define CM_STORE(R,P) "sv.s S" R "0, 0(" P ")\nsv.s S" R "1, 4(" P ")\nsv.s S" R "2, 8(" P ")\n"
#define CM_CROSS(O,A,B,T) \
    "vpfxs y,z,x,w\nvpfxt z,x,y,w\nvmul.t C" O ", C" A ", C" B "\n" \
    "vpfxs z,x,y,w\nvpfxt y,z,x,w\nvmul.t C" T ", C" A ", C" B "\n" \
    "vsub.t C" O ", C" O ", C" T "\n"
#define CM_DOT(A,B,O) \
    "vmul.t C130, C" A ", C" B "\n" \
    "vadd.s S130, S130, S131\nvadd.s S130, S130, S132\nsv.s S130, " O "(%0)\n"
static inline void collision_plane(float *n,const float *a,const float *b,const float *c,int second_from_b)
{
    const float *base=second_from_b ? b:a;
    __asm__ volatile(CM_LOAD("00","%1") CM_LOAD("01","%2") CM_LOAD("02","%3") CM_LOAD("03","%4")
        "vsub.t C010, C010, C000\nvsub.t C020, C020, C030\n"
        CM_CROSS("100","010","020","110") CM_STORE("10","%0")
        : : "r"(n),"r"(a),"r"(b),"r"(c),"r"(base) : "memory");
}
static inline void collision_edge_dots(float *out,const float *a,const float *b,const float *c,const float *d)
{
    __asm__ volatile(CM_LOAD("00","%1") CM_LOAD("01","%2") CM_LOAD("02","%3") CM_LOAD("03","%4")
        "vsub.t C100, C010, C000\nvsub.t C110, C030, C020\nvsub.t C120, C000, C020\n"
        CM_DOT("100","100","0") CM_DOT("100","110","4") CM_DOT("110","110","8")
        CM_DOT("100","120","12") CM_DOT("110","120","16")
        : : "r"(out),"r"(a),"r"(b),"r"(c),"r"(d) : "memory");
}
static inline void collision_inside_dots(float *out,const float *p,const float *v)
{
    const float *b=v+3,*c=v+6;
    __asm__ volatile(CM_LOAD("00","%1") CM_LOAD("01","%2") CM_LOAD("02","%3") CM_LOAD("03","%4")
        "vsub.t C010, C010, C000\nvsub.t C020, C020, C000\nvsub.t C030, C030, C000\n"
        CM_CROSS("100","020","030","000") CM_CROSS("110","030","010","000") CM_CROSS("120","010","020","000")
        CM_DOT("100","100","0") CM_DOT("110","110","4") CM_DOT("120","120","8")
        CM_DOT("100","110","12") CM_DOT("110","120","16") CM_DOT("120","100","20")
        : : "r"(out),"r"(p),"r"(v),"r"(b),"r"(c) : "memory");
}
/* Approximate two points only to reject separated features. The exact
   source FMAs still compute every potentially accepted contact. */
static inline void collision_point_pair(float *delta,float *scale,const float *a,const float *u,float s,
                                        const float *b,const float *v,float t)
{
    __asm__ volatile(CM_LOAD("00","%2") CM_LOAD("01","%3") CM_LOAD("02","%5") CM_LOAD("03","%6")
        "mtv %4, S100\nmtv %7, S110\n"
        "vscl.t C010, C010, S100\nvscl.t C030, C030, S110\n"
        "vadd.t C100, C000, C010\nvadd.t C110, C020, C030\nvsub.t C120, C100, C110\n"
        "vabs.t C000, C000\nvabs.t C010, C010\nvabs.t C020, C020\nvabs.t C030, C030\n"
        "vadd.t C130, C000, C010\nvadd.t C130, C130, C020\nvadd.t C130, C130, C030\n"
        CM_STORE("12","%0") CM_STORE("13","%1")
        : : "r"(delta),"r"(scale),"r"(a),"r"(u),"r"(s),"r"(b),"r"(v),"r"(t) : "memory");
}
/* A native estimate plus a conservative error envelope lets predicates
   avoid software FMAC/FIPR except at a decision boundary. No contact
   position, normal or distance is approximated. */
static inline void collision_dot_terms(float *out,const float *a,const float *b)
{
    __asm__ volatile(CM_LOAD("00","%1") CM_LOAD("01","%2")
        "vmul.t C100, C000, C010\n" CM_STORE("10","%0")
        : : "r"(out),"r"(a),"r"(b) : "memory");
}
#undef CM_LOAD
#undef CM_STORE
#undef CM_CROSS
#undef CM_DOT
#else
#define JGR_COLLISION_VFPU 0
typedef struct {int unused;} CollisionMathState;
static inline void collision_math_begin(CollisionMathState *s){(void)s;}
static inline void collision_math_end(const CollisionMathState *s){(void)s;}
static inline void collision_plane(float *n,const float *a,const float *b,const float *c,int second_from_b)
{
    float u[3],v[3];const float *base=second_from_b?b:a;
    for(int k=0;k<3;k++){u[k]=b[k]-a[k];v[k]=c[k]-base[k];}
    n[0]=u[1]*v[2]-u[2]*v[1];n[1]=u[2]*v[0]-u[0]*v[2];n[2]=u[0]*v[1]-u[1]*v[0];
}
static inline float collision_scalar_dot(const float *a,const float *b)
{return (a[0]*b[0]+a[1]*b[1])+a[2]*b[2];}
static inline void collision_dot_terms(float *out,const float *a,const float *b)
{for(int k=0;k<3;k++)out[k]=a[k]*b[k];}
static inline void collision_edge_dots(float *out,const float *a,const float *b,const float *c,const float *d)
{
    float u[3],v[3],w[3];for(int k=0;k<3;k++){u[k]=b[k]-a[k];v[k]=d[k]-c[k];w[k]=a[k]-c[k];}
    out[0]=collision_scalar_dot(u,u);out[1]=collision_scalar_dot(u,v);out[2]=collision_scalar_dot(v,v);
    out[3]=collision_scalar_dot(u,w);out[4]=collision_scalar_dot(v,w);
}
static inline void collision_inside_dots(float *out,const float *p,const float *v)
{
    float a[3],b[3],c[3],x[3],y[3],z[3];
    for(int k=0;k<3;k++){a[k]=v[k]-p[k];b[k]=v[k+3]-p[k];c[k]=v[k+6]-p[k];}
    for(int k=0;k<3;k++){int j=(k+1)%3,l=(k+2)%3;x[k]=b[j]*c[l]-b[l]*c[j];y[k]=c[j]*a[l]-c[l]*a[j];z[k]=a[j]*b[l]-a[l]*b[j];}
    out[0]=collision_scalar_dot(x,x);out[1]=collision_scalar_dot(y,y);out[2]=collision_scalar_dot(z,z);
    out[3]=collision_scalar_dot(x,y);out[4]=collision_scalar_dot(y,z);out[5]=collision_scalar_dot(z,x);
}
static inline void collision_point_pair(float *delta,float *scale,const float *a,const float *u,float s,
                                        const float *b,const float *v,float t)
{
    for(int k=0;k<3;k++) {
        float su=s*u[k],tv=t*v[k];
        delta[k]=(a[k]+su)-(b[k]+tv);
        scale[k]=((fabsf(a[k])+fabsf(su))+fabsf(b[k]))+fabsf(tv);
    }
}
#endif
static inline int collision_point_pair_far(const float *a,const float *u,float s,
                                           const float *b,const float *v,float t,float radius)
{
#ifndef JGR_COLLISION_FILTER_REFERENCE
    float delta[3],scale[3];collision_point_pair(delta,scale,a,u,s,b,v,t);
    /* This L1 error allowance dominates binary32 multiply/add versus FMA,
       subtraction and the final squared-distance rounding. Never replace
       the source result near the boundary. Overflow/NaN cannot reject. */
    float expanded=radius+((scale[0]+scale[1])+scale[2])*0x1p-18f+0x1p-100f;
    float dd=(delta[0]*delta[0]+delta[1]*delta[1])+delta[2]*delta[2];
    float bound=expanded*expanded+fabsf(dd)*0x1p-18f;
    return isfinite(dd) && isfinite(bound) && dd>bound;
#else
    (void)a;(void)u;(void)s;(void)b;(void)v;(void)t;(void)radius;return 0;
#endif
}
#endif
