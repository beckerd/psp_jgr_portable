#ifndef JGR_TABAN_ATTACK_H
#define JGR_TABAN_ATTACK_H
#include <math.h>
#include <string.h>
/* 0fb21c / task +18c..4c4. The 22-entry velocity history is intentional:
   each of the 19 links reads two following entries. Kept in actor-local
   coordinates; the same chain supplies drawing and the six hit spheres. */
typedef struct {
    float point[20][3],previous[20][3],velocity[22][3],alpha[20];
    int extent,ready;
    float contact_previous[6][3];unsigned contact_valid;
} TabanAttack;
static inline void taban_attack_step(TabanAttack *a,const float anchor[2][3])
{
    float push[3];for(int j=0;j<3;j++)push[j]=(anchor[1][j]-anchor[0][j])*.5f;
    memmove(a->velocity+1,a->velocity,21*sizeof a->velocity[0]);
    memcpy(a->previous[0],a->point[0],12);
    for(int j=0;j<3;j++){a->velocity[0][j]=anchor[0][j]-a->point[0][j];a->point[0][j]=anchor[0][j];}
    if(!a->extent) {
        for(int i=1;i<20;i++){memcpy(a->point[i],a->point[0],12);memcpy(a->previous[i],a->point[0],12);}
        return;
    }
    for(int i=1;i<20;i++) {
        float move[3],next[3],direction[3];memcpy(a->previous[i],a->point[i],12);
        for(int j=0;j<3;j++) {
            move[j]=((a->velocity[i][j]*.7f+a->velocity[i+1][j]*.2f)+a->velocity[i+2][j]*.1f)+push[j];
            next[j]=move[j]+a->point[i][j];
        }
        if(next[1]<0)next[1]=-move[1]*.75f;
        for(int j=0;j<3;j++)direction[j]=next[j]-a->point[i-1][j];
        /* Original 0fb4c6/ca/ce repeats the X check, not a vector test. */
        if(direction[0]!=0) {
            float inverse=1.f/sqrtf((direction[0]*direction[0]+direction[1]*direction[1])+direction[2]*direction[2]);
            for(int j=0;j<3;j++)next[j]=a->point[i-1][j]+(direction[j]*inverse)*3.f;
        }
        float length=0;
        for(int j=0;j<3;j++){a->velocity[i][j]=next[j]-a->point[i][j];a->point[i][j]=next[j];length+=a->velocity[i][j]*a->velocity[i][j];}
        if(i<a->extent)a->alpha[i]=fminf(sqrtf(length)/5.f,1.f);
    }
}
#endif
