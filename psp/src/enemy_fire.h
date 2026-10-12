#ifndef JGR_ENEMY_FIRE_H
#define JGR_ENEMY_FIRE_H
#include <math.h>
#include <string.h>
/* 07eb4c..07f160. Twenty delayed nozzle samples, not a hitscan cone.
   Clipped stream extent is consumed on the next simulation tick. */
typedef struct {
    unsigned token,seen;int state,age,first,last,flags,image,tip_image,variant,probe_clock;
    int angles[2],pending_wall,pending_body,impact_clock;float wall_point[3];
    float flow[20],point[20][3],direction[20][3],size[20],alpha[20];
    unsigned char mesh[20];
} EnemyFire;
/* 0eacf0 / 0eb124 / 0eb15c / 0eb178: fire left at stream impacts.
   Five growth ticks, 300 active ticks, then ten shrink ticks. */
typedef struct {float pos[3],scale;int state,life,image,mesh;} EnemyBurn;
/* 043c6c / 043cf8: player attachment, separate from scenery fires.
   Sixteen frames, three seconds, shrinking over the final eighteen ticks. */
typedef struct {
    float remaining,scale,pos[3],previous[3];unsigned frame,image;
} PlayerBurn;
static inline void enemy_burn_step(EnemyBurn *b,unsigned random15)
{
    if(b->state==0){b->scale+=.2f;if(b->scale>=1.f){b->scale=1.f;b->state=1;}}
    else if(b->state==1){if(--b->life==0)b->state=2;}
    else if(b->state==2){b->scale-=.1f;if(b->scale<=0){b->scale=0;b->state=3;}}
    if(++b->image>7){b->image=0;b->mesh=random15>=16384;}
}
static inline void enemy_fire_step(EnemyFire *s,const float origin[3],const float direction[3],int duration,int stop,unsigned random_size,unsigned random_mesh)
{
    const float scale=s->variant?.075000003f:.050000001f;
    if(s->state==0){s->state=1;s->flow[0]=1.5f;}
    else if(s->state==1) {
        if(stop)s->state=2;
        if(!(s->flags&0x18) && s->last<19)s->last++;
        if(s->age++>=duration){s->state=2;s->flags|=4;}
    } else if(s->state==2) {
        s->first++;s->last--;if(s->last<=s->first)s->state=3;
    } else {s->flags|=2;return;}
    float step=scale*s->last+scale;
    for(int i=s->last;i>0;i--,step-=scale) {
        memcpy(s->direction[i],s->direction[i-1],12);
        s->size[i]=s->size[i-1];s->alpha[i]=s->alpha[i-1];s->mesh[i]=s->mesh[i-1];s->flow[i]=s->flow[i-1];
        for(int k=0;k<3;k++)s->point[i][k]=s->point[i-1][k]+s->direction[i][k]*(s->flow[i]*step);
    }
    if(s->flow[0]>.8f)s->flow[0]-=.046666668f;
    memcpy(s->point[0],origin,12);
    for(int k=0;k<3;k++)s->direction[0][k]=direction[k]*3.f;
    s->size[0]=s->alpha[0]=random_size*(.5f/32768.f)+.5f;
    s->mesh[0]=random_mesh<16384?0:2;
    s->image=(s->image+2)&7;s->tip_image=(s->tip_image+1)&7;
}
#endif
