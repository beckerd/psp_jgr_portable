#ifndef JGR_PASSAGE_FIXTURE_H
#define JGR_PASSAGE_FIXTURE_H
#include "passagefixture_data.h"
/* Opt-in passage diagnostic. One initial placement, then stick input only.
   XZ waypoints come from user-installed routes or an explicit local fixture.
   It never supplies a contact, height after spawn, warp, result or save. */
typedef struct {
    int enabled,started,at,warps,shots,done,health;
    unsigned tick;
    float minimum_y;
    unsigned point_count;
    float points[11][3];
} PassageFixture;
static int passage_fixture_start(PassageFixture *f,Player *p)
{
    int result=f->enabled==2 ?
        passage_fixture_local_points(f->points,"data/cache/passage_points.txt"):
        passage_fixture_route_points(f->points,"challenge_rival_s1m07.bin");
    if(result<0){f->enabled=0;return -1;}
    f->point_count=f->enabled==2 ? 5:11;
    const float (*points)[3]=f->points;
    player_reposition(p,points[0]);
    p->yaw=atan2f(points[1][0]-p->x,points[1][2]-p->z);
    player_cam_reset(p);f->started=1;f->at=1;f->health=p->health;f->minimum_y=p->y;
    return 0;
}
static void passage_fixture_update(PassageFixture *f,Player *p,const Collision *col,float yaw,float dt)
{
    const float (*points)[3]=f->points;
    const int count=(int)f->point_count;
    if(f->at<count) {
        float dx=points[f->at][0]-p->x,dz=points[f->at][2]-p->z;
        if(dx*dx+dz*dz<2.25f)f->at++;
    }
    float angle=f->at<count ? atan2f(points[f->at][0]-p->x,points[f->at][2]-p->z)-yaw:0;
    player_update_axes(p,col,f->at<count ? sinf(angle):0,f->at<count ? -cosf(angle):0,0,0,yaw,dt);
    if(p->y<f->minimum_y)f->minimum_y=p->y;
    if(f->tick!=p->logic_tick){f->tick=p->logic_tick;if(p->portal_translation[1]!=0)f->warps++;}
    if(f->at==count)f->done=1;
}
#endif
