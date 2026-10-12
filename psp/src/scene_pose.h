#ifndef JGR_SCENE_POSE_H
#define JGR_SCENE_POSE_H
#include "scene.h"
#include "charpose.h"
#include "source_trs.h"
/* 0cf2e8 frozen/blended CNK pose followed by 0b9e30 bone capture.
   Mouth binding uses bone 11 of the same pose as the visible body. */
static inline int scene_skater_pose(const SceneActor *a,const Chars *c,int model,CharPose poses[64])
{
    if(!c || !c->h || (unsigned)model>=c->h->n_chars)return 0;
    const JgcChar *ch=c->chars+model;if(ch->nbones>64)return 0;
    if((unsigned)a->motion>=ch->nmotions)return 0;
    int blend=a->mode==3 && a->blend>0;
    if(blend && (unsigned)a->old_motion>=ch->nmotions)return 0;
    for(unsigned bone=0;bone<ch->nbones;bone++) {
        chars_sample_pose(c,ch,ch->motion0+(blend ? a->old_motion:a->motion),blend ? a->old_frame:a->frame,bone,poses+bone);
        if(blend){CharPose next;chars_sample_pose(c,ch,ch->motion0+a->motion,0,bone,&next);chars_blend_pose(poses+bone,&next,a->frame/a->blend);}
    }
    return 1;
}
/* Body keys in bespoke EVENTs share the PLAY skeleton but not its motion
   table. Keep both raw and shared previous clips for authored transitions. */
static inline int scene_event_skater_pose(const Scene *s,const SceneActor *a,const Chars *c,int model,CharPose poses[64])
{
    if(!a->raw_motion && !a->old_raw_motion)return scene_skater_pose(a,c,model,poses);
    if(!c || !c->h || (unsigned)model>=c->h->n_chars)return 0;
    const JgcChar *ch=c->chars+model;if(ch->nbones>64)return 0;
    int blend=a->mode==3 && a->blend>0;
    for(unsigned bone=0;bone<ch->nbones;bone++) {
        uint32_t raw=blend?a->old_raw_motion:a->raw_motion;
        int motion=blend?a->old_motion:a->motion;float frame=blend?a->old_frame:a->frame;
        if(!raw && (unsigned)motion>=ch->nmotions)return 0;
        chars_sample_pose(c,ch,raw?-1:ch->motion0+motion,frame,bone,poses+bone);
        if(raw && !scene_sample_cnk(s,raw,frame,bone,poses+bone))return 0;
        if(blend) {
            CharPose next;
            if(!a->raw_motion && (unsigned)a->motion>=ch->nmotions)return 0;
            chars_sample_pose(c,ch,a->raw_motion?-1:ch->motion0+a->motion,0,bone,&next);
            if(a->raw_motion && !scene_sample_cnk(s,a->raw_motion,0,bone,&next))return 0;
            chars_blend_pose(poses+bone,&next,a->frame/a->blend);
        }
    }
    return 1;
}
static inline int scene_mouth_matrix(const Chars *c,int model,const CharPose *poses,
                                    const float root[16],float result[16],float (*sin_bam)(int))
{
    const JgcChar *ch=c->chars+model;if(ch->nbones<=11 || ch->nbones>64 || !sin_bam)return 0;
    float mats[12][16];
    for(unsigned bone=0;bone<=11;bone++) {
        int parent=c->bones[ch->bone0+bone].parent-(int)ch->bone0;
        if(parent>=(int)bone)return 0;
        memcpy(mats[bone],parent>=0 ? mats[parent]:root,64);
        const CharPose *p=poses+bone;float pos[3],scale[3];int32_t angle[3];
        for(int j=0;j<3;j++){pos[j]=(p->ev&1) ? 0:p->pos[j];angle[j]=(p->ev&2) ? 0:p->ang[j];scale[j]=(p->ev&4) ? 1:p->scl[j];}
        source_trs_apply(pos,angle,scale,mats[bone],sin_bam);
    }
    memcpy(result,mats[11],64);return 1;
}
#endif
