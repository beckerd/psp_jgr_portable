#include "enemyshot.h"
#include "source_fmac.h"
#include <math.h>
#include <string.h>

void enemyshot_reset(EnemyShots *w)
{memset(w->shots,0,sizeof w->shots);w->created=w->dropped=w->hits=w->probes=0;}
unsigned enemyshot_create(EnemyShots *w,const float *from,const float *to,int kind,int mode)
{
    if(!w || !from || !to || (unsigned)mode>2)return 0;
    for(unsigned i=0;i<ENEMY_SHOT_CAPACITY;i++)if(!w->shots[i].active) {
        EnemyShot *s=w->shots+i;memset(s,0,sizeof *s);
        if(!++w->serial)++w->serial;s->token=w->serial;
        s->active=1;s->mode=mode;s->kind=kind;s->feedback=-1;
        memcpy(s->origin,from,12);memcpy(s->pos,from,12);memcpy(s->target,to,12);
        w->created++;return s->token;
    }
    w->dropped++;return 0;
}
int enemyshot_result(const EnemyShots *w,unsigned token)
{
    if(token)for(unsigned i=0;i<ENEMY_SHOT_CAPACITY;i++)
        if(w->shots[i].token==token)return w->shots[i].feedback;
    return -1;
}
static void shot_probe(EnemyShots *w,EnemyShot *s,const float *a,const float *b)
{
    w->probes++;s->wall=w->probe ? w->probe(w->context,a,b,3,s->pos,s->normal):0;
}
static void shot_miss(EnemyShots *w,EnemyShot *s,const float *anchor)
{
    /* 089538: two original 15-bit random draws, radius 5..15, below
       the current +75c anchor. Never turn a deliberate miss into a hit. */
    unsigned r=w->random15 ? w->random15(w->context)&32767:0;
    float radius=((float)r/32768.f)*10.f+5.f;
    int yaw=(w->random15 ? w->random15(w->context)&32767:0)*2;
    float a[3],b[3];memcpy(a,anchor,12);
    a[0]+=(w->sin_bam ? w->sin_bam(yaw):sinf(yaw*(6.28318530718f/65536.f)))*radius;
    a[2]+=(w->sin_bam ? w->sin_bam(yaw+16384):cosf(yaw*(6.28318530718f/65536.f)))*radius;
    a[1]-=5.f;memcpy(b,a,12);b[1]-=10.f;
    shot_probe(w,s,a,b);s->state=4;
}
static void shot_impact(EnemyShots *w,EnemyShot *s,int sound)
{
    s->visible=1;s->state=5;
    s->angles[0]=16384;
    if(w->atan_bam && w->sin_bam && (fabsf(s->normal[0])>=.1f || fabsf(s->normal[2])>=.1f)) {
        int yaw=w->atan_bam(s->normal[0],s->normal[2]);
        float z=source_fmac(s->normal[0],w->sin_bam(yaw),s->normal[2]*w->sin_bam(yaw+16384));
        s->angles[0]=w->atan_bam(s->normal[1],fabsf(z));s->angles[1]=yaw;
    }
    if(sound && w->sound)w->sound(w->context,-0xffef,s->pos);
}
void enemyshot_tick(EnemyShots *w,const float *anchor)
{
    for(unsigned i=0;i<ENEMY_SHOT_CAPACITY;i++) {
        EnemyShot *s=w->shots+i;if(!s->active)continue;
        switch(s->state) {
        case 0:shot_probe(w,s,s->origin,s->target);s->state=1;break;
        case 1:
            if(s->mode==2){s->feedback=!!s->wall;s->state=6;}
            else if(s->wall)shot_impact(w,s,0);
            else if(!s->mode)s->state=3;
            else {memcpy(s->pos,s->target,12);s->state=2;}
            break;
        case 2:
            if(s->contact)s->state=6;else shot_miss(w,s,anchor);
            break;
        case 3:shot_miss(w,s,anchor);break;
        case 4:if(s->wall)shot_impact(w,s,1);else s->state=6;break;
        case 5:
            s->frame+=1.f;
            if(s->frame>=w->impact_frames)s->state=6;
            break;
        case 6:s->active=0;break;
        }
        /* 0893f0 -> 089938 publishes at the CURRENT body anchor after
           state dispatch. The contact result is consumed next update. */
        if(s->state==2) {
            s->contact=w->hit ? w->hit(w->context,s->origin,s->kind):0;
            if(s->contact)w->hits++;
        }
    }
}
