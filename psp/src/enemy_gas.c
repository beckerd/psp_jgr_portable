#include "enemy_gas.h"
#include <math.h>
#include <string.h>
void enemy_gas_reset(EnemyGases *w)
{memset(w->gas,0,sizeof w->gas);w->created=w->impacts=w->dropped=w->expired=0;}
int enemy_gas_create(EnemyGases *w,const float *p,const float *feet,float speed,int heading,int aim,int skin)
{
    if(!w->sin_bam || !w->atan_bam || !w->random15 || w->frames<=0)return 0;
    for(int i=0;i<ENEMY_GAS_CAPACITY;i++)if(!w->gas[i].active) {
        EnemyGas *s=w->gas+i;memset(s,0,sizeof *s);s->active=1;s->skin=skin!=0;
        s->variant=(w->random15(w->context)*2u)/32768u;s->alpha=1;
        memcpy(s->pos,p,12);
        /* 07a93c/07aa70/07abe8: lead by fifteen ticks; the second
           and third burst rounds offset that target sideways / ahead. */
        int yaw=heading+0x8000;
        if(aim==1)yaw=heading+(w->random15(w->context)>16384?0x6000:0xa000);
        float x=w->sin_bam(yaw),z=w->sin_bam(yaw+16384);
        float dx=feet[0]+x*speed*15.f,dz=feet[2]+z*speed*15.f;
        if(aim){float offset=aim==1?10.f:30.f;dx+=x*offset;dz+=z*offset;}
        dx-=p[0];dz-=p[2];
        int angle=w->atan_bam(dx,dz);float velocity=sqrtf(dx*dx+dz*dz)/15.f;
        s->step[0]=w->sin_bam(angle)*velocity;s->step[2]=w->sin_bam(angle+16384)*velocity;
        s->step[1]=(feet[1]-p[1])/15.f+7.5f;w->created++;return 1;
    }
    w->dropped++;return 0;
}
void enemy_gas_tick(EnemyGases *w)
{
    for(int i=0;i<ENEMY_GAS_CAPACITY;i++) {
        EnemyGas *s=w->gas+i;if(!s->active)continue;
        if(s->state==0){s->state=1;continue;}
        if(s->state==1) {
            if(s->wall){memcpy(s->pos,s->wall_pos,12);s->state=2;continue;}
            float before[3],normal[3];memcpy(before,s->pos,12);
            s->pos[1]+=s->step[1];s->step[1]-=1.f;
            s->pos[0]+=s->step[0];s->pos[2]+=s->step[2];
            s->wall=w->probe && w->probe(w->context,before,s->pos,3,s->wall_pos,normal);
            if(s->age++>60){s->active=0;w->expired++;}
        } else if(s->state==2) {
            s->state=3;s->frame=0;s->alpha=1;w->impacts++;
            if(w->sound)w->sound(w->context,0x1000d,s->pos);
        } else {
            s->frame++;
            if(s->frame>60)s->alpha-=0x1.111112p-6f;
            if(s->frame>=w->frames){s->active=0;continue;}
        }
    }
}
