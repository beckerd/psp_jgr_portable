#ifndef JGR_SOURCE_FMAC_H
#define JGR_SOURCE_FMAC_H
#include <stdint.h>
#include <string.h>
#include "source_float_fast.h"

/* Binary32 multiply-add with ONE final nearest/even rounding. The installed
   PSP libm fmaf is (float)((double)x*y+z): rounding the sum to double first
   can lose a tiny residual exactly at a binary32 midpoint (probe case 1326).

   A binary32 product is exact in binary64, whose exponent range also contains
   every finite binary32 product. Only a binary32 midpoint needs correction.
   TwoSum recovers the double addition's residual; move one double ULP toward
   the exact sum before the final cast. An exact midpoint stays ties-to-even.
   Keep -ffp-contract=off, including the double error-recovery expressions. */
static inline float source_fmac(float x,float y,float z)
{
#ifndef JGR_FMAC_REFERENCE
    /* Exact products need no binary64 emulation. Axis-aligned collision
       planes hit these cases frequently on the PSP's single-precision FPU.
       Keep the addition for signed zero when either factor is zero. */
    if(x==0.f || y==0.f)return x*y+z;
    if(x==1.f)return y+z;
    if(y==1.f)return x+z;
    if(x==-1.f)return -y+z;
    if(y==-1.f)return -x+z;
    /* Both factors are nonzero here: rounding their product once is the
       fused result even when it underflows to a signed zero. */
    if(z==0.f)return x*y;
    /* Allegrex has no fused multiply-add and no binary64 FPU. For normal
       operands with nearby exponents, an integer 24x24 product and aligned
       sum retain EVERY bit, followed by one nearest/even binary32 rounding.
       This is the same FMAC result without software-double conversions.
       Wider exponent gaps and subnormal/overflow results retain the full
       reference below. No approximation or changed contact tolerance. */
    float fast;if(source_fast_fmac(x,y,z,&fast,0))return fast;
#endif
    double product=(double)x*y,sum=product+(double)z;
    uint64_t bits;memcpy(&bits,&sum,8);
    int exponent=(int)((bits>>52)&2047)-1023;
    if(exponent>=-150 && exponent<=127) {
        unsigned lost=exponent>=-126 ? 29:(unsigned)(-97-exponent);
        uint64_t significand=(bits&UINT64_C(0x000fffffffffffff))|UINT64_C(0x0010000000000000);
        uint64_t half=UINT64_C(1)<<(lost-1),mask=(half<<1)-1;
        if((significand&mask)==half) {
            double b=sum-product;
            double error=(product-(sum-b))+((double)z-b);
            if(error!=0.) {
                bits+=((sum>0.)==(error>0.)) ? UINT64_C(1):UINT64_MAX;
                memcpy(&sum,&bits,8);
            }
        }
    }
    return (float)sum;
}
#endif
