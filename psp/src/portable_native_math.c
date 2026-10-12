/* Source-order math for setup-only animation evaluation.
 * The original positive-base power path evaluates exp(log(x)*y), including
 * its own binary32 rational polynomials, rather than a correctly rounded
 * integer-power library call. The coefficients come from the user's file.
 * No original instruction bytes or polynomial coefficient values are here.
 * Scope is the Bernstein animator's nonnegative base and integer exponent
 * 0..10. Other arguments fail to the source NaN result, not an approximation.
 * Source operations: 011d08 logarithm, 011b90 exponential, 011910 scaling,
 * and the positive-base path of 011e34/0139ec. */
#include "portable_native_math.h"
#include "source_fmac.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MATH_BASE 0x8c101890u
#define MATH_BYTES 0x2b0u
static unsigned char native_math[MATH_BYTES];
static int native_math_ready;
static unsigned bits(float value){unsigned word;memcpy(&word,&value,4);return word;}
static float value(unsigned word){float result;memcpy(&result,&word,4);return result;}
static unsigned word(const unsigned char *p)
{return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static float coefficient(unsigned address)
{return value(word(native_math+(address-MATH_BASE)));}
int jgr_native_math_load(JgrDisc *disc)
{
    JgrDiscFile executable;native_math_ready=0;
    if(!disc || jgr_disc_find(disc,"1ST_READ.BIN",&executable) ||
       jgr_disc_read(disc,&executable,MATH_BASE-0x8c010000u,native_math,sizeof native_math))return 0;
    native_math_ready=1;return 1;
}
void jgr_native_math_clear(void)
{memset(native_math,0,sizeof native_math);native_math_ready=0;}
static float source_log_positive(float x)
{
    if(x==coefficient(0x8c101b0c))return coefficient(0x8c101b04);
    unsigned u=bits(x);
    float normalized=value((u&0x807fffffu)|0x3f800000u);
    int exponent=(int)((u&0x7f800000u)>>23)-127;
    float scale=((float)exponent+coefficient(0x8c101b10))*coefficient(0x8c101b30);
    float ratio=(coefficient(0x8c1018d4)+normalized)/(coefficient(0x8c1018d0)+normalized);
    float square=ratio*ratio,polynomial=coefficient(0x8c1018d8);
    for(unsigned i=1;i<=7;i++)polynomial=source_fmac(square,polynomial,coefficient(0x8c1018d8+i*4));
    return scale+ratio*polynomial;
}
static float source_scale(float x,int shift)
{
    unsigned u=bits(x),exponent=(u&0x7f800000u)>>23;
    if(exponent==255){
        if(u&0x7fffffu)return coefficient(0x8c101b00);
        return coefficient(u&0x80000000u?0x8c101b38:0x8c101b08);
    }
    if(!exponent)return coefficient(u&0x80000000u?0x8c101b34:0x8c101b04);
    int combined=(int)exponent+shift;
    if(combined<=0 || combined>=255){
        if(shift<0)return coefficient(u&0x80000000u?0x8c101b34:0x8c101b04);
        return coefficient(u&0x80000000u?0x8c101b38:0x8c101b08);
    }
    return value((u&0x807fffffu)|((unsigned)combined<<23));
}
static float source_exp(float x)
{
    if(x==0.f)return coefficient(0x8c101b0c);
    unsigned u=bits(x);
    if((int)((u&0x7f800000u)>>23)-127>=15)
        return coefficient(u&0x80000000u?0x8c101b04:0x8c101b08);
    float scaled=x*coefficient(0x8c1018c8);
    int shift=(int)scaled;
    float remainder=scaled-(float)shift;
    /* The original keeps the nearest integral exponent, resolving exactly
     * half-sized fractions away from zero before evaluating its rational. */
    if((int)((bits(remainder)&0x7f800000u)>>23)-127>=-1){
        if(bits(remainder)&0x80000000u){remainder=coefficient(0x8c101b0c)+remainder;shift--;}
        else{remainder=remainder+coefficient(0x8c101b3c);shift++;}
    }
    float t=coefficient(0x8c101b30)*remainder;
    float numerator=coefficient(0x8c101890),denominator=coefficient(0x8c1018ac);
    for(unsigned i=1;i<=6;i++)numerator=source_fmac(t,numerator,coefficient(0x8c101890+i*4));
    for(unsigned i=1;i<=6;i++)denominator=source_fmac(t,denominator,coefficient(0x8c1018ac+i*4));
    return source_scale(numerator/denominator,shift);
}
float jgr_native_math_power(float base,float exponent)
{
    if(!native_math_ready)return NAN;
    if(!(base>=0.f && base<=1.f && exponent>=0.f && exponent<=10.f) || exponent!=(float)(int)exponent)
        return coefficient(0x8c101b00);
    if(base==0.f)return coefficient(exponent==0.f?0x8c101b00:0x8c101b04);
    if(exponent==0.f)return coefficient(0x8c101b0c);
    float logarithm=source_log_positive(base);
    return source_exp(logarithm*exponent);
}
