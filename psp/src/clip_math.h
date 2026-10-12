#ifndef JGR_CLIP_MATH_H
#define JGR_CLIP_MATH_H
#include "render_math.h"
#include "stage.h"
#include <string.h>

/* Four independent plane equations per vector, with the original multiply /
   add order. Padding lanes are never used for visibility. */
typedef struct { float coefficient[2][4][4]; } __attribute__((aligned(16))) ClipMathPlanes;
static inline void clip_math_prepare(ClipMathPlanes *out,const float planes[6][4])
{
    memset(out,0,sizeof *out);
    for(unsigned p=0;p<6;p++)for(unsigned c=0;c<4;c++)out->coefficient[p/4][c][p%4]=planes[p][c];
}
#if defined(__PSP__) && !defined(JGR_SCALAR_CLIP_MATH)
#define CLIP_MATH_VFPU 1
/* The caller preserves M0..M2 around a loop that calls no other VFPU user.
   Aligned lv.q and individual lv.s avoid the PSP-1000 unaligned-load erratum. */
static inline void clip_math_load(const ClipMathPlanes *p)
{
    __asm__ volatile(
        "lv.q C000, 0 + %0\nlv.q C010, 16 + %0\nlv.q C020, 32 + %0\nlv.q C030, 48 + %0\n"
        "lv.q C100, 64 + %0\nlv.q C110, 80 + %0\nlv.q C120, 96 + %0\nlv.q C130, 112 + %0\n"
        : : "m"(*p) : "memory");
}
static inline void clip_math_distances(const ClipMathPlanes *p,const void *xyz,float out[8])
{
    (void)p;
    typedef struct {float v[3];} Point;
    typedef struct {float v[8];} Distances;
    __asm__ volatile(
        "lv.s S200, 0 + %1\nlv.s S201, 4 + %1\nlv.s S202, 8 + %1\n"
#define CLIP_DISTANCE(X,Y,Z,W,OFF) \
        "vscl.q C210, " X ", S200\nvscl.q C220, " Y ", S201\n" \
        "vadd.q C210, C210, C220\nvscl.q C220, " Z ", S202\n" \
        "vadd.q C210, C210, C220\nvadd.q C210, C210, " W "\nsv.q C210, " OFF " + %0\n"
        CLIP_DISTANCE("C000","C010","C020","C030","0")
        CLIP_DISTANCE("C100","C110","C120","C130","16")
#undef CLIP_DISTANCE
        : "=m"(*(Distances *)out) : "m"(*(const Point *)xyz) : "memory");
}
static inline int clip_math_sphere_impl(const ClipMathPlanes *p,const void *xyz,float radius,int full_mask)
{
    (void)p;typedef struct {float v[3];} Point;
    unsigned reject,mask0=0,mask1=0;
    __asm__ volatile(
        "lv.s S200, 0 + %1\nlv.s S201, 4 + %1\nlv.s S202, 8 + %1\nlv.s S203, %2\n"
        "vone.q C230\nvscl.q C230, C230, S203\nvneg.q C230, C230\n"
        "vscl.q C210, C000, S200\nvscl.q C220, C010, S201\n"
        "vadd.q C210, C210, C220\nvscl.q C220, C020, S202\n"
        "vadd.q C210, C210, C220\nvadd.q C210, C210, C030\n"
        "vcmp.q LT, C210, C230\nvnop\nmfvc %0, $131\n"
        : "=r"(reject) : "m"(*(const Point *)xyz),"m"(radius) : "memory");
    if(reject&15)return -1;
    if(full_mask)__asm__ volatile("vneg.q C220, C230\nvcmp.q LT, C210, C220\nvnop\nmfvc %0, $131\n"
                                  : "=r"(mask0) : : "memory");
    __asm__ volatile(
        "vscl.q C210, C100, S200\nvscl.q C220, C110, S201\n"
        "vadd.q C210, C210, C220\nvscl.q C220, C120, S202\n"
        "vadd.q C210, C210, C220\nvadd.q C210, C210, C130\n"
        "vcmp.q LT, C210, C230\nvnop\nmfvc %0, $131\n"
        : "=r"(reject) : : "memory");
    if(reject&3)return -1;
    if(full_mask)__asm__ volatile("vneg.q C220, C230\nvcmp.q LT, C210, C220\nvnop\nmfvc %0, $131\n"
                                  : "=r"(mask1) : : "memory");
    return (int)((mask0&15)|((mask1&3)<<4));
}
static inline int clip_math_sphere_mask(const ClipMathPlanes *p,const void *xyz,float radius)
{return clip_math_sphere_impl(p,xyz,radius,1);}
static inline int clip_math_sphere_visible(const ClipMathPlanes *p,const void *xyz,float radius)
{return clip_math_sphere_impl(p,xyz,radius,0)>=0;}
/* Entire AABB inside up to four inward planes. M0..M2 remain borrowed
   from the caller; load scalars because packed boxes have 24-byte stride. */
