#ifndef JGR_ENEMYSHOT_TRANSFORM_H
#define JGR_ENEMYSHOT_TRANSFORM_H
#include "enemyshot.h"
#include "source_trs.h"
/* 0893f0's complete owner transform. The impact tree is authored upright;
   the last quarter turn is separate from the surface-normal orientation. */
static inline void enemyshot_transform(float root[16],const EnemyShot *s,float (*sine)(int))
{
    const float zero[3]={0},one[3]={1,1,1};int32_t a[3]={0,s->angles[1],0};
    source_trs_fsca(s->pos,a,one,root,sine);
    a[1]=0;
    if(!s->angles[2]) {a[0]=16384-s->angles[0];source_trs_apply(zero,a,one,root,sine);}
    else {
        a[0]=-s->angles[0];source_trs_apply(zero,a,one,root,sine);
        a[0]=0;a[2]=-s->angles[2];source_trs_apply(zero,a,one,root,sine);
        a[2]=0;a[0]=16384;source_trs_apply(zero,a,one,root,sine);
    }
}
#endif
