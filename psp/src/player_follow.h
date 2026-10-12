#ifndef JGR_PLAYER_FOLLOW_H
#define JGR_PLAYER_FOLLOW_H
#include "player.h"
#include "collide.h"
#include "follow_camera.h"
#include "stage_camera.h"
#include "spray_tracks.h"

/* 01dc40 world adapter: consume last tick's queries after the player has
   updated, then register the three 01e29c/01e320 lines for the NEXT tick.
   Each playable/replay actor owns a distinct camera and query history. */
typedef struct {
    Player *player;
    const Collision *collision;
    FollowCamera camera;
    FollowInput query;
    SceneCamera view;
    unsigned ticks;
    const StageCameraRegion *region,*pending_region;
    const SprayTracks *result_tracks;float result_frame;
} PlayerFollow;

static inline float player_follow_tick(void *context)
{
    PlayerFollow *f=context;Player *p=f->player;FollowInput in;
#ifdef PLAYER_FOLLOW_INPUT
    PLAYER_FOLLOW_INPUT(p,&in);
#else
    player_follow_input(p,&in);
#endif
    /* 0198f8 consumes actor +860 into +864, then queues the feet for the
       next 0c6f9c query pass. Do not tie the cut to render frames. */
    f->region=f->pending_region;
    f->pending_region=stage_camera_query(p->world_stage,in.pos);
    if(f->region) {
        stage_camera_step(&f->camera,&in,f->region,&f->view);
        p->cam_ex=f->camera.next_eye[0];p->cam_ez=f->camera.next_eye[2];f->ticks++;
        return (f->camera.yaw+32768)*(6.283185307f/65536.f);
    }
    if(p->state==10 && f->result_tracks && f->result_tracks->data) {
        /* 01f04c mode 1: PLAY +88 table[0], not a follow-camera orbit.
           Ninja uses Rx, Rz, then the motion heading plus half a turn. */
        float matrix[16];int32_t angle[3]={p->visual_angles[0],p->visual_angles[1]+32768,p->visual_angles[2]};
        follow_basis(in.pos,angle,matrix);
        SceneCamera *v=&f->view;int roll,fov;
        if(spray_tracks_sample(f->result_tracks,0,f->result_frame,matrix,v->pos,v->forward,
                              &roll,&fov,&f->result_frame)) {
            v->roll=roll;v->fov=fov;v->active=1;v->near_clip=1;v->far_clip=60000;f->ticks++;
            memcpy(f->camera.eye,v->pos,12);memcpy(f->camera.next_eye,v->pos,12);
            return p->cam_yaw;
        }
    }
    if(in.reset)memset(&f->query,0,sizeof f->query);
    in.ground_hit=f->query.ground_hit;in.body_hit=f->query.body_hit;in.eye_hit=f->query.eye_hit;
    memcpy(in.point,f->query.point,12);memcpy(in.normal,f->query.normal,12);
    follow_camera_step(&f->camera,&in,&f->view);p->camera_reset=0;
    ColHit hit;const Collision *col=f->collision;FollowCamera *c=&f->camera;
    memset(&f->query,0,sizeof f->query);
    if(col){
        f->query.body_hit=col_probe(col,in.body,c->next_eye,3,&hit);
        if(f->query.body_hit){memcpy(f->query.point,hit.p,12);memcpy(f->query.normal,hit.n,12);}
        f->query.eye_hit=col_probe(col,c->eye,c->next_eye,3,&hit);
        float d[3]={in.body[0]-c->next_eye[0],0,in.body[2]-c->next_eye[2]};follow_normalize(d);
        float end[3]={in.pos[0]+d[0]*30,in.pos[1]-5,in.pos[2]+d[2]*30};
        f->query.ground_hit=col_probe(col,in.body,end,3,&hit);
    }
    p->cam_ex=c->next_eye[0];p->cam_ez=c->next_eye[2];f->ticks++;
    return (c->yaw+32768)*(6.283185307f/65536.f);
}
static inline void player_follow_bind(PlayerFollow *f,Player *p,const Collision *col)
{
    memset(f,0,sizeof *f);f->player=p;f->collision=col;f->camera.state=4;
    p->camera_tick=player_follow_tick;p->camera_context=f;
}
static inline void player_follow_spray(PlayerFollow *f)
{
    Player *p=f->player;const SprayCamera *s=&p->spray_camera;
    if(!s->active || f->region)return; /* 01dc40 region takes priority over spray. */
    /* Original 01eb80 preserves the spray camera as the mode-2 departure
       state. The first ordinary update still honours player_cam_reset. */
    memcpy(f->view.pos,s->eye,12);memcpy(f->view.forward,s->forward,12);
    f->view.roll=s->roll;f->view.fov=s->fov;f->view.active=1;
    f->view.near_clip=1;f->view.far_clip=60000;
    float root[16],body[3];player_root_matrix(p,root);
    for(int k=0;k<3;k++)body[k]=root[k*4+1]*10.f+root[k*4+3];
    memcpy(f->camera.eye,s->eye,12);memcpy(f->camera.next_eye,s->eye,12);
    memcpy(f->camera.target,body,12);memcpy(f->camera.next_target,body,12);
}
#endif
