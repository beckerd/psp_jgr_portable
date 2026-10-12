#ifndef JGR_REVERB_H
#define JGR_REVERB_H
#include <stdint.h>
#include <string.h>
/* Original RBL=2 allocates 131072 words. This program reads/writes offsets
   0..18294 and writes offset zero every sample: every read sees a write at
   most 18294 samples old. A 32768-word PSP ring preserves that history and
   TEMP's modulo-128 phase. Tests compare against the full original ring
   across multiple wraps. No delay time or coefficient is shortened. */
#define SOURCE_REVERB_MASK 32767u
#ifdef SOURCE_REVERB_PACKED
typedef uint16_t SourceReverbWord;
#else
typedef int32_t SourceReverbWord;
#endif
typedef struct {int32_t temp[128],mem[32];SourceReverbWord ring[SOURCE_REVERB_MASK+1];unsigned cursor,started;} SourceReverb;
static inline int source_reverb_clip(int v){return v< -8388608?-8388608:v>8388607?8388607:v;}
static inline int source_reverb_multiply(int x,int coefficient)
{
#ifndef SOURCE_REVERB_PORTABLE
    /* Exact fixed-point identities for 19 of reverb1's 53 multiplies.
       Keep floor rounding for negative values; x+4095 would overflow for
       unrestricted positive int inputs. Constant callers fold the tests. */
    if(coefficient==1024)return x>>2;
    if(coefficient==4095)return x-(x>>12)-((x&4095)!=0);
#endif
#if defined(__PSP__) && !defined(SOURCE_REVERB_PORTABLE)
    /* GCC expands constant int64 products into long shift/add chains on
       Allegrex. Its signed 32x32 MULT supplies the same 64-bit product. */
    unsigned lo,hi;
    __asm__("mult %2,%3\n\tmflo %0\n\tmfhi %1"
            : "=&r"(lo),"=&r"(hi) : "r"(x),"r"(coefficient) : "hi","lo");
    return (int)((lo>>12)|(hi<<20));
#else
    return (int)(((int64_t)x*coefficient)>>12);
#endif
}
/* AICA's 1-sign/4-exponent/11-mantissa delay-memory encoding. */
static inline uint16_t source_reverb_pack(int value)
{
    uint32_t v=(uint32_t)value,sign=(v>>23)&1,t=(v^(v<<1))&0xffffff;
    /* Same capped 24-bit leading-zero count, one Allegrex CLZ instead
       of up to twelve iterations at every delay-memory write. */
    unsigned exponent=__builtin_clz(t|0x800u)-8;
    v<<=exponent<12?exponent:11;
    return (uint16_t)(((v>>11)&2047)|(sign<<15)|(exponent<<11));
}
static inline int source_reverb_unpack(unsigned v)
{
    unsigned sign=v>>15,exponent=v>>11&15;
    uint32_t bits=((v&2047)<<11)|(sign<<22);
    if(exponent>11)exponent=11;else bits^=1u<<22;
    bits|=sign<<23;
    int value=(bits&0x800000)?(int)bits-0x1000000:(int)bits;
    return value>>exponent;
}
/* Store the EXACT decoded delay value. AICA pack/unpack discards the
   low max(0, 11-exponent) bits of a signed 24-bit value. Quantize at each
   write, preserving the delay codec, but avoid decoding every repeated read.
   This costs 64 KiB over the packed ring, with no new per-callback memory. */
static inline int source_reverb_quantize(int value)
{
    uint32_t v=(uint32_t)value,t=(v^(v<<1))&0xffffff;
    unsigned exponent=__builtin_clz(t|0x800u)-8;
    unsigned lost=exponent<11 ? 11-exponent:0;
    return (int)(v&(~0u<<lost));
}
static inline int source_reverb_read(const SourceReverb *s,unsigned at)
{
#ifdef SOURCE_REVERB_PACKED
    return source_reverb_unpack(s->ring[at]);
#else
    return s->ring[at];
#endif
}
static inline void source_reverb_write(SourceReverb *s,unsigned at,int value)
{
#ifdef SOURCE_REVERB_PACKED
    s->ring[at]=source_reverb_pack(value);
#else
    s->ring[at]=source_reverb_quantize(value);
#endif
}
static inline void source_reverb_reset(SourceReverb *s)
{
    memset(s,0,sizeof *s);s->cursor=1;
    /* Original MANATEE 8810 fills delay RAM with 60006000, NOT zero. */
#ifdef SOURCE_REVERB_PACKED
    for(unsigned i=0;i<=SOURCE_REVERB_MASK;i++)s->ring[i]=0x6000;
#endif
}
#if JGR_PORTABLE_ASSETS
#include "source_data_reverb_program.inc"
#else
#include "reverb_program.inc"
#endif
static inline void source_reverb_next(SourceReverb *s,int input,int out[2])
{
    /* All initialized delay words decode to zero. Until the first nonzero
       send, the program can only write zeros; preserve its cursor phase.
       Once excited, ALWAYS run it: silence at the input is not a tail cut. */
    if(!s->started && !input) {
        s->cursor=s->cursor==1 ? SOURCE_REVERB_MASK+1:s->cursor-1;
        out[0]=out[1]=0;return;
    }
    s->started=1;source_reverb_program(s,input,out);
}
#endif
