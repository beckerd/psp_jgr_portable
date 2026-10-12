#ifndef JGR_POLICE_ARROW_H
#define JGR_POLICE_ARROW_H
#include <math.h>
#include <stdint.h>
#include "source_fmac.h"
#include "source_trs.h"
typedef struct {
    float factor,distance,pos[3],root[16];
    int target_pitch,target_yaw,pitch,yaw;
} PoliceArrow;
/* 0c9d08/0c9d50: cone test used by the cordon draw callback. Source
   camera space is +Z forward (the PSP view's Z is negated by the caller).
   This is not a 300-unit sphere or a screen-space rectangle. */
static inline int police_arrow_visible(float x,float y,float z,int fov,float (*sin_bam)(int))
{
    if(z< -10.f || z>300.f)return 0;
    int angle=(int)((fov>>1)*1.2f);
    float cosine=sin_bam(angle+16384),tangent=sin_bam(angle)/cosine;
    return (sqrtf(x*x+y*y)-z*tangent)*(1.f/cosine)<=10.f;
}
/* 049ab8 -> 1043bc/103de4/103df0 and Ninja 278a40. Horizontal
   camera-task direction, not the skater's heading. Preserve the FIPR
   rounding, two reciprocal square roots and the cross-product FMAC. */
static inline int police_arrow_angle(const float marker[3],const float player[3],
                                    const float forward[3],int (*atan_bam)(float,float))
{
    if(!player || !forward)return 0;
    float x=marker[0]-player[0],z=marker[2]-player[2];
    if(x==0 && z==0)return 0;
    float fx=forward[0],fz=forward[2];
    float aa=(float)((double)x*x+(double)z*z),bb=(float)((double)fx*fx+(double)fz*fz);
    if(aa==0 || bb==0)return 0;
    float dot=(float)((double)x*fx+(double)z*fz);
    float cosine=(dot*(1.f/sqrtf(aa)))*(1.f/sqrtf(bb));
    /* Single-precision normalization can round aligned vectors one ULP
       beyond +/-1. Keep acos in its domain: the PSP sqrtf error path can
       kill the game thread when 1-cosine*cosine becomes negative. */
    if(cosine>1.f)cosine=1.f;
    else if(cosine< -1.f)cosine=-1.f;
    int angle=atan_bam(sqrtf(1.f-cosine*cosine),cosine);
    return source_fmac(x,-fz,z*fx)<0 ? 65536-angle:angle;
}
/* 049d9c followed by 049f3c. The source computes position using the OLD
   expansion factor, then changes it by .025. Do not clamp the overshoot
   or fold the following-tick visibility transition into this tick. */
static inline void police_arrow_tick(PoliceArrow *a,int *visibility,
                                    const float marker[3],const float player[3],
                                    const float forward[3],float (*sin_bam)(int),
                                    int (*atan_bam)(float,float))
{
    a->target_yaw=police_arrow_angle(marker,player,forward,atan_bam);
    if(*visibility>=1 && *visibility<=3 && player) {
        float x=marker[0]-player[0],z=marker[2]-player[2];
        a->target_pitch=0;a->distance=sqrtf(x*x+z*z);
        a->pos[0]=((sin_bam(a->target_yaw)*a->distance)*a->factor)*.04f;
        a->pos[1]=0;
        a->pos[2]=((-sin_bam(a->target_yaw+16384)*a->distance)*a->factor)*.04f-2.f;
        if(*visibility==1){a->factor=1;a->target_pitch=16384;}
        else if(*visibility==2){if(a->factor>0)a->factor-=.025f;else *visibility=0;}
        else {if(a->factor<1)a->factor+=.025f;else *visibility=1;}
    } else {
        a->factor=0;a->target_pitch=3640;
        a->pos[0]=a->pos[1]=0;a->pos[2]=-2;
    }
    int delta=(int16_t)(a->target_yaw-a->yaw);
    a->yaw=delta>1024 ? a->yaw+1024:delta< -1024 ? a->yaw-1024:a->target_yaw;
    a->pitch=a->target_pitch;
    const int32_t angles[3]={-a->pitch,-a->yaw,0};const float scale[3]={.03f,.03f,.03f};
    source_trs_fsca(a->pos,angles,scale,a->root,sin_bam);
}
#endif
