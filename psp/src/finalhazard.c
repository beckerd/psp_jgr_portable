#include "finalhazard.h"
#include "charpose.h"
#include "player.h"
#include <string.h>
#include <math.h>
/* Executable 1dc118/130/160/168. These are task root transforms, not
   offsets chosen to make an unrelated model fit the final arena. */

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_finalhazard_c__positions_0.inc"
#else

#include "generated/source_data/finalhazard_c__positions_0.inc"

#endif


#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_finalhazard_c__angles_0.inc"
#else

#include "generated/source_data/finalhazard_c__angles_0.inc"

#endif

void finalhazard_init(FinalHazard *s)
{memset(s,0,sizeof *s);s->request=4;}
int finalhazard_yaw(const FinalHazard *s)
{return (unsigned)s->position<6 ? angles[s->position]:0;}
int finalhazard_select(float x,float z)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_finalhazard_c__targets_0.inc"
#else

#include "generated/source_data/finalhazard_c__targets_0.inc"

#endif

    for(int i=0;i<4;i++) {
        float dx=x-targets[i][0],dz=z-targets[i][1];
        if(sqrtf((float)((double)dx*dx+(double)dz*dz))<350.f)return i;
    }
    return 4;
}
static void idle(FinalHazard *s)
{s->position=s->idle;s->frame=s->motion=0;s->state=1;}
static void attack(FinalHazard *s)
{s->position=s->direction+2;s->frame=0;s->motion=1;s->state=4;}
static void events(FinalHazard *s)
{
    if(s->frame==297)s->projectile_serial++;
    else if(s->frame==170)s->voice_serial++;
}
void finalhazard_tick(FinalHazard *s)
{
    if(s->deleted || (unsigned)s->request>4 || (unsigned)s->direction>3 || (unsigned)s->idle>1)return;
    const int frames=s->motion ? 600:160; /* original CNK ushort +2 */
    switch(s->state) {
    case 0:idle(s);break;
    case 1:
        if(s->request!=4){s->state=2;s->direction=s->request;}
        if(++s->frame>=frames){s->frame=0;s->idle=1-s->idle;s->position=s->idle;}
        break;
    case 2:if(++s->frame>=frames)attack(s);break;
    case 3:attack(s);break;
    case 4:
        if(s->request==4)s->state=5;
        if(++s->frame>=frames)s->frame=0;
        events(s);break;
    case 5:
        if(++s->frame>=frames)idle(s);
        else events(s);
        break;
    }
}
int finalhazard_pose(FinalHazard *s,const Chars *bank)
{
    if(!bank || !bank->h || bank->h->n_chars!=2 || (unsigned)s->position>=6 || (unsigned)s->motion>1)return -1;
    int32_t rotation[3]={0,angles[s->position],0};const float scale[3]={1,1,1};
    player_scene_matrix(positions[s->position],rotation,scale,s->world);
    if(s->state==4 || s->state==5) {
        /* 1dc190 = bone 3, two POINTS, both w=1. The second is not a
           direction vector. 0b8f5c honors node eval flags like rendering. */
        const JgcChar *ch=bank->chars;float mats[64][16];
        if(ch->nbones!=31 || ch->nmotions!=2)return -1;
        chars_mesh_matrices(bank,ch,ch->motion0+s->motion,s->frame,s->world,mats);
        
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_finalhazard_c__points_0.inc"
#else

#include "generated/source_data/finalhazard_c__points_0.inc"

#endif

        for(int j=0;j<2;j++)for(int r=0;r<3;r++) {
            const float *m=mats[3]+r*4,*p=points[j];
            s->attachment[j*3+r]=(float)((double)m[0]*p[0]+(double)m[1]*p[1]+(double)m[2]*p[2]+m[3]);
        }
    }
    return 0;
}
void finalhazard_draw(const FinalHazard *s,Chars *bank)
{
    if(s->deleted)return;
    chars_draw_one_xf(bank,0,s->world,s->motion,s->frame);
    chars_draw_material(bank,1,s->world,NULL,s->motion,s->frame,1.f,NULL);
}
