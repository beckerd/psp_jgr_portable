#ifndef JGR_FOLLOW_CAMERA_H
#define JGR_FOLLOW_CAMERA_H
#include "scene.h"
#include "source_trs.h"
#include "source_probe.h"
#include <stdlib.h>
extern int player_bam_atan2(float,float);
/* Ordinary +150 camera produced by 01e424. Staff 0c8948 kind 0 copies
   this record, NOT the close-obstruction override at +110. Query results
   are deferred inputs; registration belongs to the world adapter. */
typedef struct {
    float eye[3],next_eye[3],target[3],next_target[3];
    float distance,raise;int state,collision,fov,yaw,roll;
} FollowCamera;
typedef struct FollowInput {
    float pos[3],body[3],ground_y,translation[3],shake;
    int32_t angle[3];int mode,reset,ground_below,yaw_offset,ramp_yaw,wall_side;
    int ground_hit,body_hit,eye_hit;float point[3],normal[3];
} FollowInput;
static inline void follow_normalize(float *v)
{
    float n=probe_fipr(v,v);if(n<=0 || !isfinite(n))return;
    float inverse=1.f/sqrtf(n);for(int k=0;k<3;k++)v[k]*=inverse;
}
static inline float follow_distance(const float *a,const float *b)
{float x=a[0]-b[0],z=a[2]-b[2];return sqrtf(x*x+z*z);} /* 0b26d0 is XZ ONLY */
static inline int follow_angle(int from,int to,int step)
{
    int a=from&65535,b=to&65535,d=b-a;
    if(d>=32768)d-=65536;else if(d<=-32768)d+=65536;
    if(abs(d)<step)return b;
    return a+(d<0 ? -step:step);
}
static inline float follow_approach(float from,float to,float step)
{float d=to-from;if(fabsf(d)<step)return to;return d<0 ? from-step:from+step;}
static inline void follow_basis(const float pos[3],const int32_t a[3],float m[16])
{
    const float zero[3]={0},one[3]={1,1,1};int32_t angle[3]={0};
    source_trs(pos,angle,one,m);
    /* Direct Ninja Rx, Rz, Ry calls, unlike NJS object's Z/Y/X order. */
    angle[0]=a[0];source_trs_apply(zero,angle,one,m,player_fsca_sin);angle[0]=0;
    angle[2]=a[2];source_trs_apply(zero,angle,one,m,player_fsca_sin);angle[2]=0;
    angle[1]=a[1];source_trs_apply(zero,angle,one,m,player_fsca_sin);
}
static inline void follow_point(const float *m,const float *p,float *v)
{
    source_point(m,p,v);
}
static inline void follow_camera_reset(FollowCamera *c,const FollowInput *p)
{
    float m[16];const int32_t yaw[3]={0,p->angle[1],0};
    const float eye[3]={0,18,30},target[3]={0,14,0};
    follow_basis(p->pos,yaw,m);follow_point(m,eye,c->next_eye);follow_point(m,target,c->next_target);
    c->yaw=player_bam_atan2(c->next_eye[0]-c->next_target[0],c->next_eye[2]-c->next_target[2]);
    c->fov=13653;c->roll=follow_angle(c->roll,0,182);c->state=5;
}
static inline void follow_ground(FollowCamera *c,const FollowInput *p)
{
    float distance=follow_distance(c->next_eye,p->pos);
    if(distance>43)distance=43;
    if(distance<30)distance=30;
    float d[3]={c->next_eye[0]-p->pos[0],0,c->next_eye[2]-p->pos[2]};follow_normalize(d);
    c->next_eye[0]=d[0]*distance+p->pos[0];c->next_eye[2]=d[2]*distance+p->pos[2];
    c->next_target[0]=c->next_eye[0]-d[0]*43;c->next_target[2]=c->next_eye[2]-d[2]*43;
    c->yaw=player_bam_atan2(c->next_eye[0]-c->next_target[0],c->next_eye[2]-c->next_target[2]);
    float base[3]={p->pos[0],fminf(p->pos[1],p->ground_y),p->pos[2]},m[16],v[3],point[3]={0,18,0};
    const int32_t tilt[3]={p->angle[0],0,p->angle[2]};follow_basis(base,tilt,m);
    follow_point(m,point,v);c->next_eye[1]=v[1]+c->raise;
    float h=p->pos[1]-(p->ground_y+14);point[1]=h>0 ? h+14:14;
    follow_point(m,point,v);c->next_target[1]=v[1];c->fov=13653;c->roll=follow_angle(c->roll,0,182);
}
static inline void follow_orbit(FollowCamera *c,const FollowInput *p)
{
    float m[16],desired[3],local[3]={0,p->mode==3 ? 20:25,25};
    const float target[3]={0,14,0};int32_t angle[3];memcpy(angle,p->angle,12);
    angle[1]+=p->mode==3 ? 0:p->yaw_offset;
    if(p->mode==5){
        float base[3]={p->pos[0],p->ground_y,p->pos[2]};
        angle[0]=angle[2]=0;angle[1]=p->ramp_yaw;local[1]=10;local[2]=30;
        follow_basis(base,angle,m);memcpy(c->next_target,p->body,12);
    }else{follow_basis(p->pos,angle,m);follow_point(m,target,c->next_target);}
    follow_point(m,local,desired);
    int yaw=follow_angle(player_bam_atan2(c->next_eye[0]-c->next_target[0],c->next_eye[2]-c->next_target[2]),
        player_bam_atan2(desired[0]-c->next_target[0],desired[2]-c->next_target[2]),60);
    float distance=follow_approach(c->distance,follow_distance(desired,c->next_target),0x1.da12f6p-2f);
    c->next_eye[0]=distance*player_fsca_sin(yaw)+c->next_target[0];
    c->next_eye[1]=follow_approach(c->next_eye[1],desired[1],0x1.da12f6p+0f);
    c->next_eye[2]=distance*player_fsca_sin(yaw+16384)+c->next_target[2];
    c->yaw=player_bam_atan2(c->next_eye[0]-c->next_target[0],c->next_eye[2]-c->next_target[2]);
    c->fov=follow_angle(c->fov,13653,546);
    c->roll=follow_angle(c->roll,p->mode==3 ? (p->wall_side ? 16384:-16384):0,182);
}
static inline void follow_camera_step(FollowCamera *c,const FollowInput *p,SceneCamera *out)
{
    if(p->reset || c->state==4 || c->state==7)follow_camera_reset(c,p);
    int smooth=c->state==3;
    if(smooth){
        if(p->pos[1]==p->ground_y)c->raise=!p->ground_hit && p->ground_below ? fminf(c->raise+2,30):fmaxf(c->raise-2,0);
        for(int k=0;k<3;k++){
            c->target[k]=(c->next_target[k]-c->target[k])*.3f+c->target[k];
            c->eye[k]=(c->next_eye[k]-c->eye[k])*.3f+c->eye[k];
        }
        if((c->collision==0 && p->eye_hit && p->body_hit) || (c->collision==1 && p->body_hit)) {
            for(int k=0;k<3;k++){c->eye[k]=p->point[k]+p->normal[k]*4;c->target[k]=p->body[k];}
            c->collision=1;
        }else if(c->collision==1 && !p->body_hit)c->collision=0;
    }else {c->raise=0;c->distance=follow_distance(c->next_eye,c->next_target);}
    for(int k=0;k<3;k++){
        c->eye[k]+=p->translation[k];c->next_eye[k]+=p->translation[k];
        c->target[k]+=p->translation[k];c->next_target[k]+=p->translation[k];
    }
    if(p->mode==0)follow_ground(c,p);
    else if(p->mode>=3 && p->mode<=6)follow_orbit(c,p);
    if(!smooth){
        if(c->state==4 || c->state==5){memcpy(c->eye,c->next_eye,12);memcpy(c->target,c->next_target,12);}
        c->collision=1;
    }
    memset(out,0,sizeof *out);memcpy(out->pos,c->eye,12);
    for(int k=0;k<3;k++)out->forward[k]=c->target[k]-c->eye[k];
    out->forward[1]=(c->target[1]+player_fsca_sin((int)(p->shake*65536/9))*5)-c->eye[1];
    follow_normalize(out->forward);out->fov=c->fov;out->roll=c->roll;
    /* Ordinary source record +170/+174, preserved by every staff callback. */
    out->near_clip=1;out->far_clip=60000;out->active=1;c->state=3;
    c->distance=follow_distance(c->next_eye,c->next_target);
}
#endif
