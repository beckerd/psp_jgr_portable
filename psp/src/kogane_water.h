#ifndef KOGANE_WATER_H
#define KOGANE_WATER_H
#include <stdint.h>
typedef struct {float delta[3],position[3];int32_t angles[3];} WaterReturn;
typedef struct {float point[6][3],normal[6][3];} WaterVolume;
typedef struct {
    int pending,phase,countdown,region;
    float alpha,fade_step;
    int splash,splash_started,recover_started;float splash_frame,splash_position[3];
} WaterRecovery;
#if JGR_PORTABLE_ASSETS
#include "source_data_kogane_water_data.inc"
#else
#include "generated/source_data/kogane_water_data.inc"
#endif
/* 03a1cc selects by area, with sectors 86/87 using the waterfront list. */
static inline int kogane_water_region(int area,int sector)
{
    if(area==0 || (area==3 && (sector==86 || sector==87)))return 0;
    return area==1 ? 1:area==3 ? 2:-1;
}
static inline int kogane_water_contains(const WaterVolume *v,const float p[3])
{
    /* 0c7ff4 -> 103d9c: inclusive half-spaces, including rotated edges. */
    for(int i=0;i<6;i++) {
        float x=v->point[i][0]-p[0],y=v->point[i][1]-p[1],z=v->point[i][2]-p[2];
        float dot=(float)((double)x*v->normal[i][0]+(double)y*v->normal[i][1]+(double)z*v->normal[i][2]);
        if(dot<0.f)return 0;
    }
    return 1;
}
static inline int kogane_water_query(int area,int sector,const float position[3])
{
    int region=kogane_water_region(area,sector);
    int first=region==0 ? 0:region==1 ? 8:9,count=region==0 ? 8:1;
    if(region<0)return 0;
    for(int i=first;i<first+count;i++)if(kogane_water_contains(KOGANE_WATER_VOLUME+i,position))return region+1;
    return 0;
}
static inline void kogane_water_effects_tick(WaterRecovery *w)
{
    if(w->splash && (w->splash_frame+=0x1.333334p+0f)>40.f)w->splash=0;
    if(w->fade_step) {
        w->alpha+=w->fade_step;
        if(w->alpha>1.f){w->alpha=1.f;w->fade_step=0;}
        else if(w->alpha<0.f){w->alpha=0.f;w->fade_step=0;}
    }
}
#endif
