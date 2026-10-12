#ifndef JGR_SOURCE_SHADOW_H
#define JGR_SOURCE_SHADOW_H
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <float.h>
#include "source_fmac.h"
/* Only the sign is consumed by silhouette construction. Use Allegrex's
   native float arithmetic when its error interval excludes zero; preserve
   the original ordered binary64 dot for cancellation/underflow cases. */
static inline int source_shadow_facing(const float normal[3],const float light[3])
{
#ifndef JGR_SHADOW_SIGN_REFERENCE
    float x=normal[1]*light[1],y=normal[2]*light[2],z=normal[0]*light[0];
    float dot=(x+y)+z,error=(fabsf(x)+fabsf(y)+fabsf(z))*(8.f*FLT_EPSILON)+8.f*FLT_MIN;
    if(isfinite(dot) && isfinite(error) && fabsf(dot)>error)return dot<0;
#endif
    return (float)((double)normal[1]*light[1]+(double)normal[2]*light[2]+(double)normal[0]*light[0])<0;
}
/* 104198 then 104120, in Ninja view space (+Z forward). The original
   shortens each extrusion edge to Z=1 and rejects the entire mesh when
   either edge has both endpoints behind that plane. */
static inline int source_shadow_prepare(const float (*v)[3],unsigned n,const float light[3],
                                         float length,float (*volume)[3])
{
    float offset[3];for(int k=0;k<3;k++)offset[k]=light[k]*length;
    for(unsigned j=0;j<n;j++)for(int k=0;k<3;k++) {
        volume[j][k]=v[j][k];volume[j+n][k]=v[j][k]+offset[k];
    }
    for(unsigned j=0;j<n;j++) {
        float *a=volume[j],*b=volume[j+n];
        float fr2=a[2],fr3=b[2];
        if(fr2<1) {
            if(fr3<1)return 0;
            float t=(fr2-1)/(fr2-fr3);fr3=b[0]-a[0];
            a[0]=source_fmac(t,fr3,a[0]);fr2=source_fmac(t,b[1]-a[1],a[1]);a[1]=fr2;a[2]=1;
            /* Preserve 104164's literal register reuse: after clipping A,
               fr2 is its new Y and fr3 is the old X delta, NOT the old Zs.
               Simplifying this into an ordinary symmetric clip disagrees
               with the source's close-camera garage volumes. */
        }
        if(fr3<1 && fr2!=fr3) {
            float t=(fr3-1)/(fr3-fr2);
            b[0]=source_fmac(t,a[0]-b[0],b[0]);b[1]=source_fmac(t,a[1]-b[1],b[1]);b[2]=1;
        }
    }
    return 1;
}
/* 104198 / 103e80 / 104000. Input is the ORIGINAL type-58 signed strip
   stream. Output is the finite closed modifier volume, not a floor decal.
   The source uses a 32-word adjacency bitset. Return index count or -1. */
static inline int source_shadow_volume(const float (*v)[3],unsigned n,
    const int16_t *strips,unsigned words,unsigned nstrips,const float light[3],
    float length,float (*volume)[3],uint16_t *out,unsigned capacity)
{
    if(!v||!strips||!light||!volume||!out||!n||n>32||!nstrips||nstrips>64||!isfinite(length))return -1;
    uint32_t adjacent[32]={0};uint16_t back[192][3];unsigned nb=0,pos=0,used=0;
    for(int k=0;k<3;k++)if(!isfinite(light[k]))return -1;
    for(unsigned j=0;j<n;j++)for(int k=0;k<3;k++)if(!isfinite(v[j][k]))return -1;
    if(!source_shadow_prepare(v,n,light,length,volume))return 0;
    v=(const float (*)[3])volume;
    for(unsigned s=0;s<nstrips;s++) {
        if(pos>=words)return -1;
        int len=strips[pos++],flip=len<0;if(flip)len=-len;
        if(len<3 || (unsigned)len>words-pos)return -1;
        for(int t=0;t<len;t++)if((unsigned)strips[pos+t]>=n)return -1;
        for(int t=0;t<len-2;t++) {
            unsigned a=strips[pos+t],b=strips[pos+t+1],c=strips[pos+t+2];
            if((t&1)^flip){unsigned temp=b;b=c;c=temp;}
            float ab[3],bc[3],normal[3];
            for(int k=0;k<3;k++){ab[k]=v[b][k]-v[a][k];bc[k]=v[c][k]-v[b][k];}
            normal[0]=ab[1]*bc[2]-ab[2]*bc[1];
            normal[1]=ab[2]*bc[0]-ab[0]*bc[2];
            normal[2]=ab[0]*bc[1]-ab[1]*bc[0];
            /* FIPR fv4,fv0 has a single rounding of the dot product. */
            if(source_shadow_facing(normal,light)) {
                if(capacity-used<6)return -1;
                out[used++]=a;out[used++]=b;out[used++]=c;
                out[used++]=c+n;out[used++]=b+n;out[used++]=a+n;
                adjacent[a]|=(1u<<b)|(1u<<c);adjacent[b]|=(1u<<a)|(1u<<c);adjacent[c]|=(1u<<a)|(1u<<b);
            }else {
                if(nb>=192)return -1;
                back[nb][0]=c;back[nb][1]=b;back[nb++][2]=a;
            }
        }
        pos+=len;
    }
    if(pos!=words)return -1;
    for(unsigned j=0;j<nb;j++)for(int k=0;k<3;k++) {
        unsigned a=back[j][k],b=back[j][(k+1)%3];
        if(!(adjacent[a]&(1u<<b)))continue;
        if(capacity-used<6)return -1;
        out[used++]=a;out[used++]=b+n;out[used++]=b;
        out[used++]=a;out[used++]=a+n;out[used++]=b+n;
    }
    return (int)used;
}
#endif
