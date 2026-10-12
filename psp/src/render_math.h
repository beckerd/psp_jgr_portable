#ifndef JGR_RENDER_MATH_H
#define JGR_RENDER_MATH_H
#include "charpose.h"

/* Render-thread hierarchy math. Gameplay/collision retains the source-order
   scalar helper. Save/restore registers once around a hierarchy;
   sampling/local TRS inside that loop uses only scalar registers. */
typedef struct {float matrix[48];unsigned condition;} __attribute__((aligned(16))) RenderMathState;
#if defined(__PSP__)
/* Do not change the SDK's lazy ownership bookkeeping: the SDK revision used
   here invalidates a saved owner's valid bits when borrowing its matrices.
   Explicit preservation also works with callers that manage registers
   directly. Clipping also uses VFPU comparisons, so preserve VFPU_CC. */
static inline void render_math_begin(RenderMathState *state)
{
    __asm__ volatile("vnop\nmfvc %0, $131\n" : "=r"(state->condition) : : "memory");
    __asm__ volatile(
        "sv.q C000, 0 + %0\nsv.q C010, 16 + %0\nsv.q C020, 32 + %0\nsv.q C030, 48 + %0\n"
        "sv.q C100, 64 + %0\nsv.q C110, 80 + %0\nsv.q C120, 96 + %0\nsv.q C130, 112 + %0\n"
        "sv.q C200, 128 + %0\nsv.q C210, 144 + %0\nsv.q C220, 160 + %0\nsv.q C230, 176 + %0\n"
        : "=m"(state->matrix) : : "memory");
}
static inline void render_math_end(const RenderMathState *state)
{
    __asm__ volatile(
        "lv.q C000, 0 + %0\nlv.q C010, 16 + %0\nlv.q C020, 32 + %0\nlv.q C030, 48 + %0\n"
        "lv.q C100, 64 + %0\nlv.q C110, 80 + %0\nlv.q C120, 96 + %0\nlv.q C130, 112 + %0\n"
        "lv.q C200, 128 + %0\nlv.q C210, 144 + %0\nlv.q C220, 160 + %0\nlv.q C230, 176 + %0\n"
        : : "m"(state->matrix) : "memory");
    __asm__ volatile("mtvc %0, $131\nvnop\n" : : "r"(state->condition) : "memory");
}
#else
static inline void render_math_begin(RenderMathState *state){(void)state;}
static inline void render_math_end(const RenderMathState *state){(void)state;}
#endif
/* A caller may keep its saved context across adjacent owned math kernels.
   Borrowers must close it before entering GU/GUM or returning to a renderer
   that owns its own VFPU context. End is also safe after a borrower closed it. */
typedef struct {RenderMathState saved;int active;} RenderMathScope;
static inline void render_math_scope_begin(RenderMathScope *scope)
{render_math_begin(&scope->saved);scope->active=1;}
static inline int render_math_scope_active(const RenderMathScope *scope)
{return scope && scope->active;}
static inline void render_math_scope_end(RenderMathScope *scope)
{
    if(render_math_scope_active(scope)){render_math_end(&scope->saved);scope->active=0;}
}
#if defined(__PSP__) && !defined(JGR_SCALAR_RENDER_MATH)
typedef struct {float v[16];} RenderMatrix;
static inline void render_affine_multiply_unaligned(float *out,const float *a,const float *b)
{
    /* Vectorize the four independent columns, retaining each column's
       multiply/add order instead of a reassociated VFPU matrix/dot product.
       All inputs are loaded before output: out may alias either input.
       Unaligned loads/stores accept the existing 4-byte-aligned matrices.
       PSP-1000 lvl.q/lvr.q corrupt COP1 registers selected by the low five
       destination bits: C000/C010/C020 -> f0/f1/f2, C100/C110/C120 -> f4/f5/f6.
       Declare those clobbers even on newer models so GCC protects live floats. */
    __asm__ volatile(
        "ulv.q C000, 0 + %1\n"
        "ulv.q C010, 16 + %1\n"
        "ulv.q C020, 32 + %1\n"
        "ulv.q C100, 0 + %2\n"
        "ulv.q C110, 16 + %2\n"
        "ulv.q C120, 32 + %2\n"
        "vzero.q C220\n"
#define RENDER_AFFINE_ROW(X, Y, Z, W, OFFSET) \
        "vscl.q C200, C100, " X "\n" \
        "vadd.q C200, C220, C200\n" \
        "vscl.q C210, C110, " Y "\n" \
        "vadd.q C200, C200, C210\n" \
        "vscl.q C210, C120, " Z "\n" \
        "vadd.q C200, C200, C210\n" \
        "vadd.s S203, S203, " W "\n" \
        "usv.q C200, " OFFSET " + %0\n"
        RENDER_AFFINE_ROW("S000","S001","S002","S003","0")
        RENDER_AFFINE_ROW("S010","S011","S012","S013","16")
        RENDER_AFFINE_ROW("S020","S021","S022","S023","32")
#undef RENDER_AFFINE_ROW
        "vzero.q C230\n"
        "vone.s S233\n"
        "usv.q C230, 48 + %0\n"
        : "=m"(*(RenderMatrix *)out)
        : "m"(*(const RenderMatrix *)a),"m"(*(const RenderMatrix *)b)
        : "$f0","$f1","$f2","$f4","$f5","$f6","memory");
}
static inline void render_affine_multiply_aligned(float *out,const float *a,const float *b)
{
    /* All matrices are 16-byte aligned: one vector load/store per row,
       with no PSP-1000 unaligned-load COP1 side effects. Same arithmetic
       order as the general path, including signed-zero canonicalization. */
    __asm__ volatile(
        "lv.q C000, 0 + %1\n"
        "lv.q C010, 16 + %1\n"
        "lv.q C020, 32 + %1\n"
        "lv.q C100, 0 + %2\n"
        "lv.q C110, 16 + %2\n"
        "lv.q C120, 32 + %2\n"
        "vzero.q C220\n"
#define RENDER_AFFINE_ROW(X, Y, Z, W, OFFSET) \
        "vscl.q C200, C100, " X "\n" \
        "vadd.q C200, C220, C200\n" \
        "vscl.q C210, C110, " Y "\n" \
        "vadd.q C200, C200, C210\n" \
        "vscl.q C210, C120, " Z "\n" \
        "vadd.q C200, C200, C210\n" \
        "vadd.s S203, S203, " W "\n" \
        "sv.q C200, " OFFSET " + %0\n"
        RENDER_AFFINE_ROW("S000","S001","S002","S003","0")
        RENDER_AFFINE_ROW("S010","S011","S012","S013","16")
        RENDER_AFFINE_ROW("S020","S021","S022","S023","32")
#undef RENDER_AFFINE_ROW
        "vzero.q C230\n"
        "vone.s S233\n"
        "sv.q C230, 48 + %0\n"
        : "=m"(*(RenderMatrix *)out)
        : "m"(*(const RenderMatrix *)a),"m"(*(const RenderMatrix *)b)
        : "memory");
}
static inline void render_affine_multiply(float *out,const float *a,const float *b)
{
#ifndef JGR_UNALIGNED_RENDER_REFERENCE
    if(!(((unsigned long)out|(unsigned long)a|(unsigned long)b)&15u))
        render_affine_multiply_aligned(out,a,b);
    else
#endif
        render_affine_multiply_unaligned(out,a,b);
}

#else
static inline void render_affine_multiply(float *out,const float *a,const float *b)
{chars_affine_multiply(out,a,b);}
#endif
#endif