static inline int clip_math_box_inside4(const ClipMathPlanes *p,const float center[3],const float extent[3],float margin,unsigned mask)
{
    (void)p;typedef struct {float v[3];} Three;unsigned reject;
    __asm__ volatile(
        "lv.s S200, 0 + %1\nlv.s S201, 4 + %1\nlv.s S202, 8 + %1\nlv.s S203, %3\n"
        "lv.s S230, 0 + %2\nlv.s S231, 4 + %2\nlv.s S232, 8 + %2\n"
        "vscl.q C210, C000, S200\nvscl.q C220, C010, S201\nvadd.q C210, C210, C220\n"
        "vscl.q C220, C020, S202\nvadd.q C210, C210, C220\nvadd.q C210, C210, C030\n"
        "vabs.q C220, C000\nvscl.q C220, C220, S230\nvsub.q C210, C210, C220\n"
        "vabs.q C220, C010\nvscl.q C220, C220, S231\nvsub.q C210, C210, C220\n"
        "vabs.q C220, C020\nvscl.q C220, C220, S232\nvsub.q C210, C210, C220\n"
        "vone.q C220\nvscl.q C220, C220, S203\nvcmp.q LE, C210, C220\nvnop\nmfvc %0, $131\n"
        : "=r"(reject) : "m"(*(const Three *)center),"m"(*(const Three *)extent),"m"(margin) : "memory");
    return !(reject&mask&15);
}
/* Classify one indexed vertex against several guard planes at once. Keep
   planes resident for a whole index chunk; caller owns M0..M2 and VFPU_CC. */
static inline unsigned clip_math_point_code(const ClipMathPlanes *p,const void *xyz,unsigned mask)
{
    (void)p;typedef struct {float v[3];} Point;
    unsigned a=0,b=0;
    __asm__ volatile("lv.s S200, 0 + %0\nlv.s S201, 4 + %0\nlv.s S202, 8 + %0\nvzero.q C230\n"
                     : : "m"(*(const Point *)xyz) : "memory");
#define POINT_CODE(X,Y,Z,W,RESULT) \
    __asm__ volatile("vscl.q C210, " X ", S200\nvscl.q C220, " Y ", S201\n" \
        "vadd.q C210, C210, C220\nvscl.q C220, " Z ", S202\n" \
        "vadd.q C210, C210, C220\nvadd.q C210, C210, " W "\n" \
        "vcmp.q LT, C210, C230\nvnop\nmfvc %0, $131\n" : "=r"(RESULT) : : "memory")
    if(mask&15)POINT_CODE("C000","C010","C020","C030",a);
    if(mask&48)POINT_CODE("C100","C110","C120","C130",b);
#undef POINT_CODE
    return ((a&15)|((b&3)<<4))&mask;
}
/* input points must have initialized padding through the next four vertices.
   skyclip owns a 12-vertex initialized array, so no load crosses its storage. */
