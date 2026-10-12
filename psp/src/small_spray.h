#ifndef JGR_SMALL_SPRAY_H
#define JGR_SMALL_SPRAY_H
#include <math.h>
#include <string.h>
#include "source_inverse.h"
extern float player_fsca_sin(int);
extern int player_bam_atan2(float,float);
/* 01b5b0's seven original motion references. This is the target in the
   unsmoothed T*Rx*Rz*Ry player frame, NOT the rendered half-turn root. */
static inline int small_spray_ref(const float p[3])
{
    int angle=player_bam_atan2(p[0],-p[2]);
    float distance=sqrtf(p[0]*p[0]+p[2]*p[2]);
    if(p[1]<5.f || p[1]>20.f) {
        if(angle>0x4000 && distance>5.f)return 0xa4;
        if(angle<-0x4000 && distance>5.f)return 0xa3;
        return p[1]<5.f ? 0xa1:0xa2;
    }
    if(angle>0x2aaa)return angle>0x5555 ? 0xa4:0xa6;
    if(angle<-0x2aaa)return angle<-0x5555 ? 0xa3:0xa5;
    return 0xa0;
}
static inline void small_spray_rotate(float m[16],int axis,int angle)
{
    if(!angle)return; /* original helpers skip an exactly zero BAM value */
    float s=player_fsca_sin(angle),c=player_fsca_sin(angle+16384);
    float r[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},out[16];
    int a=(axis+1)%3,b=(axis+2)%3;
    r[a*4+a]=r[b*4+b]=c;r[a*4+b]=-s;r[b*4+a]=s;
    memcpy(out,m,sizeof out);
    /* The source updates only the two rotated columns. FSCA/FTRV, not
       expanded trig identities: each stage rounds before the next one. */
    for(int i=0;i<4;i++)for(int j=0;j<3;j++)if(j!=axis) {
        double d=(double)m[i*4]*r[j];d+=(double)m[i*4+1]*r[4+j];
        d+=(double)m[i*4+2]*r[8+j];d+=(double)m[i*4+3]*r[12+j];out[i*4+j]=(float)d;
    }
    memcpy(m,out,sizeof out);
}
static inline int small_spray_target(const float position[3],const int angles[3],const float target[3],float local[3])
{
    float m[16]={1,0,0,position[0],0,1,0,position[1],0,0,1,position[2],0,0,0,1},inv[16];
    small_spray_rotate(m,0,angles[0]);small_spray_rotate(m,2,angles[2]);small_spray_rotate(m,1,angles[1]);
    if(!source_inverse(m,inv))return -1;
    for(int i=0;i<3;i++) {
        double d=(double)inv[i*4]*target[0];d+=(double)inv[i*4+1]*target[1];
        d+=(double)inv[i*4+2]*target[2];d+=inv[i*4+3];local[i]=(float)d;
    }
    return small_spray_ref(local);
}
#endif
