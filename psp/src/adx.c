#include "adx.h"
#include <math.h>
#include <limits.h>
/* SH4 FTRC saturates to s32; the decoder then writes its upper halfword.
   Avoid an out-of-range C conversion (undefined behavior), including NaN. */
static int16_t sample(float v)
{
    int32_t value;
    if(isnan(v) || v<=-2147483648.f)value=INT32_MIN;
    else if(v>=2147483648.f)value=INT32_MAX;
    else value=(int32_t)v;
    return (int16_t)((uint32_t)value>>16);
}
int adx_decode_continuous(const unsigned char *input,unsigned bytes,int blocks,
                    int16_t *output,unsigned samples,float history[2],
                    int16_t coefficient1,int16_t coefficient2)
{
    if(!input || !output || !history || blocks<0 || (unsigned)blocks>bytes/18 ||
       (unsigned)blocks>samples/32)return -1;
    float h0=history[0],h1=history[1];
    float c1=coefficient1/4096.f,c2=coefficient2/4096.f;
    for(int block=0;block<blocks;block++) {
        const unsigned char *p=input+block*18;unsigned word=(unsigned)p[0]*256+p[1];
        /* Early marker exits without publishing history, just like 297cf6. */
        if(word&0x8000)return block;
        float scale=(word&0x1fff)+1;
        for(int j=0;j<32;j++) {
            int nibble=j&1 ? p[2+j/2]&15:p[2+j/2]>>4;
            if(nibble>=8)nibble-=16;
            /* 297c8a..ca2: FMUL, FMAC, FMAC. The nibble tables at 2a6720
               and 2a6b20 contain signed nibble*65536, not unscaled s16. */
            float v=c2*h1;
            v=fmaf(c1,h0,v);
#ifdef JGR_ADX_REFERENCE
            v=fmaf(scale,nibble*65536.f,v);
#else
            /* scale is 1..8192 and nibble is -8..7. Their integer product
               has at most 17 significant bits, so scaling by 2^16 is exact
               in binary32. One FPU addition is the same single rounding as
               this FMAC, without PSP libm's software-double machinery. */
            v+=(float)(nibble*(int)scale)*65536.f;
#endif
            h1=h0;h0=v;output[block*32+j]=sample(v);
        }
    }
    history[0]=h0;history[1]=h1;return blocks;
}
int adx_decode_mono(const unsigned char *input,unsigned bytes,int blocks,
                    int16_t *output,unsigned samples,int16_t history[2],
                    int16_t coefficient1,int16_t coefficient2)
{
    if(!history)return -1;
    float h[2]={history[0]*65536.f,history[1]*65536.f};
    int n=adx_decode_continuous(input,bytes,blocks,output,samples,h,coefficient1,coefficient2);
    if(n==blocks){history[0]=sample(h[0]);history[1]=sample(h[1]);}
    return n;
}