static inline void clip_math_polygon_distances(const JgsVert input[12],unsigned count,
                                               const float plane[4],float out[12])
{
    typedef struct {float v[4];} Four;
    __asm__ volatile("lv.s S100, 0 + %0\nlv.s S101, 4 + %0\nlv.s S102, 8 + %0\nlv.s S103, 12 + %0\n"
                     "vscl.q C110, C100[1,1,1,1], S103\n" : : "m"(*(const Four *)plane) : "memory");
    for(unsigned i=0;i<count;i+=4) {
        typedef struct {JgsVert v[4];} FourVertices;
        __asm__ volatile(
            "lv.s S000, 12 + %1\nlv.s S010, 16 + %1\nlv.s S020, 20 + %1\n"
            "lv.s S001, 36 + %1\nlv.s S011, 40 + %1\nlv.s S021, 44 + %1\n"
            "lv.s S002, 60 + %1\nlv.s S012, 64 + %1\nlv.s S022, 68 + %1\n"
            "lv.s S003, 84 + %1\nlv.s S013, 88 + %1\nlv.s S023, 92 + %1\n"
            "vscl.q C200, C000, S100\nvscl.q C210, C010, S101\nvadd.q C200, C200, C210\n"
            "vscl.q C210, C020, S102\nvadd.q C200, C200, C210\nvadd.q C200, C200, C110\n"
            "sv.q C200, %0\n"
            : "=m"(*(Four *)(out+i)) : "m"(*(const FourVertices *)(input+i)) : "memory");
    }
}
#else
#define CLIP_MATH_VFPU 0
static inline void clip_math_load(const ClipMathPlanes *p){(void)p;}
static inline void clip_math_distances(const ClipMathPlanes *p,const void *xyz,float out[8])
{
    float point[3];memcpy(point,xyz,sizeof point);
    for(unsigned i=0;i<8;i++) {
        const float (*c)[4]=p->coefficient[i/4];unsigned k=i%4;
        out[i]=c[0][k]*point[0]+c[1][k]*point[1]+c[2][k]*point[2]+c[3][k];
    }
}
static inline void clip_math_polygon_distances(const JgsVert input[12],unsigned count,
                                               const float plane[4],float out[12])
{
    for(unsigned i=0;i<count;i++)out[i]=plane[0]*input[i].x+plane[1]*input[i].y+plane[2]*input[i].z+plane[3];
}
static inline int clip_math_sphere_mask(const ClipMathPlanes *p,const void *xyz,float radius)
{
    float distances[8] __attribute__((aligned(16)));clip_math_distances(p,xyz,distances);
    int mask=0;
    for(unsigned i=0;i<6;i++) {
        if(distances[i]<-radius)return -1;
        if(distances[i]<radius)mask|=1<<i;
    }
    return mask;
}
static inline int clip_math_sphere_visible(const ClipMathPlanes *p,const void *xyz,float radius)
{return clip_math_sphere_mask(p,xyz,radius)>=0;}
static inline int clip_math_box_inside4(const ClipMathPlanes *p,const float center[3],const float extent[3],float margin,unsigned mask)
{
    const float (*c)[4]=p->coefficient[0];
    for(unsigned k=0;k<4;k++)if(mask&(1u<<k)) {
        float d=c[0][k]*center[0]+c[1][k]*center[1]+c[2][k]*center[2]+c[3][k];
        d-=fabsf(c[0][k])*extent[0];d-=fabsf(c[1][k])*extent[1];d-=fabsf(c[2][k])*extent[2];
        if(!(d>margin))return 0;
    }
    return 1;
}
static inline unsigned clip_math_point_code(const ClipMathPlanes *p,const void *xyz,unsigned mask)
{
    float distances[8];clip_math_distances(p,xyz,distances);unsigned code=0;
    for(unsigned i=0;i<6;i++)if((mask&(1u<<i)) && distances[i]<0)code|=1u<<i;
    return code;
}
#endif
#endif
