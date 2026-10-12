#ifndef JGR_SPRAY_CAMERA_H
#define JGR_SPRAY_CAMERA_H
#include <math.h>
#include <stdint.h>
#include <string.h>
/* 01eb80 authored subvolume: +90 eye, +9c initial look-at POINT,
   +a8 destination BAM FOV, +ac transition seconds. Evaluate then advance. */
typedef struct {
    int active,style,initial_fov,final_fov,fov,roll,authored;
    float eye[3],target[3],forward[3],blend,step,frame;
} SprayCamera;
/* 0c4e48 queues the skater's query; 0c48dc tests registered OTHER actor
   points (0c4dcc), not the skater, against each camera volume. Select the
   nearest qualifying subject, preserving first-on-equal-distance order.
   +9c is this dynamic world-space subject, not template data. */
static inline int spray_camera_subject(const float position[3],const void *volumes,unsigned nv,
                                      const float *points,unsigned np,float target[3])
{
    if(!volumes || !points || !nv || np>50)return -1;
    int selected=-1;float nearest=0;
    for(unsigned v=0;v<nv;v++)for(unsigned p=0;p<np;p++) {
        const unsigned char *volume=(const unsigned char *)volumes+v*176;
        const float *point=points+p*3;int inside=1;
        for(int j=0;j<6;j++) {
            float plane[3],normal[3],d[3];memcpy(plane,volume+j*12,12);memcpy(normal,volume+72+j*12,12);
            for(int k=0;k<3;k++)d[k]=plane[k]-point[k];
            double dot=(double)d[0]*normal[0];dot+=(double)d[1]*normal[1];dot+=(double)d[2]*normal[2];
            if((float)dot<0){inside=0;break;}
        }
        if(!inside)continue;
        float d[3];for(int j=0;j<3;j++)d[j]=position[j]-point[j];
        double sum=(double)d[0]*d[0];sum+=(double)d[1]*d[1];sum+=(double)d[2]*d[2];
        float distance=sqrtf((float)sum);
        if(selected<0 || distance<nearest){selected=(int)v;nearest=distance;memcpy(target,point,12);}
    }
    return selected;
}
static inline int spray_camera_begin(SprayCamera *c,int style,const void *volume)
{
    const unsigned char *v=volume;
    float eye[3],target[3],seconds;int32_t fov;
    memcpy(eye,v+144,12);memcpy(target,v+156,12);
    memcpy(&fov,v+168,4);memcpy(&seconds,v+172,4);
    if(!isfinite(seconds) || seconds<=0 || fov<=0 || fov>=32768)return 0;
    for(int j=0;j<3;j++)if(!isfinite(eye[j]) || !isfinite(target[j]))return 0;
    /* 01ebec calls 0b26d0: XZ distance, two FMULs then FADD.
       Height does not narrow the initial field of view. */
    float x=target[0]-eye[0],z=target[2]-eye[2];
    float distance=sqrtf(x*x+z*z);
    c->active=1;c->authored=1;c->roll=0;c->frame=0;c->style=style;c->blend=0;c->step=1.f/(seconds*30.f);
    c->initial_fov=distance>200.f ? 0x71c:distance<20.f ? 0x4000:
        (int)((1.f-(distance-20.f)/180.f)*14564.f)+0x71c;
    c->final_fov=fov;memcpy(c->eye,eye,12);memcpy(c->target,target,12);return 1;
}
static inline void spray_camera_tick(SprayCamera *c,const float focus[3])
{
    if(!c->active)return;
    float d[3];
    for(int j=0;j<3;j++)d[j]=((focus[j]-c->target[j])*c->blend+c->target[j])-c->eye[j];
    double sum=(double)d[0]*d[0];sum+=(double)d[1]*d[1];sum+=(double)d[2]*d[2];
    float length=sqrtf((float)sum);
    if(length>0){float inverse=1.f/length;for(int j=0;j<3;j++)c->forward[j]=d[j]*inverse;}
    else {c->forward[0]=c->forward[1]=0;c->forward[2]=1;}
    c->fov=c->initial_fov+(int)((float)(c->final_fov-c->initial_fov)*c->blend);
    c->blend+=c->step;if(!(1.f>c->blend))c->blend=1.f;
}
#endif
