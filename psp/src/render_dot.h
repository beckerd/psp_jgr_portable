#ifndef JGR_RENDER_DOT_H
#define JGR_RENDER_DOT_H
#include "source_float_fast.h"
/* Exact ordered binary64 dot, accelerated for the normal finite range used
   by render transforms. Allegrex has a 32-bit FPU but a 64-bit integer
   product. Align the exact 24x24 products and reproduce each double sum's
   53-bit nearest/even rounding before the final float conversion. */
static inline float render_dot(const float *a,const float *b,unsigned n)
{
#ifndef JGR_RENDER_DOT_REFERENCE
    uint64_t product[4]={0};unsigned sign[4];int exponent[4],low=1000,high=-1000;
    for(unsigned i=0;i<n;i++) {
        uint32_t x,y;memcpy(&x,a+i,4);memcpy(&y,b+i,4);
        unsigned ex=(x>>23)&255,ey=(y>>23)&255;
        if(ex==255 || ey==255)goto fallback;
        if(!(x&0x7fffffff) || !(y&0x7fffffff))continue;
        if(!ex || !ey)goto fallback;
        exponent[i]=(int)ex+(int)ey-300;sign[i]=(x^y)>>31;
        if(exponent[i]<low)low=exponent[i];
        if(exponent[i]>high)high=exponent[i];
        if(high-low>12)goto fallback;
        product[i]=(uint64_t)((x&0x7fffffu)|0x800000u)*((y&0x7fffffu)|0x800000u);
    }
    if(low==1000)goto fallback; /* Preserve all-negative signed zero. */
    int64_t sum=0;
    for(unsigned i=0;i<n;i++)if(product[i]) {
        int64_t term=(int64_t)(product[i]<<(exponent[i]-low));
        sum+=sign[i] ? -term:term;
        uint64_t mag=sum<0 ? (uint64_t)-sum:(uint64_t)sum;
        unsigned upper=(unsigned)(mag>>32);
        if(upper) {
            int lost=11-__builtin_clz(upper); /* top bit - 52 */
            if(lost>0) {
                uint64_t half=UINT64_C(1)<<(lost-1),mask=(half<<1)-1,tail=mag&mask;
                uint64_t rounded=mag&~mask;
                if(tail>half || (tail==half && ((mag>>lost)&1)))rounded+=half<<1;
                sum=sum<0 ? -(int64_t)rounded:(int64_t)rounded;
            }
        }
    }
    float result;
    if(source_round_integer(sum<0 ? (uint64_t)-sum:(uint64_t)sum,low,sum<0 ? 0x80000000u:0,&result,0))return result;
fallback:;
#endif
    double total=(double)a[0]*b[0];
    for(unsigned i=1;i<n;i++)total+=(double)a[i]*b[i];
    return (float)total;
}
#endif
