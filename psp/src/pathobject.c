#include "pathobject.h"
#include <math.h>
#include <string.h>
/* Shared source FSCA ROM; do not duplicate its 128 KiB in the PSP image. */
extern float player_fsca_sin(int angle);

void pathobject_mode(PathObject *p,int mode)
{
    p->mode=mode;
    if((unsigned)mode<=5)p->state=mode<3 ? 1:0;
}
void pathobject_resume(PathObject *p){if(p->state==0)p->state=1;}
void pathobject_init(PathObject *p,uint32_t frames,uint32_t start,uint32_t speed,int mode)
{
    memset(p,0,sizeof *p);p->frames=frames;
    p->frame=(float)start*((float)frames/10000.f);p->rate=(float)speed/100.f;
    p->radius2=102.f*102.f;p->angle=0x1b05;p->armed=1;
    for(int i=0;i<2;i++)for(int j=0;j<3;j++)p->scale[i][j]=1.f;
    pathobject_mode(p,mode);
}
unsigned pathobject_tick(PathObject *p,const PathObjectInput *in)
{
    unsigned out=0;
    if(p->state==0){memcpy(p->pos,in->position,sizeof p->pos);out|=PATH_INTEGRATE;}
    else if(p->state==1) {
        memcpy(p->pos,in->position,sizeof p->pos);memcpy(p->rotation,in->rotation,sizeof p->rotation);
        if(!p->sound && !in->sound_blocked) {
            float x=in->player[0]-p->pos[0],y=in->player[1]-p->pos[1],z=in->player[2]-p->pos[2];
            float distance=(x*x+y*y)+z*z;
            if(!(p->radius2>distance))p->armed=1;
            else if(p->armed) {
                /* 1033ac/1034bc rotate (0,0,10). BAM angles are masked by
                   Ninja's sin table. The second rotation is relative. */
                int a=(int)((uint32_t)p->rotation[1]+(uint32_t)(p->angle/2))&65535;
                int b=(int)(0u-(unsigned)p->angle)&65535;
                float sa=player_fsca_sin(a),ca=player_fsca_sin(a+16384);
                float sb=player_fsca_sin(b),cb=player_fsca_sin(b+16384);
                float ax=sa*10.f,az=ca*10.f;
                /* FTRV retains product precision before the final rounding;
                   the second call rotates the existing matrix, not identity. */
                float bx=(float)((double)ca*sb+(double)sa*cb)*10.f;
                float bz=(float)((double)-sa*sb+(double)ca*cb)*10.f;
                if(x*bz-z*bx>0.f && z*ax-x*az>0.f) {
                    out|=PATH_SOUND_CREATE;p->sound=in->sound_created;p->armed=0;
                    if(p->special)p->pulse=1;
                }
            }
        }
        p->frame+=p->rate;
        float end=(float)(p->frames-1u);
        if(p->frame>end && (unsigned)p->mode<=5) {
            if(p->mode==0 || p->mode==3)p->frame=0;
            else {p->frame=end;p->state=p->mode==1 || p->mode==4 ? 2:3;}
        }
        out|=PATH_INTEGRATE;
    } else if(p->state==2)out|=PATH_INTEGRATE;
    else if(p->state==3)out|=PATH_DELETE;
    if(p->pulse==1 || p->pulse==2) {
        float step=p->pulse==1 ? .25f:-.25f;
        for(int i=0;i<2;i++)for(int j=0;j<3;j++)p->scale[i][j]+=step;
        if((p->pulse==1 && p->scale[0][0]>3.f) || (p->pulse==2 && p->scale[0][0]<1.f)) {
            float v=p->pulse==1 ? 3.f:1.f;p->pulse=p->pulse==1 ? 2:0;
            for(int i=0;i<2;i++)for(int j=0;j<3;j++)p->scale[i][j]=v;
        }
    }
    if(p->sound) {
        if(in->sound_finished){out|=PATH_SOUND_DELETE;p->sound=0;}
        else out|=PATH_SOUND_POSITION;
    }
    return out;
}
