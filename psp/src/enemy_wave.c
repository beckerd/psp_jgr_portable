#include "enemy_wave.h"
#include <math.h>
#include <string.h>
void enemy_wave_reset(EnemyWaves *w)
{memset(w->waves,0,sizeof w->waves);w->created=w->dropped=0;}
void enemy_wave_create(EnemyWaves *w,const float *p,int yaw)
{
    for(int i=0;i<ENEMY_WAVE_CAPACITY;i++)if(!w->waves[i].active) {
        EnemyWave *s=w->waves+i;memset(s,0,sizeof *s);s->active=1;s->yaw=yaw;
        memcpy(s->pos,p,12);w->created++;return;
    }
    w->dropped++;
}
void enemy_wave_tick(EnemyWaves *w)
{
    if(!w->sin_bam || !w->atan_bam)return;
    for(int i=0;i<ENEMY_WAVE_CAPACITY;i++) {
        EnemyWave *s=w->waves+i;if(!s->active)continue;
        if(s->state==0)s->state=1;
        else if(s->state==1) {
            s->frame++;
            if(s->floor) {
                memcpy(s->pos,s->floor_pos,12);
                if(w->route)s->yaw=s->floor_yaw;
                float along=s->normal[0]*w->sin_bam(s->yaw)+s->normal[2]*w->sin_bam(s->yaw+16384);
                s->pitch=w->atan_bam(-along,s->normal[1]);
            }
            if(s->contact) {
                s->state=2;s->frame=0;memcpy(s->hit_pos,s->pos,12);
                if(w->hit_position)w->hit_position(w->context,s->hit_pos);
                if(w->hit)w->hit(w->context,s->pos);
            }
            else if(!s->floor){s->active=0;continue;}
        } else if(++s->frame>=w->hit_frames){s->active=0;continue;}
        s->age++;
        if(s->state==1) {
            float cp=w->sin_bam(s->pitch+16384),dir[3]={w->sin_bam(s->yaw)*cp,w->sin_bam(s->pitch),w->sin_bam(s->yaw+16384)*cp};
            float a[3]={s->pos[0],s->pos[1]+5.f,s->pos[2]};
            float b[3]={s->pos[0]+dir[0]*0x1.638e3ap+2f,s->pos[1]+dir[1]*0x1.638e3ap+1f-5.f,s->pos[2]+dir[2]*0x1.638e3ap+2f};
            if(w->route)s->floor=w->route(w->context,a,b,s->yaw,s->floor_pos,s->normal,&s->floor_yaw);
            else s->floor=w->probe && w->probe(w->context,a,b,4,s->floor_pos,s->normal);
            s->contact=w->contact && w->contact(w->context,s->pos,dir);
        }
    }
}
