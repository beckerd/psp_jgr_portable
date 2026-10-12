#ifndef JGR_POSE_SAMPLE_H
#define JGR_POSE_SAMPLE_H
#include <string.h>
#include "charpose.h"
/* Pure key sampling shared by CPU and ME; no globals, allocation, VFPU,
   system calls, frame quantization or one-frame latency. */
static inline void chars_sample_prepared(const CharSampleCursor *cursor,unsigned bone,CharPose *pose)
{
    const JgcBone *b=cursor->bones+bone;
    memcpy(pose->pos,b->pos,sizeof pose->pos);
    memcpy(pose->ang,b->ang,sizeof pose->ang);
    memcpy(pose->scl,b->scl,sizeof pose->scl); pose->ev=b->ev;pose->keyed=0;
    if (!cursor->channels) return;
    const JgcChan *cn=cursor->channels+bone;
    const unsigned char *keys=cursor->keys;
    int k=cursor->k,k1=cursor->k1;float t=cursor->t;
    if (cn->npos) {
        pose->keyed|=1;
        const float *a=(const float *)(keys+cn->off_pos)+(k<(int)cn->npos ? k : cn->npos-1)*3;
        const float *d=(const float *)(keys+cn->off_pos)+(k1<(int)cn->npos ? k1 : cn->npos-1)*3;
        for (int i=0;i<3;i++) pose->pos[i]=a[i]+(d[i]-a[i])*t;
        pose->ev&=~1u;
    }
    if (cn->nang) {
        pose->keyed|=2;
        const short *a=(const short *)(keys+cn->off_ang)+(k<(int)cn->nang ? k : cn->nang-1)*3;
        const short *d=(const short *)(keys+cn->off_ang)+(k1<(int)cn->nang ? k1 : cn->nang-1)*3;
        for (int i=0;i<3;i++) pose->ang[i]=a[i]+(int)((short)(d[i]-a[i])*t);
        pose->ev&=~2u;
    }
    if (cn->nscl) {
        pose->keyed|=4;
        const float *a=(const float *)(keys+cn->off_scl)+(k<(int)cn->nscl ? k : cn->nscl-1)*3;
        const float *d=(const float *)(keys+cn->off_scl)+(k1<(int)cn->nscl ? k1 : cn->nscl-1)*3;
        for (int i=0;i<3;i++) pose->scl[i]=a[i]+(d[i]-a[i])*t;
        pose->ev&=~4u;
    }
}

#endif
