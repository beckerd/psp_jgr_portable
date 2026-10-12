#ifndef JGR_SOURCE_FLOAT_FAST_H
#define JGR_SOURCE_FLOAT_FAST_H
#include <stdint.h>
#include <string.h>
/* Exact normal-number arithmetic for Allegrex's integer multiplier. Every
   aligned significand fits below bit 63. Return 0 when the original scalar
   path is required; this is numeric dispatch, not an approximate result. */
static inline int source_round_integer(uint64_t magnitude,int exponent,uint32_t sign,float *out,int exact_double)
{
    if(!magnitude){*out=0.f;return 1;}
    unsigned upper=(unsigned)(magnitude>>32);
    int bit=upper ? 63-__builtin_clz(upper):31-__builtin_clz((unsigned)magnitude);
    /* Preserve legacy libm's binary64 intermediate where requested. */
    if(exact_double && bit>52)return 0;
    int power=exponent+bit;
    if(power< -126 || power>127)return 0;
    uint32_t mantissa;
    if(bit<=23)mantissa=(uint32_t)magnitude<<(23-bit);
    else {
        unsigned lost=bit-23;mantissa=(uint32_t)(magnitude>>lost);
        uint64_t remainder=magnitude-((uint64_t)mantissa<<lost),half=UINT64_C(1)<<(lost-1);
        if(remainder>half || (remainder==half && (mantissa&1)))mantissa++;
    }
    uint32_t result=sign|((unsigned)(power+126)*0x800000u+mantissa);
    memcpy(out,&result,4);return 1;
}
static inline int source_add_products(uint64_t p,uint64_t q,int exponent,int shift,uint32_t sign,uint32_t signq,float *out,int exact_double)
{
    if(shift>=0)q<<=shift;else {p<<=-shift;exponent+=shift;}
    uint64_t magnitude;
    if(sign==signq)magnitude=p+q;
    else if(p>=q)magnitude=p-q;
    else {magnitude=q-p;sign=signq;}
    return source_round_integer(magnitude,exponent,sign,out,exact_double);
}
static inline int source_fast_fmac(float x,float y,float z,float *out,int exact_double)
{
    uint32_t ix,iy,iz;memcpy(&ix,&x,4);memcpy(&iy,&y,4);memcpy(&iz,&z,4);
    unsigned ex=(ix>>23)&255,ey=(iy>>23)&255,ez=(iz>>23)&255;
    int shift=(int)ez-(int)ex-(int)ey+150;
    if(!ex || ex==255 || !ey || ey==255 || !ez || ez==255 || shift< -14 || shift>39)return 0;
    uint64_t p=(uint64_t)((ix&0x7fffffu)|0x800000u)*((iy&0x7fffffu)|0x800000u);
    uint64_t q=(iz&0x7fffffu)|0x800000u;
    return source_add_products(p,q,(int)ex+(int)ey-300,shift,(ix^iy)&0x80000000u,iz&0x80000000u,out,exact_double);
}
/* Two products whose aligned sum is exactly representable in binary64.
   This preserves Ninja FTRV's double sum and final float rounding. */
static inline int source_fast_dot2(float a,float b,float c,float d,float *out)
{
    uint32_t ia,ib,ic,id;memcpy(&ia,&a,4);memcpy(&ib,&b,4);memcpy(&ic,&c,4);memcpy(&id,&d,4);
    unsigned ea=(ia>>23)&255,eb=(ib>>23)&255,ec=(ic>>23)&255,ed=(id>>23)&255;
    int shift=(int)ec+(int)ed-(int)ea-(int)eb;
    if(!ea || ea==255 || !eb || eb==255 || !ec || ec==255 || !ed || ed==255 || shift< -5 || shift>5)return 0;
    uint64_t p=(uint64_t)((ia&0x7fffffu)|0x800000u)*((ib&0x7fffffu)|0x800000u);
    uint64_t q=(uint64_t)((ic&0x7fffffu)|0x800000u)*((id&0x7fffffu)|0x800000u);
    return source_add_products(p,q,(int)ea+(int)eb-300,shift,(ia^ib)&0x80000000u,(ic^id)&0x80000000u,out,1);
}
/* Three 48-bit products within three exponent steps sum in at most 53
   bits. Both ordered binary64 additions are therefore exact as well. */
static inline int source_fast_dot3(const float *a,const float *b,float *out)
{
    uint64_t product[3];unsigned sign[3];int exponent[3],lowest=1000,highest=-1000;
    for(int k=0;k<3;k++) {
        uint32_t x,y;memcpy(&x,a+k,4);memcpy(&y,b+k,4);
        unsigned ex=(x>>23)&255,ey=(y>>23)&255;
        if(!ex || ex==255 || !ey || ey==255)return 0;
        exponent[k]=(int)ex+(int)ey-300;sign[k]=(x^y)>>31;
        if(exponent[k]<lowest)lowest=exponent[k];
        if(exponent[k]>highest)highest=exponent[k];
        if(highest-lowest>3)return 0;
        product[k]=(uint64_t)((x&0x7fffffu)|0x800000u)*((y&0x7fffffu)|0x800000u);
    }
    int64_t sum=0;
    for(int k=0;k<3;k++) {
        int64_t value=(int64_t)(product[k]<<(exponent[k]-lowest));
        sum+=sign[k] ? -value:value;
    }
    return source_round_integer(sum<0 ? (uint64_t)-sum:(uint64_t)sum,lowest,sum<0 ? 0x80000000u:0,out,1);
}
#endif
