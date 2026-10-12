#ifndef JGR_RIVAL_SPRAY_H
#define JGR_RIVAL_SPRAY_H
#include "gang.h"
#include "charanim.h"
#include "player.h"
#include <math.h>

/* 0df5ac selects two points on node 15 via 1cf738's 0002000f
   callback. 1cf6e4/1cf6fc/1cf714 are Rival1/2/3's original points.
   0e0648 uses the second point as the nozzle and second-first as aim. */

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_rival_spray_h__rival_can_points_0.inc"
#else

#include "generated/source_data/rival_spray_h__RIVAL_CAN_POINTS_0.inc"

#endif

static int rival_spray_matrix(const Gang *g,const Chars *bank,int model,float out[16])
{
    if(!g->active || g->mode!=1 || g->state!=2 || g->kind<18 || g->kind>20 ||
       !bank || !bank->h || (unsigned)model>=bank->h->n_chars)return 0;
    const JgcChar *ch=bank->chars+model;if(ch->nbones<=15 || ch->nbones>64)return 0;
    float root[16],mats[64][16],point[2][3];
    gang_member_matrix(g,g->m,root);
    chars_mesh_matrices(bank,ch,ch->motion0,g->m[0].frame,root,mats);
    const float *m=mats[15];
    for(int j=0;j<2;j++)for(int k=0;k<3;k++) {
        const float *p=RIVAL_CAN_POINTS[g->kind-18][j];
        point[j][k]=(float)((double)m[k*4]*p[0]+(double)m[k*4+1]*p[1]+
                           (double)m[k*4+2]*p[2]+m[k*4+3]);
    }
    float dx=point[1][0]-point[0][0],dy=point[1][1]-point[0][1],dz=point[1][2]-point[0][2];
    /* 0e0648's unbound-surface branch: spray only while the can points
       forwards (dot > .4), not throughout the hand's recovery swing. */
    if(dx*g->dir[0]+dy*g->dir[1]+dz*g->dir[2]<=.4f)return 0;
    int32_t angles[3]={-player_bam_atan2(dy,sqrtf(dx*dx+dz*dz)),player_bam_atan2(dx,dz),0};
    const float scale[3]={1,1,1};player_scene_matrix(point[1],angles,scale,out);
    return 1;
}
#endif
