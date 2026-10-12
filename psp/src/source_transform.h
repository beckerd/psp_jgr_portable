#ifndef JGR_SOURCE_TRANSFORM_H
#define JGR_SOURCE_TRANSFORM_H
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "render_math.h"
#include "vfpu_profile.h"
/* Transform independent rows together. A compensated product/sum supplies
   a rounding interval; use it only when that interval lies strictly inside
   one binary32 rounding cell. Boundary/extreme cases retain the ordered
   binary64 expression. No approximate position enters collision/camera. */
static inline float source_point_reference(const float *m,const float *p)
{double d=(double)m[0]*p[0];d+=(double)m[1]*p[1];d+=(double)m[2]*p[2];d+=m[3];return (float)d;}
static inline int source_point_range(float f)
{
    uint32_t b;memcpy(&b,&f,4);unsigned e=(b>>23)&255;
    return !(b&0x7fffffff) || (e>=97 && e<=157);
}
static inline void source_point_compensated(const float *m,const float *p,float *h,float *l,float *size)
{
#if defined(__PSP__) && !defined(JGR_SCALAR_SOURCE_TRANSFORM)
    /* Only lv.s/sv.s touch caller buffers: no PSP-1000 unaligned-vector
       load COP1 corruption. M0..M2 are preserved around the whole batch. */
    __asm__ volatile(
        "lv.s S200, 12(%0)\nlv.s S201, 28(%0)\nlv.s S202, 44(%0)\n"
        "vzero.t C210\nvabs.t C230, C200\n"
        : : "r"(m) : "memory");
    for(int k=0;k<3;k++) {
        /* A zero local coordinate contributes no nonzero product. The
           rounding-cell check below still sends cancellation/signed-zero
           results through the ordered reference expression. */
        if(p[k]==0.f)continue;
        const float *a=m+k;float split=4097.f;
        __asm__ volatile(
            "lv.s S000, 0(%0)\nlv.s S001, 16(%0)\nlv.s S002, 32(%0)\n"
            "mtv %1, S010\nvpfxs x,x,x,x\nvmov.t C010, C010\nmtv %2, S020\n"
            "vscl.t C020, C000, S020\nvsub.t C030, C020, C000\nvsub.t C020, C020, C030\nvsub.t C030, C000, C020\n"
            "mtv %2, S100\nvscl.t C100, C010, S100\nvsub.t C110, C100, C010\nvsub.t C100, C100, C110\nvsub.t C110, C010, C100\n"
            "vmul.t C120, C000, C010\nvabs.t C000, C120\nvadd.t C230, C230, C000\n"
            "vmul.t C130, C020, C100\nvsub.t C130, C130, C120\n"
            "vmul.t C000, C020, C110\nvadd.t C130, C130, C000\n"
            "vmul.t C000, C030, C100\nvadd.t C130, C130, C000\n"
            "vmul.t C000, C030, C110\nvadd.t C130, C130, C000\n"
            /* TwoSum(high, product), then add the low components. */
            "vadd.t C220, C200, C120\nvsub.t C000, C220, C200\n"
            "vsub.t C010, C220, C000\nvsub.t C010, C200, C010\n"
            "vsub.t C020, C120, C000\nvadd.t C010, C010, C020\n"
            "vadd.t C130, C130, C010\nvadd.t C210, C210, C130\n"
            /* General TwoSum again: cancellation need not preserve order. */
            "vadd.t C200, C220, C210\nvsub.t C000, C200, C220\n"
            "vsub.t C010, C200, C000\nvsub.t C010, C220, C010\n"
            "vsub.t C020, C210, C000\nvadd.t C210, C010, C020\n"
            : : "r"(a),"r"(p[k]),"r"(split) : "memory");
    }
    __asm__ volatile(
        "sv.s S200, 0(%0)\nsv.s S201, 4(%0)\nsv.s S202, 8(%0)\n"
        "sv.s S210, 0(%1)\nsv.s S211, 4(%1)\nsv.s S212, 8(%1)\n"
        "sv.s S230, 0(%2)\nsv.s S231, 4(%2)\nsv.s S232, 8(%2)\n"
        : : "r"(h),"r"(l),"r"(size) : "memory");
#else
    for(int r=0;r<3;r++) {
        h[r]=m[r*4+3];l[r]=0;size[r]=fabsf(h[r]);
        for(int k=0;k<3;k++) {
            if(p[k]==0.f)continue;
            float a=m[r*4+k],b=p[k],ca=4097.f*a,cb=4097.f*b;
            float ah=ca-(ca-a),al=a-ah,bh=cb-(cb-b),bl=b-bh;
            float product=a*b,error=((ah*bh-product)+ah*bl)+al*bh;error+=al*bl;
            size[r]+=fabsf(product);
            float sum=h[r]+product,bb=sum-h[r],e=(h[r]-(sum-bb))+(product-bb);
            l[r]+=error+e;float next=sum+l[r];bb=next-sum;
            l[r]=(sum-(next-bb))+(l[r]-bb);h[r]=next;
        }
    }
#endif
}
static inline unsigned source_point_owned(const float *m,const float *p,float *out)
{
#if (defined(__PSP__) && !defined(JGR_SCALAR_SOURCE_TRANSFORM)) || defined(JGR_TEST_SOURCE_TRANSFORM)
    int safe=1;for(int i=0;i<12;i++)safe&=source_point_range(m[i]);
    for(int i=0;i<3;i++)safe&=source_point_range(p[i]);
    if(safe) {
        if(p[0]==0.f && p[1]==0.f && p[2]==0.f) {
            for(int r=0;r<3;r++)out[r]=m[r*4+3]!=0.f ? m[r*4+3]:source_point_reference(m+r*4,p);
            return 0;
        }
        unsigned exact=0;
        float h[3],l[3],size[3];source_point_compensated(m,p,h,l,size);
        for(int r=0;r<3;r++) {
            uint32_t bits;memcpy(&bits,h+r,4);unsigned exp=(bits>>23)&255;
            /* 2^-36 of the absolute term sum generously covers compensated
               binary32 arithmetic and the reference's ordered binary64
               rounding. The smaller neighboring ULP handles powers of two. */
            float bound=size[r]*0x1p-36f+0x1p-90f,half=0;
            if(exp>25 && exp<250) {
                uint32_t u=(exp-24-((bits&0x7fffff)==0))<<23;memcpy(&half,&u,4);
            }
            if(half>0 && fabsf(l[r])+bound<half)out[r]=h[r];
            else {out[r]=source_point_reference(m+r*4,p);exact++;}
        }
        return exact;
    }
#endif
    for(int r=0;r<3;r++)out[r]=source_point_reference(m+r*4,p);
    return 3;
}
static inline void source_point_batch(const float *const *matrices,const float *const *points,float (*out)[3],int n)
{
#if defined(__PSP__) && !defined(JGR_SCALAR_SOURCE_TRANSFORM)
    RenderMathState saved;render_math_begin(&saved);
#endif
    unsigned stamp=vfpu_profile_transform_begin(),exact=0;
    for(int i=0;i<n;i++)exact+=source_point_owned(matrices[i],points[i],out[i]);
#if defined(__PSP__) && !defined(JGR_SCALAR_SOURCE_TRANSFORM)
    render_math_end(&saved);
#endif
    vfpu_profile_transform_end(stamp,n,exact);
}
static inline void source_point(const float *m,const float *p,float *out)
{
    /* Camera rotations and roots repeatedly translate by zero. They need
       no VFPU ownership/save/restore; retain the full signed-zero and
       nonfinite expression on the exceptional rows. */
    if(p[0]==0.f && p[1]==0.f && p[2]==0.f) {
        float translated[3];
        for(int r=0;r<3;r++) {
            const float *row=m+r*4;
            translated[r]=isfinite(row[0]) && isfinite(row[1]) && isfinite(row[2]) && row[3]!=0.f
                ? row[3]:source_point_reference(row,p);
        }
        memcpy(out,translated,sizeof translated);
        return;
    }
    const float *matrices[1]={m},*points[1]={p};float result[1][3];
    source_point_batch(matrices,points,result,1);memcpy(out,result[0],12);
}
/* Graffiti descriptors use column-major matrices and a mix of positions
   (W=1) and directions (W=0). Transpose once and retain the fourth product,
   including signed zero/nonfinite behavior, while owning VFPU once for
   the complete descriptor. Bit i selects W=0 for point i. */
static inline void source_column_points(const float column[16],const float (*points)[3],
                                       float (*out)[3],unsigned directions,int n)
{
    float rows[2][12];
    for(int r=0;r<3;r++) {
        for(int k=0;k<3;k++)rows[0][r*4+k]=rows[1][r*4+k]=column[k*4+r];
        rows[0][r*4+3]=column[12+r];rows[1][r*4+3]=column[12+r]*0.f;
    }
#if defined(__PSP__) && !defined(JGR_SCALAR_SOURCE_TRANSFORM)
    RenderMathState saved;render_math_begin(&saved);
#endif
    unsigned stamp=vfpu_profile_transform_begin(),exact=0;
    for(int i=0;i<n;i++,directions>>=1)
        exact+=source_point_owned(rows[directions&1],points[i],out[i]);
#if defined(__PSP__) && !defined(JGR_SCALAR_SOURCE_TRANSFORM)
    render_math_end(&saved);
#endif
    vfpu_profile_transform_end(stamp,n,exact);
}
#endif
