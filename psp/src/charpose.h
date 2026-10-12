#ifndef CHARPOSE_H
#define CHARPOSE_H
#include "charanim.h"
/* DEMO movies include whole authored set hierarchies (up to 381 nodes),
   not just the 20-node PLAY skater. Keep a bounded render scratch pool. */
#define CHAR_RENDER_MAX_BONES 512
/* Local TRS, before hierarchy evaluation. Blending matrices would shear
   limbs and change their lengths. Angles are integer binary turns. CNK
   streams use s16 keys; sparse NJS scenes preserve their full s32 keys. */
typedef struct CharPose { float pos[3], scl[3]; int ang[3]; unsigned ev, keyed; } CharPose;
typedef struct {
    const JgcBone *bones;const JgcChan *channels;const unsigned char *keys;
    int k,k1;float t;
} CharSampleCursor;
void chars_sample_prepare(const Chars *,const JgcChar *,int,float,CharSampleCursor *);
/* Render-frame cache and same-frame asynchronous NPC preparation. */
void chars_pose_frame_begin(void);
void chars_pose_forget(void);
void chars_pose_prefetch(const Chars *,int,int,float);
void chars_pose_prefetch_channels(const Chars *,int,int,float);
void chars_pose_submit(void);
void chars_pose_join(void);
const float *chars_pose_local(const Chars *,const JgcChar *,int,float,unsigned);
/* Consume a complete prepared local hierarchy with one lookup/join. The
   contiguous 16-float matrices remain valid until chars_pose_forget().
   A hit accounts for ch->nbones reused poses; a miss changes no counters. */
const float *chars_pose_locals(const Chars *,const JgcChar *,int,float);
typedef struct {unsigned queued_bones,me_bones,cpu_bones,reused_bones,cursor_reused;
    unsigned batches,wait_us,cancelled,chunks,me_ticks,max_chunk_ticks,storage_bytes;int verify_status;} CharPoseProfile;
void chars_pose_profile(CharPoseProfile *);
static inline const Chars *chars_motion_owner(const Chars *c){return c->motion_owner ? c->motion_owner:c;}
/* Bind the existing gameplay FSCA table; NULL retains libm for standalone
   import tools. The PSP binds once before loading/drawing any characters. */
void chars_set_pose_sine(float (*sine)(int));
void chars_local_matrix(float *m,unsigned ev,const float *pos,const int *ang,const float *scl);
static inline void chars_affine_multiply(float *out,const float *a,const float *b)
{
    float t[16];
    for(int row=0;row<3;row++)for(int col=0;col<4;col++) {
        float v=0.f;
        for(int k=0;k<3;k++)v+=a[row*4+k]*b[k*4+col];
        t[row*4+col]=col==3 ? v+a[row*4+3]:v;
    }
    t[12]=t[13]=t[14]=0.f;t[15]=1.f;
    for(int i=0;i<16;i++)out[i]=t[i];
}
void chars_sample_pose(const Chars *c, const JgcChar *ch, int motion,
                       float frame, unsigned bone, CharPose *pose);
void chars_blend_pose(CharPose *a, const CharPose *b, float weight);
void chars_overlay_pose(const Chars *c,const JgcChar *ch,int motion,float frame,
                         unsigned bone,float weight,CharPose *pose);
/* Original collision/attachment hierarchy (0be244): all TRS channels,
   Z-Y-X rotation, same channel/parent order as the visible PLAY hierarchy.
   The caller supplies ch->nbones matrices (up to 64). */
void chars_attachment_matrices(const Chars *c,const JgcChar *ch,
                              const CharPose *pose,const float *root,float (*mats)[16]);
#endif
