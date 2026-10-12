#ifndef JGR_GE_SUBMIT_H
#define JGR_GE_SUBMIT_H
#include <pspgu.h>
#include <pspgum.h>
#ifdef __PSP__
void ge_submit_bone(unsigned bone,const float *row_major);
void ge_submit_model(const float *row_major);
#else
static inline void ge_submit_bone(unsigned bone,const float *in)
{
    ScePspFMatrix4 m;float *out=(float *)&m;
    for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++)out[c*4+r]=in[r*4+c];
    sceGuBoneMatrix(bone,&m);
}
static inline void ge_submit_model(const float *in)
{
    ScePspFMatrix4 m;float *out=(float *)&m;
    /* The GE consumes the affine 3x4 only, including when the caller
       supplies a compact instance matrix rather than a full 4x4. */
    for(unsigned c=0;c<4;c++) {
        for(unsigned r=0;r<3;r++)out[c*4+r]=in[r*4+c];
        out[c*4+3]=c==3 ? 1.f:0.f;
    }
    sceGuSetMatrix(GU_MODEL,&m);
}
#endif
#if defined(__PSP__) || defined(JGR_TEST_GE_SUBMIT)
void ge_submit_reset(void);
void ge_submit_profile(unsigned out[2]);
void ge_submit_draw(int primitive,int format,int count,const void *indices,const void *vertices);
/* Former SDK-direct HUD/effect calls. The reference switch is diagnostic:
   existing city/actor batching is unchanged for a same-build A/B test. */
void ge_submit_aux(int primitive,int format,int count,const void *indices,const void *vertices);
void ge_submit_aux_mode(int enabled);
int ge_submit_aux_batched(void);
void ge_submit_call(const void *list);
static inline void ge_submit_gum(int primitive,int format,int count,const void *indices,const void *vertices)
{sceGumUpdateMatrix();ge_submit_draw(primitive,format,count,indices,vertices);}
#else
static inline void ge_submit_reset(void){}
static inline void ge_submit_profile(unsigned out[2]){out[0]=out[1]=0;}
#define ge_submit_draw sceGuDrawArray
#define ge_submit_aux sceGuDrawArray
static inline void ge_submit_aux_mode(int enabled){(void)enabled;}
static inline int ge_submit_aux_batched(void){return 1;}
#define ge_submit_gum sceGumDrawArray
#endif
#endif
