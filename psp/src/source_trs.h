#ifndef JGR_SOURCE_TRS_H
#define JGR_SOURCE_TRS_H
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "source_float_fast.h"
#include "source_transform.h"
extern float player_fsca_sin(int);
/* A rotation column has exactly two nonzero coefficients. Removing its
   zero products preserves the ordered binary64 sum for every nonzero
   result, without sending those products/adds through PSP software double.
   Preserve signed zero explicitly; use the full expression for nonfinite
   rows so zero*infinity still produces the reference NaN. */
static inline float source_trs_column(const float *m,const float *v,int first,int second)
{
#ifndef SOURCE_TRS_PORTABLE
    if(isfinite(m[0]) && isfinite(m[1]) && isfinite(m[2]) && isfinite(m[3])) {
        /* Axis-aligned parent columns have an exact zero term. The
           remaining binary32 product rounds identically to the original
           binary64 product followed by its zero additions. Avoid software
           double on these very common collision/effect hierarchy rows. */
        int za=m[first]==0.f || v[first]==0.f;
        int zb=m[second]==0.f || v[second]==0.f;
        if(za!=zb)return za ? m[second]*v[second]:m[first]*v[first];
        if(!za) {
            float fast;
            if(source_fast_dot2(m[first],v[first],m[second],v[second],&fast))return fast;
            double sum=(double)m[first]*v[first]+(double)m[second]*v[second];
            if(sum!=0.)return (float)sum;
        }
        uint32_t negative=0x80000000u;
        for(int k=0;k<4;k++){uint32_t a,b;memcpy(&a,m+k,4);memcpy(&b,v+k,4);negative&=a^b;}
        /* Exact cancellation of nonzero products rounds to positive zero.
           Negative zero requires all four original products to be -0. */
        if(m[first]!=0.f && v[first]!=0.f)negative=0;
        float zero;memcpy(&zero,&negative,4);return zero;
    }
#endif
    double sum=(double)m[0]*v[0];sum+=(double)m[1]*v[1];
    sum+=(double)m[2]*v[2];sum+=(double)m[3]*v[3];return (float)sum;
}
/* Ninja 27a06c(r5=0): Z, then Y, then X. Each nonzero rotation updates
   only its two columns through FTRV, including zero products. Do not expand
   into one scalar formula or contract across the rounded matrix stages. */
static inline void source_trs_rotate(const int32_t angles[3],const float scale[3],float m[16],float (*sin_bam)(int),int owned)
{
    for(int axis=2;axis>=0;axis--)if(angles[axis]) {
        unsigned a=(unsigned)angles[axis];float s=sin_bam(a),c=sin_bam(a+16384u);
        float vectors[2][4]={{0}},columns[2][4];int first,second;
        if(axis==2){first=0;second=1;vectors[0][0]=c;vectors[0][1]=s;vectors[1][0]=-s;vectors[1][1]=c;}
        else if(axis==1){first=0;second=2;vectors[0][0]=c;vectors[0][2]=-s;vectors[1][0]=s;vectors[1][2]=c;}
        else {first=1;second=2;vectors[0][1]=c;vectors[0][2]=s;vectors[1][1]=-s;vectors[1][2]=c;}
        int rows=4;
#ifndef SOURCE_TRS_PORTABLE
        /* An affine bottom row stays (0,0,0,1). Its last +0 product also
           makes both rotation columns positive zero, including -0 inputs. */
        if(m[12]==0.f && m[13]==0.f && m[14]==0.f && m[15]==1.f){rows=3;columns[0][3]=columns[1][3]=0.f;}
#endif
        int paired=0;
#if !defined(SOURCE_TRS_PORTABLE) && ((defined(__PSP__) && !defined(JGR_SCALAR_SOURCE_TRANSFORM)) || defined(JGR_TEST_SOURCE_TRANSFORM))
        /* The owner already holds M0..M2. Reuse the verified compensated
           point kernel to evaluate three independent rotation rows at
           once. A rotation has no translation, and only two products.
           Original signed-zero/nonfinite behavior stays on the scalar
           column path; no binary32 approximation enters the hierarchy. */
        if(owned && rows==3) {
            int safe=1;for(int k=0;k<12;k++)safe&=source_point_range(m[k]);
            if(safe) {
                float pair[12]={0};
                for(int r=0;r<3;r++){pair[r*4]=m[r*4+first];pair[r*4+1]=m[r*4+second];}
                for(int j=0;j<2;j++) {
                    float v[3]={vectors[j][first],vectors[j][second],0};
                    source_point_owned(pair,v,columns[j]);
                    for(int r=0;r<3;r++)if(columns[j][r]==0.f)
                        columns[j][r]=source_trs_column(m+r*4,vectors[j],first,second);
                }
                paired=1;
            }
        }
#else
        (void)owned;
#endif
        if(!paired)for(int j=0;j<2;j++)for(int r=0;r<rows;r++) {
            columns[j][r]=source_trs_column(m+r*4,vectors[j],first,second);
        }
        for(int r=0;r<4;r++){m[r*4+first]=columns[0][r];m[r*4+second]=columns[1][r];}
    }
    for(int r=0;r<4;r++)for(int c=0;c<3;c++)m[r*4+c]*=scale[c];
}
static inline void source_trs_apply(const float pos[3],const int32_t angles[3],const float scale[3],float m[16],float (*sin_bam)(int))
{
    float translated[3];source_point(m,pos,translated);
    for(int r=0;r<3;r++)m[r*4+3]=translated[r];
    source_trs_rotate(angles,scale,m,sin_bam,0);
}
/* Caller owns M0..M2 across the complete hierarchy, including rotation. */
static inline unsigned source_trs_apply_owned(const float pos[3],const int32_t angles[3],const float scale[3],float m[16],float (*sin_bam)(int))
{
    float translated[3];unsigned exact=source_point_owned(m,pos,translated);
    for(int r=0;r<3;r++)m[r*4+3]=translated[r];
    source_trs_rotate(angles,scale,m,sin_bam,1);return exact;
}
static inline void source_trs_fsca(const float pos[3],const int32_t angles[3],const float scale[3],float m[16],float (*sin_bam)(int))
{
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};memcpy(m,identity,64);
#ifndef SOURCE_TRS_PORTABLE
    if(isfinite(pos[0]) && isfinite(pos[1]) && isfinite(pos[2])) {
        /* Identity FTRV is just the supplied position (+0 canonicalizes
           negative zero). Avoid the compensated three-row transform. */
        for(int k=0;k<3;k++)m[k*4+3]=pos[k]+0.f;
        source_trs_rotate(angles,scale,m,sin_bam,0);return;
    }
#endif
    source_trs_apply(pos,angles,scale,m,sin_bam);
}
static inline void source_trs(const float pos[3],const int32_t angles[3],const float scale[3],float m[16])
{ source_trs_fsca(pos,angles,scale,m,player_fsca_sin); }
#endif
