#include "enemy_shell.h"
#include "source_fmac.h"
#include <math.h>
#include <string.h>
void enemy_shell_reset(EnemyShells *w)
{memset(w->shells,0,sizeof w->shells);w->created=w->dropped=0;}
void enemy_shell_create(EnemyShells *w,const float *a,const float *b)
{
    for(int i=0;i<ENEMY_SHELL_CAPACITY;i++)if(!w->shells[i].active) {
        EnemyShell *s=w->shells+i;memset(s,0,sizeof *s);s->active=1;
        memcpy(s->pos,a,12);memcpy(s->target,b,12);w->created++;return;
    }
    w->dropped++;
}
void enemy_shell_tick(EnemyShells *w)
{
    for(int i=0;i<ENEMY_SHELL_CAPACITY;i++) {
        EnemyShell *s=w->shells+i;if(!s->active)continue;
        switch(s->state) {
        case 0: {
            for(int j=0;j<3;j++)s->direction[j]=s->target[j]-s->pos[j];
            float *d=s->direction;
            float length=sqrtf(source_fmac(d[2],d[2],source_fmac(d[1],d[1],d[0]*d[0])));
            if(!(length>0) || !isfinite(length)){s->active=0;continue;}
            float reciprocal=1.f/length;for(int j=0;j<3;j++)d[j]*=reciprocal;
            s->speed=length/14.f;s->state=1;break;
        }
        case 1:
            if(s->wall){memcpy(s->pos,s->wall_pos,12);s->state=2;}
            else if(s->contact)s->state=2;
            else {
                float old[3],normal[3];memcpy(old,s->pos,12);
                for(int j=0;j<3;j++)s->pos[j]+=s->direction[j]*s->speed;
                s->wall=w->probe && w->probe(w->context,old,s->pos,3,s->wall_pos,normal);
                if(s->ticks++>60)s->active=0;
            }break;
        case 2:if(w->explosion)w->explosion(w->context,s->pos,3);s->active=0;break;
        }
        /* Attribute 25 is a projectile contact, not a direct player hit.
           Its subsequent attribute-28 explosion owns the damage. */
        if(s->active && s->state<2)s->contact=w->contact && w->contact(w->context,s->pos,3.f);
    }
}
