#include "enemy_missile.h"
#include "source_fmac.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static int turn(int a,int b,int n)
{int d=(int16_t)(b-a);return d>n?a+n:d < -n?a-n:b;}
static void angles(EnemyMissiles *w,const float *v,int *a)
{
    if(fabsf(v[0])<.1f && fabsf(v[2])<.1f)return;
    int yaw=w->atan_bam(v[0],v[2]);
    float z=source_fmac(v[0],w->sin_bam(yaw),v[2]*w->sin_bam(yaw+16384));
    a[0]=w->atan_bam(v[1],fabsf(z));a[1]=yaw;a[2]=0;
}
static void forward(EnemyMissiles *w,EnemyMissile *s)
{
    float cp=w->sin_bam(s->angles[0]+16384);
    s->direction[0]=w->sin_bam(s->angles[1])*cp;
    s->direction[1]=w->sin_bam(s->angles[0]);
    s->direction[2]=w->sin_bam(s->angles[1]+16384)*cp;
    float n=source_fmac(s->direction[0],s->direction[0],s->direction[1]*s->direction[1]);
    n=source_fmac(s->direction[2],s->direction[2],n);n=1.f/sqrtf(n);
    for(int i=0;i<3;i++)s->direction[i]*=n;
}
void enemy_missile_reset(EnemyMissiles *w)
{memset(w->missiles,0,sizeof w->missiles);w->created=w->dropped=w->hits=w->probes=0;}
void enemy_missile_create(EnemyMissiles *w,const float *from,const float *to,int variant)
{
    if(!w || !w->atan_bam || !w->sin_bam)return;
    for(int i=0;i<ENEMY_MISSILE_CAPACITY;i++)if(!w->missiles[i].active) {
        EnemyMissile *s=w->missiles+i;memset(s,0,sizeof *s);s->active=1;s->variant=!!variant;
        memcpy(s->previous,from,12);memcpy(s->origin,from,12);memcpy(s->pos,from,12);memcpy(s->target,to,12);w->created++;return;
    }
    w->dropped++;
}
/* 0a9a04. Guided center and sinusoidal visual/collision displacement are
   distinct. Original turn ramps cap at 0xaaa and speed at 5.55555582. */
void enemy_missile_move(EnemyMissiles *w,EnemyMissile *s,const float *player)
{
    if(s->speed<0x1.638e3ap+2f){s->speed+=0x1.7b426p-2f;if(s->speed>0x1.638e3ap+2f)s->speed=0x1.638e3ap+2f;}
    float v[3];for(int i=0;i<3;i++)v[i]=player[i]-s->pos[i];
    if(v[0]==0.f && v[1]==0.f && v[2]==0.f){s->state=2;return;}
    int a[3]={s->angles[0],s->angles[1],0};angles(w,v,a);
    for(int i=0;i<2;i++) {
        int d=abs((int16_t)(a[i]-s->angles[i]));
        s->turn[i]=d<s->turn[i]?d:turn(s->turn[i],d,16);
        if(s->turn[i]>0xaaa)s->turn[i]=0xaaa;
        s->angles[i]=turn(s->angles[i],a[i],s->turn[i]);
    }
    forward(w,s);for(int i=0;i<3;i++)s->center[i]+=s->direction[i]*s->speed;
    s->phase+=s->phase_step;
    if(s->phase>=65536) {
        unsigned rnd=w->random15?w->random15(w->context)&32767:0;
        float fraction=(float)rnd/32768.f;
        s->phase=0;s->phase_step=(int)(fraction*256.f+1024.f);
        int roll=(int)(fraction*32768.f);
        s->weave[0]=w->sin_bam(roll+16384);s->weave[1]=w->sin_bam(roll);s->weave[2]=0;
    }
    float magnitude=s->amplitude*w->sin_bam(s->phase);
    for(int i=0;i<3;i++)s->pos[i]=s->weave[i]*magnitude+s->center[i];
    s->amplitude*=.99f;
}
static void trail_store(EnemyMissile *s)
{s->trail_at=(s->trail_at+1)%30;memcpy(s->trail[s->trail_at],s->pos,12);}
void enemy_missile_tick(EnemyMissiles *w,const float *player)
{
    for(int i=0;i<ENEMY_MISSILE_CAPACITY;i++) {
        EnemyMissile *s=w->missiles+i;if(!s->active)continue;
        switch(s->state) {
        case 0: {
            float v[3];for(int k=0;k<3;k++)v[k]=s->target[k]-s->pos[k];
            if(v[0]==0.f && v[1]==0.f && v[2]==0.f){s->active=0;continue;}
            angles(w,v,s->angles);forward(w,s);s->state=1;
            memcpy(s->center,s->pos,12);s->phase=65536;s->amplitude=10.f;
            for(int j=0;j<30;j++)for(int k=0;k<3;k++)s->trail[j][k]=s->pos[k]-29.f*s->direction[k];
            break; }
        case 1:
            s->frame+=1;s->texture_v-=.125f;if(s->texture_v<0)s->texture_v+=1;
            if(s->wall){memcpy(s->pos,s->wall_pos,12);s->state=2;}
            else if(s->contact)s->state=2;
            else {
                float old[3],normal[3];memcpy(old,s->pos,12);enemy_missile_move(w,s,player);
                s->wall=w->probe && w->probe(w->context,old,s->pos,3,s->wall_pos,normal);w->probes++;
                if(s->ticks++>300)s->active=0;
            }break;
        case 2:
            s->state=3;trail_store(s);s->ticks=0;
            if(w->explosion)w->explosion(w->context,s->pos,s->variant+1);
            break;
        case 3:if(++s->ticks>29)s->active=0;trail_store(s);break;
        }
        s->flash_frame++;s->smoke_frame++;if(s->smoke_frame>=17.f)s->alpha-=0x1.3b13b2p-4f;
        if(s->state<3) {
            if(s->trail_toggle) {
                trail_store(s);for(int k=0;k<3;k++)s->trail[s->trail_at][k]-=29.f*s->direction[k];
            }
            s->trail_toggle=!s->trail_toggle;
            s->contact=w->contact?w->contact(w->context,s->pos,s->previous,s->variant):0;
            if(s->contact)w->hits++;
            memcpy(s->previous,s->pos,12);
        }
    }
}
