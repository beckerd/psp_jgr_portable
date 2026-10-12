#include "diagnostics.h"
#include <math.h>
#include <string.h>
#include "charpose.h"
#include "pose_sample.h"
#include "render_math.h"
#include "vfpu_profile.h"
#ifdef __PSP__
#include <pspkernel.h>
#endif

int chars_share_motions(Chars *c,const Chars *owner,int model)
{
    if(!c || !owner || !c->h || !owner->h || c==owner || owner->motion_owner ||
       c->h->n_chars!=1 || c->h->n_motions || (unsigned)model>=owner->h->n_chars)return -1;
    JgcChar *a=c->chars;const JgcChar *b=owner->chars+model;
    if(a->nbones!=b->nbones || !b->nmotions || b->motion0>owner->h->n_motions ||
       b->nmotions>owner->h->n_motions-b->motion0)return -2;
    for(unsigned i=0;i<a->nbones;i++) {
        int pa=c->bones[a->bone0+i].parent,pb=owner->bones[b->bone0+i].parent;
        if(pa>=0)pa-=(int)a->bone0;
        if(pb>=0)pb-=(int)b->bone0;
        if(pa!=pb)return -3;
    }
    a->motion0=b->motion0;a->nmotions=b->nmotions;c->motions=owner->motions;c->motion_owner=owner;return 0;
}

static float (*pose_sine)(int);
void chars_set_pose_sine(float (*sine)(int)){pose_sine=sine;}
void chars_local_matrix(float *m,unsigned ev,const float *pos,const int *ang,const float *scl)
{
    memset(m,0,16*sizeof *m);m[0]=m[5]=m[10]=m[15]=1.f;
    if(!(ev&1)){m[3]=pos[0];m[7]=pos[1];m[11]=pos[2];}
    if(!(ev&2) && (ang[0] || ang[1] || ang[2])) {
        float cx,sx,cy,sy,cz,sz;
        if(pose_sine) {
            unsigned x=(unsigned)ang[0]&65535,y=(unsigned)ang[1]&65535,z=(unsigned)ang[2]&65535;
            sx=pose_sine(x);cx=pose_sine(x+16384);
            sy=pose_sine(y);cy=pose_sine(y+16384);
            sz=pose_sine(z);cz=pose_sine(z+16384);
        } else {
            float ax=ang[0]*(6.283185307f/65536.f),ay=ang[1]*(6.283185307f/65536.f),az=ang[2]*(6.283185307f/65536.f);
            sx=sinf(ax);cx=cosf(ax);sy=sinf(ay);cy=cosf(ay);sz=sinf(az);cz=cosf(az);
        }
        /* Ninja's default Rz*Ry*Rx and alternate Ry*Rx*Rz. Preserve the
           two-product grouping, without constructing or multiplying three
           sparse 4x4 matrices. Translation and scale retain their order. */
        if(ev&0x20) {
            float a=sy*sx,b=cy*sx;
            m[0]=cy*cz+a*sz;m[1]=cy*(-sz)+a*cz;m[2]=sy*cx;
            m[4]=cx*sz;m[5]=cx*cz;m[6]=-sx;
            m[8]=(-sy)*cz+b*sz;m[9]=(-sy)*(-sz)+b*cz;m[10]=cy*cx;
        } else {
            float a=cz*sy,b=sz*sy;
            m[0]=cz*cy;m[1]=(-sz)*cx+a*sx;m[2]=(-sz)*(-sx)+a*cx;
            m[4]=sz*cy;m[5]=cz*cx+b*sx;m[6]=cz*(-sx)+b*cx;
            m[8]=-sy;m[9]=cy*sx;m[10]=cy*cx;
        }
    }
    if(!(ev&4))for(int r=0;r<3;r++)for(int c=0;c<3;c++)m[r*4+c]*=scl[c];
}
void chars_attachment_matrices(const Chars *c,const JgcChar *ch,
                              const CharPose *pose,const float *root,float (*mats)[16])
{
    if(ch->nbones>64)return;
#if defined(__PSP__) && JGR_DIAGNOSTICS
    unsigned started=sceKernelGetSystemTimeLow();
#endif
    RenderMathState math;render_math_begin(&math);
    for(unsigned i=0;i<ch->nbones;i++) {
        const CharPose *p=pose+i;float m[16] __attribute__((aligned(16)));
        /* Attachments evaluate every channel and always use Z-Y-X. */
        chars_local_matrix(m,0,p->pos,p->ang,p->scl);
        int parent=c->bones[ch->bone0+i].parent-(int)ch->bone0;
        render_affine_multiply(mats[i],parent>=0 ? mats[parent]:root,m);
    }
    render_math_end(&math);
#if defined(__PSP__) && JGR_DIAGNOSTICS
    vfpu_profile_attachment(sceKernelGetSystemTimeLow()-started,ch->nbones);
#endif
}

void chars_sample_prepare(const Chars *c,const JgcChar *ch,int motion,float frame,CharSampleCursor *cursor)
{
    cursor->bones=c->bones+ch->bone0;cursor->channels=NULL;cursor->keys=NULL;
    cursor->k=cursor->k1=0;cursor->t=0.f;
    if(motion<0)return;
    const Chars *owner=chars_motion_owner(c);const JgcMotion *mo=c->motions+motion;
    cursor->channels=(const JgcChan *)chars_data(owner,mo->chan0);
    cursor->keys=chars_data(owner,owner->h->off_keys);
    int n=mo->nframes ? (int)mo->nframes:1;
    float f=fmodf(frame,(float)n);if(f<0.f)f+=n;
    cursor->k=(int)f;cursor->k1=cursor->k+1<n ? cursor->k+1:0;cursor->t=f-cursor->k;
}

#include "pose_cache.inc"

void chars_blend_pose(CharPose *a, const CharPose *b, float weight)
{
    if (weight==0.f) return;
    if (weight==1.f) { *a=*b; return; }
    for (int i=0;i<3;i++) {
        a->pos[i]+=(b->pos[i]-a->pos[i])*weight;
        a->scl[i]+=(b->scl[i]-a->scl[i])*weight;
        a->ang[i]+=(int)((short)(b->ang[i]-a->ang[i])*weight);
    }
    a->ev&=b->ev|~7u;
    a->keyed|=b->keyed;
}

void chars_overlay_pose(const Chars *c,const JgcChar *ch,int motion,float frame,
                         unsigned bone,float weight,CharPose *pose)
{
    /* The source copies the primary hierarchy before applying the overlay:
       missing overlay channels retain the primary pose, not the bind pose. */
    CharPose other;chars_sample_pose(c,ch,motion,frame,bone,&other);
    const JgcChan *cn=(const JgcChan *)chars_data(chars_motion_owner(c),c->motions[motion].chan0)+bone;
    if (!cn->npos) { memcpy(other.pos,pose->pos,sizeof other.pos);other.ev=(other.ev&~1u)|(pose->ev&1u); }
    if (!cn->nang) { memcpy(other.ang,pose->ang,sizeof other.ang);other.ev=(other.ev&~2u)|(pose->ev&2u); }
    if (!cn->nscl) { memcpy(other.scl,pose->scl,sizeof other.scl);other.ev=(other.ev&~4u)|(pose->ev&4u); }
    other.keyed|=pose->keyed;
    chars_blend_pose(pose,&other,weight);
}
