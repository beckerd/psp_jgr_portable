#include "finalemitter.h"
#include "player.h"
#include "source_fmac.h"
#include "source_trs.h"
#include <string.h>
#include <math.h>

static unsigned random15(uint32_t *s)
{*s=*s*0x41c64e6du+0x3039u;return (*s>>16)&32767;}
static int32_t approach(int32_t old,int32_t next,int limit)
{
    int delta=(int16_t)(next-old);
    if(delta>limit)return (int32_t)((uint32_t)old+limit);
    if(delta<-limit)return (int32_t)((uint32_t)old-limit);
    return next;
}
static float distance(const float a[3],const float b[3])
{
    /* 2724d4: Y squared first, two FMACs, .025 squared-distance floor. */
    float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];
    float d=source_fmac(x,x,y*y);d=source_fmac(z,z,d);
    return d<.025f ? 0:sqrtf(d);
}
static void steer(FinalEmitter *s,const float a[6])
{
    float dx=a[3]-a[0],dy=a[4]-a[1],dz=a[5]-a[2];
    /* 06da4c does not change any angle for a near-vertical input. */
    if(fabsf(dx)<.1f && fabsf(dz)<.1f)return;
    int yaw=player_bam_atan2(dx,dz);
    float sn=player_fsca_sin(-yaw),cs=player_fsca_sin(-yaw+16384);
    float z=(float)((double)-sn*dx+(double)cs*dz);
    int pitch=player_bam_atan2(dy,fabsf(z));
    s->angle[0]=approach(s->angle[0],pitch,0x2aaa);
    s->angle[1]=approach(s->angle[1],yaw,0x2aaa);s->angle[2]=0;
}
static void evolve(FinalEmitter *s,const float a[6],uint32_t *rng)
{
    /* 0ec0ec decrements the rounded spacing, rather than recomputing it
       for each index. The old direction/size/alpha/scale follow the head. */
    float spacing=(float)s->last*.6f+2.5f;
    for(int j=s->last;j>=1;j--,spacing-=.6f) {
        memcpy(s->direction[j],s->direction[j-1],12);
        s->size[j]=s->size[j-1];s->alpha[j]=s->alpha[j-1];
        s->scale[j]=s->scale[j-1];s->variant[j]=s->variant[j-1];
        float amount=spacing*s->scale[j];
        for(int k=0;k<3;k++)s->position[j][k]=s->position[j-1][k]+s->direction[j][k]*amount;
    }
    if(s->scale[0]>.8f)s->scale[0]-=0x1.7e4b18p-5f;
    steer(s,a);
    /* 104300 rotates (0,0,-1) by Y(yaw-32768), X(pitch), Z(roll).
       Its Z rotation leaves that vector unchanged. */
    float sy=player_fsca_sin(s->angle[1]-32768),cy=player_fsca_sin(s->angle[1]-16384);
    float sx=player_fsca_sin(s->angle[0]),cx=player_fsca_sin(s->angle[0]+16384);
    s->direction[0][0]=-(sy*cx)*3.f;s->direction[0][1]=sx*3.f;s->direction[0][2]=-(cy*cx)*3.f;
    memcpy(s->position[0],a,12);
    s->size[0]=s->alpha[0]=((float)random15(rng)/32768.f)*.5f+.5f;
    s->variant[0]=(float)random15(rng)/32768.f<.5f ? 0:2;
    s->texture+=2;if(s->texture>=8)s->texture=0;
    if(++s->head_texture>=8)s->head_texture=0;
}
static void contacts(FinalEmitter *s,const float a[6],const FinalEmitterContact *c)
{
    s->flags&=~16u;
    for(int j=s->first;j<s->last;j++)if(c && (c->spheres&(1u<<j))){s->last=j;s->flags|=16;break;}
    if(s->cadence<3)return;
    s->cadence=0;
    if(c && c->line) {
        s->flags|=8;float d=distance(a,c->point);
        while(s->last>s->first && distance(a,s->position[s->last])>d)s->last--;
    }else s->flags&=~8u;
}
void finalemitter_init(FinalEmitter *s){memset(s,0,sizeof *s);}
unsigned finalemitter_step(FinalEmitter *s,const float a[6],unsigned duration,
                           const FinalEmitterContact *c,uint32_t *rng)
{
    if(!s || !a || !rng || (s->flags&2) || s->state<0 || s->state>3 ||
       s->first<0 || s->first>30 || s->last<-1 || s->last>=30 ||
       (s->last<0 && s->state<2) || (unsigned)s->texture>=8 || (unsigned)s->head_texture>=8)return 0;
    for(int k=0;k<6;k++)if(!isfinite(a[k]))return 0;
    unsigned request=0;
    switch(s->state) {
    case 0:s->state=1;s->age=s->cadence=s->flags=0;s->first=s->last=0;s->scale[0]=1.5f;
        evolve(s,a,rng);request=FINAL_EMITTER_REGISTER;break;
    case 1:
        if(s->flags&4)s->state=2; /* still run this active callback */
        contacts(s,a,c);
        if(!(s->flags&24) && s->last<29)s->last++;
        evolve(s,a,rng);
        if(s->age++>=duration){s->state=2;s->flags|=4;request|=FINAL_EMITTER_STOP_VOICE;}
        request|=FINAL_EMITTER_REGISTER;break;
    case 2:if(++s->first>=--s->last)s->state=3;evolve(s,a,rng);break;
    case 3:s->flags|=2;return FINAL_EMITTER_DELETE;
    }
    if((request&FINAL_EMITTER_REGISTER) && ++s->cadence>=3)request|=FINAL_EMITTER_LINE;
    return request;
}
float finalemitter_draw_radius(const FinalEmitter *s)
{
    if(!s || s->first<0 || s->first>30 || s->last<-1 || s->last>=30)return 3.f;
    float radius=3.f,spacing=2.5f;
    for(int j=0;j<s->last-1 && j<30;j++,spacing+=.6f)
        if(j>=s->first)radius=(spacing*s->size[j])*3.f;
    return radius;
}
static void direction_angles(const float direction[3],int32_t angles[3])
{
    angles[0]=angles[1]=angles[2]=0;
    if(fabsf(direction[0])<.1f && fabsf(direction[2])<.1f)return;
    int yaw=player_bam_atan2(direction[0],direction[2]);
    float sn=player_fsca_sin(-yaw),cs=player_fsca_sin(-yaw+16384);
    float z=(float)((double)-sn*direction[0]+(double)cs*direction[2]);
    angles[0]=player_bam_atan2(direction[1],fabsf(z));angles[1]=yaw;
}
int finalemitter_render(const FinalEmitter *s,const float forward[3],FinalEmitterDraw draw,void *ctx)
{
    if(!s || !forward || !draw || (s->flags&2) || s->first<0 || s->first>30 ||
       s->last<-1 || s->last>=30 || (unsigned)s->texture>=8 || (unsigned)s->head_texture>=8)return 0;
    for(int k=0;k<3;k++)if(!isfinite(forward[k]))return 0;
#if JGR_PORTABLE_ASSETS
#include "source_data_final_emitter_texmap.inc"
#else
    
#include "generated/source_data/final_emitter_texmap.inc"

#endif
    int32_t camera[3],tail[3];direction_angles(forward,camera);camera[0]=-camera[0];
    /* Signed source indexing reads the end of the position array when
       last is 0/-1; represent that alias explicitly, without C underflow. */
    const float *direction=s->last>0 ? s->direction[s->last-1]:s->position[29+s->last];
    direction_angles(direction,tail);
    int side=(int16_t)(tail[1]-camera[1])<0 ? 0:1;
    int texture=s->texture,j=0,count=0;float spacing=2.5f,alpha=1.f;
    for(;j<s->last-1;j++,spacing+=.6f) {
        if(j>=s->first) {
            if(s->variant[j]!=0 && s->variant[j]!=2)return -1;
            float amount=spacing*s->size[j],scale[3]={amount,amount,amount},root[16];
            source_trs(s->position[j],camera,scale,root);alpha=s->alpha[j];
            draw(ctx,s->variant[j]+side,root,alpha,texmap[texture]);count++;
        }
        if(--texture<0)texture=7;
    }
    float scale[3]={spacing,spacing,spacing},root[16];
    source_trs(s->position[j],camera,scale,root);
    /* The original head does not write a fresh material alpha. */
    draw(ctx,4+side+((s->flags&24) ? 0:2),root,alpha,texmap[s->head_texture]);
    return count+1;
}
