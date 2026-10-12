/* Synthetic camera/marker vectors reproduce endpoint rounding without assets. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#ifndef __PSP__
#include <fenv.h>
#pragma STDC FENV_ACCESS ON
#endif
#include "../src/police_arrow.h"
static unsigned calls;
static float last_sine,last_cosine;
static int angle(float sine,float cosine)
{
    assert(isfinite(sine) && isfinite(cosine));
    assert(sine>=0 && cosine>=-1 && cosine<=1);
    last_sine=sine;last_cosine=cosine;calls++;
    return (int)lroundf(atan2f(sine,cosine)*(65536.f/(2.f*3.14159265358979323846f)));
}
static float sine(int bam)
{return sinf((bam&65535)*(2.f*3.14159265358979323846f/65536.f));}
static void endpoints(void)
{
    const float origin[3]={0,0,0},marker[3]={1,0,4};
    /* These aligned vectors produce cosine 0x3f800001 before clamping. */
    for(int sign=-1;sign<=1;sign+=2){
        float forward[3]={sign,0,4*sign};
#ifndef __PSP__
        feclearexcept(FE_ALL_EXCEPT);
#endif
        int yaw=police_arrow_angle(marker,origin,forward,angle);
        assert(last_cosine==sign && last_sine==0);
        assert(yaw==(sign>0 ? 0:32768));
#ifndef __PSP__
        assert(!fetestexcept(FE_INVALID | FE_DIVBYZERO));
#endif
    }
    unsigned before=calls;
    assert(!police_arrow_angle(origin,origin,marker,angle));
    assert(!police_arrow_angle(marker,origin,origin,angle));
    assert(calls==before);
}
static void directions(void)
{
    const float origin[3]={0,0,0},forward[3]={0,0,1};
    const float markers[4][3]={{0,0,8},{8,0,0},{0,0,-8},{-8,0,0}};
    const int expected[4]={0,49152,32768,16384};
    for(unsigned i=0;i<4;i++)assert(police_arrow_angle(markers[i],origin,forward,angle)==expected[i]);
}
static void full_ticks(void)
{
    const float origin[3]={0,0,0},scales[]={.03125f,1.f,512.f};
    PoliceArrow arrow={0};int visibility=0;
    for(unsigned scale=0;scale<3;scale++)for(int x=-12;x<=12;x++)for(int z=-12;z<=12;z++){
        if(!x && !z)continue;
        float marker[3]={x*scales[scale],0,z*scales[scale]};
        for(int sign=-1;sign<=1;sign+=2)for(int perturb=-1;perturb<=1;perturb++){
            float forward[3]={sign*x+perturb*0x1p-16f,0,sign*z};
            for(int mode=0;mode<4;mode++){
                visibility=mode;
#ifndef __PSP__
                feclearexcept(FE_ALL_EXCEPT);
#endif
                police_arrow_tick(&arrow,&visibility,marker,origin,forward,sine,angle);
                for(unsigned k=0;k<16;k++)assert(isfinite(arrow.root[k]));
                assert(isfinite(arrow.distance) && isfinite(arrow.factor));
#ifndef __PSP__
                assert(!fetestexcept(FE_INVALID | FE_DIVBYZERO));
#endif
            }
        }
    }
}
int main(void)
{
    endpoints();directions();full_ticks();
    printf("Police arrow: %u finite angle updates, aligned/opposed endpoints, quadrants, visibility modes and matrices passed.\n",calls);
    return 0;
}
