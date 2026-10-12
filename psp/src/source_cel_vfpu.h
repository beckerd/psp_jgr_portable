#ifndef JGR_SOURCE_CEL_VFPU_H
#define JGR_SOURCE_CEL_VFPU_H
#include "source_light.h"
#include <string.h>

/* This kernel owns only M0. Unlike the hierarchy/clip kernels it neither
   reads nor writes VFPU_CC. Preserve the four touched vectors once around
   a complete packet; callers close this scope before entering GU/GUM. */
typedef struct {float matrix[16] __attribute__((aligned(16)));int active;} SourceCelVfpuScope;
static inline void source_cel_vfpu_begin(SourceCelVfpuScope *scope)
{
#if defined(__PSP__) && !defined(JGR_SCALAR_RENDER_MATH)
    __asm__ volatile("sv.q C000,0(%0)\nsv.q C010,16(%0)\nsv.q C020,32(%0)\nsv.q C030,48(%0)\n"
        : : "r"(scope->matrix):"memory");
#endif
    scope->active=1;
}
static inline void source_cel_vfpu_end(SourceCelVfpuScope *scope)
{
    if(!scope->active)return;
#if defined(__PSP__) && !defined(JGR_SCALAR_RENDER_MATH)
    __asm__ volatile("lv.q C000,0(%0)\nlv.q C010,16(%0)\nlv.q C020,32(%0)\nlv.q C030,48(%0)\n"
        : : "r"(scope->matrix):"memory");
#endif
    scope->active=0;
}

/* Frame-owned GU output and the CPU skin workspace are 16-byte aligned.
   Preserve bits, including NaN payloads/material words; use exact-size tails
   and a scalar fallback for any unexpected alignment. Source and destination
   do not overlap. The caller already owns the same M0 emission scope. */
static inline void source_cel_vfpu_copy(void *destination,const void *source,unsigned bytes)
{
    unsigned char *out=destination;const unsigned char *in=source;
#if defined(__PSP__) && !defined(JGR_SCALAR_RENDER_MATH)
    if(!(((uintptr_t)out|(uintptr_t)in)&15u)) {
        while(bytes>=64u) {
            __asm__ volatile(
                "lv.q C000,0(%1)\nlv.q C010,16(%1)\nlv.q C020,32(%1)\nlv.q C030,48(%1)\n"
                "sv.q C000,0(%0)\nsv.q C010,16(%0)\nsv.q C020,32(%0)\nsv.q C030,48(%0)\n"
                : : "r"(out),"r"(in):"memory");
            out+=64;in+=64;bytes-=64;
        }
        while(bytes>=16u) {
            __asm__ volatile("lv.q C000,0(%1)\nsv.q C000,0(%0)\n" : : "r"(out),"r"(in):"memory");
            out+=16;in+=16;bytes-=16;
        }
    }
#endif
    while(bytes--)*out++=*in++;
}

/* The caller owns M0 for the complete packet emission. Keep the two
   directed-edge division and colour calculations scalar; only the five
   independent lerps run on VFPU. No dot product, reciprocal approximation,
   reassociation, change of vertex format, or read beyond either vertex. */
static inline __attribute__((always_inline)) void source_cel_vfpu_edge(const SourceLitVertex *a,const SourceLitVertex *b,
    float sa,float sb,uint32_t material,SourceLitVertex out[2],
    const float rgb[2][3],const float *offset,SourceCelPalette *colors)
{
    float t=sa/(sa-sb);
    uint32_t color0=source_cel_colour(material,0,rgb,offset,colors);
    uint32_t color1=source_cel_colour(material,1,rgb,offset,colors);
#if defined(__PSP__) && !defined(JGR_SCALAR_RENDER_MATH)
    unsigned factor;memcpy(&factor,&t,sizeof factor);
    /* The first quad contains U,V,colour,X. The colour lane is irrelevant
       during arithmetic and replaced with its exact integer bits before
       stores. Y/Z use individual loads, so the final source record needs
       no padding. PSP-1000 unaligned loads into C000/C010 also clobber COP1
       f0/f1; declaring them protects surrounding scalar division/colour work. */
    __asm__ volatile(
        "mtv %3, S033\n"
        "ulv.q C000, 0(%1)\nulv.q C010, 0(%2)\n"
        "lv.s S020, 16(%1)\nlv.s S021, 20(%1)\n"
        "lv.s S030, 16(%2)\nlv.s S031, 20(%2)\n"
        "vsub.q C010, C010, C000\nvsub.p C030, C030, C020\n"
        "vscl.q C010, C010, S033\nvscl.p C030, C030, S033\n"
        "vadd.q C010, C000, C010\nvadd.p C030, C020, C030\n"
        "mtv %4, S012\nusv.q C010, 0(%0)\nsv.s S030, 16(%0)\nsv.s S031, 20(%0)\n"
        "mtv %5, S012\nusv.q C010, 24(%0)\nsv.s S030, 40(%0)\nsv.s S031, 44(%0)\n"
        : : "r"(out),"r"(a),"r"(b),"r"(factor),"r"(color0),"r"(color1)
        : "$f0","$f1","memory");
#else
    float u=a->u,v=a->v,x=a->x,y=a->y,z=a->z;
    u+=(b->u-u)*t;v+=(b->v-v)*t;x+=(b->x-x)*t;y+=(b->y-y)*t;z+=(b->z-z)*t;
    out[0].u=out[1].u=u;out[0].v=out[1].v=v;
    out[0].x=out[1].x=x;out[0].y=out[1].y=y;out[0].z=out[1].z=z;
    out[0].color=color0;out[1].color=color1;
#endif
}
#endif
