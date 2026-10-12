#ifndef JGR_STAGE_CAMERA_H
#define JGR_STAGE_CAMERA_H
#include "follow_camera.h"

/* Original 0xd8-byte record consumed by 0c6f9c and 01f2a0. */
typedef struct {
    int32_t type;
    float point[6][3],normal[6][3],eye[3],target[3];
    int32_t fov,roll,variable_fov;
    float near_point[3];int32_t near_fov;
    float far_point[3];int32_t far_fov;
} StageCameraRegion;
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_stage_camera.inc"
#else
#include "generated/source_data/stage_camera.inc"
#endif

static inline int stage_camera_contains(const StageCameraRegion *r,const float *pos)
{
    /* 0c7ff4: point FROM plane origin; inclusive, rotated half-spaces.
       probe_fipr retains the SH-4 guard precision at the boundaries. */
    for(int i=0;i<6;i++) {
        float delta[3];for(int k=0;k<3;k++)delta[k]=r->point[i][k]-pos[k];
        if(probe_fipr(delta,r->normal[i])<0.f)return 0;
    }
    return 1;
}
static inline const StageCameraRegion *stage_camera_query(int stage,const float *pos)
{
    /* 02fb6c registers this one region for Shibuya, including S1M07.
       It belongs to the passage, not a mission timer or rival route. */
    if(stage==1 && stage_camera_contains(&shibuya_passage_camera,pos))return &shibuya_passage_camera;
    return NULL;
}
static inline void stage_camera_step(FollowCamera *c,const FollowInput *p,
                                     const StageCameraRegion *r,SceneCamera *out)
{
    /* 01f2a0 cuts immediately. Its state 5 also makes the first ordinary
       update on departure cut back instead of easing from the remote eye. */
    memcpy(c->eye,r->eye,12);memcpy(c->next_eye,r->eye,12);
    if(r->type==1)memcpy(c->next_target,r->target,12);
    else {memcpy(c->next_target,p->body,12);c->next_target[1]=p->ground_y+14.f;}
    memcpy(c->target,c->next_target,12);
    c->fov=r->fov;
    if(r->variable_fov) {
        float d=follow_distance(r->eye,p->pos);
        float near=follow_distance(r->near_point,r->eye),far=follow_distance(r->far_point,r->eye);
        if(d<near)c->fov=r->near_fov;
        else if(d>far)c->fov=r->far_fov;
        else {
            float t=near==far ? 0.f:(d-near)/(far-near);
            c->fov=(int)((float)(r->far_fov-r->near_fov)*t+(float)r->near_fov);
        }
    }
    c->roll=r->roll;
    c->yaw=player_bam_atan2(c->next_eye[0]-c->next_target[0],c->next_eye[2]-c->next_target[2]);
    memset(out,0,sizeof *out);memcpy(out->pos,c->eye,12);
    for(int k=0;k<3;k++)out->forward[k]=c->target[k]-c->eye[k];
    follow_normalize(out->forward);out->fov=c->fov;out->roll=c->roll;
    out->near_clip=1;out->far_clip=60000;out->active=1;c->state=5;
}
#endif
