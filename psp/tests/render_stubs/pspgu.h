/* Recording GU adapter for charanim.c host tests; values match PSPSDK. */
#ifndef TEST_PSPGU_H
#define TEST_PSPGU_H
#include <stddef.h>
#include <assert.h>
#include <string.h>
#define GU_FALSE 0
#define GU_TRUE 1
#define GU_TRIANGLES 3
#define GU_TRIANGLE_STRIP 4
#define GU_SPRITES 6
#define GU_VIEW 1
#define GU_STENCIL_TEST 2
#define GU_STENCIL_BUFFER_BIT 2
#define GU_ALWAYS 1
#define GU_NOTEQUAL 3
#define GU_GEQUAL 7
#define GU_KEEP 0
#define GU_INCR 4
#define GU_DECR 5
#define GU_TRANSFORM_2D (1<<23)
#define GU_ALPHA_TEST 0
#define GU_DEPTH_TEST 1
#define GU_BLEND 4
#define GU_CULL_FACE 5
#define GU_TEXTURE_2D 9
#define GU_MODEL 2
#define GU_PROJECTION 0
#define GU_DEPTH_BUFFER_BIT 4
#define GU_TEXTURE_32BITF 3
#define GU_COLOR_8888 (7<<2)
#define GU_VERTEX_32BITF (3<<7)
#define GU_INDEX_16BIT (2<<11)
#define GU_WEIGHT_8BIT (1<<9)
#define GU_WEIGHTS(n) ((((n)-1)&7)<<14)
#define GU_TRANSFORM_3D 0
#define GU_CW 0
#define GU_CCW 1
#define GU_ADD 0
#define GU_OTHER_COLOR 0
#define GU_ONE_MINUS_OTHER_COLOR 1
#define GU_SRC_ALPHA 2
#define GU_ONE_MINUS_SRC_ALPHA 3
#define GU_DST_ALPHA 4
#define GU_ONE_MINUS_DST_ALPHA 5
#define GU_FIX 10
#define GU_GREATER 6
#define GU_PSM_8888 3
#define GU_PSM_4444 2
#define GU_PSM_T8 5
#define GU_TFX_MODULATE 0
#define GU_TCC_RGBA 1
#define GU_LINEAR 1
#define GU_LINEAR_MIPMAP_NEAREST 5
#define GU_TEXTURE_AUTO 0
#define GU_REPEAT 0
#define GU_CLAMP 1
typedef struct { float m[16]; } ScePspFMatrix4;
static struct { int enabled[10],depthmask,frontface,op,src,dst; unsigned sf,df;
    int texmode,texwidth,texheight,texstride; const void *texture;
    int maxmips,minfilter,magfilter,levelmode; float levelbias;
    int mipwidth[8],mipheight[8],mipstride[8]; const void *miptexture[8]; } gu;
/* Includes large STAGELAST background submissions, not only tiny effects. */
_Alignas(16) static unsigned char gu_memory[2*1024*1024];
static size_t gu_used;
static inline void sceGuEnable(int s) { gu.enabled[s]=1; }
static inline void sceGuDisable(int s) { gu.enabled[s]=0; }
static inline void sceGuDepthMask(int mask) { gu.depthmask=mask; }
static unsigned gu_depthoffset;
static inline void sceGuDepthOffset(unsigned offset) { gu_depthoffset=offset; }
static inline void sceGuFrontFace(int face) { gu.frontface=face; }
static inline void sceGuBlendFunc(int op,int src,int dst,unsigned sf,unsigned df)
{ gu.op=op;gu.src=src;gu.dst=dst;gu.sf=sf;gu.df=df; }
static inline void *sceGuGetMemory(int bytes)
{ size_t p=gu_used;gu_used+=(bytes+15)&~15;assert(gu_used<=sizeof gu_memory);return gu_memory+p; }
static void sceGuDrawArray(int primitive,int vtype,int count,const void *indices,const void *vertices);
static float gu_matrices[3][16];static int gu_depthfunc;static unsigned gu_clear;
static inline void sceGuSetMatrix(int mode,const ScePspFMatrix4 *m)
{ assert(mode>=0 && mode<3);for(int k=0;k<16;k++)gu_matrices[mode][k]=m->m[k]; }
static float gu_bones[8][16];static unsigned gu_bone_uploads;
static inline void sceGuBoneMatrix(unsigned index,const ScePspFMatrix4 *m)
{assert(index<8);memcpy(gu_bones[index],m,sizeof *m);gu_bone_uploads++;}
static unsigned gu_pixelmask;static int gu_stencilfunc,gu_stencilref,gu_stencilmask,gu_stencilop[3];
static inline void sceGuClearStencil(unsigned s){assert(s==128);}
static inline void sceGuClear(unsigned bits){assert(bits==GU_STENCIL_BUFFER_BIT || bits==GU_DEPTH_BUFFER_BIT);gu_clear=bits;}
static inline void sceGuStencilFunc(int f,int r,int m){gu_stencilfunc=f;gu_stencilref=r;gu_stencilmask=m;}
static inline void sceGuStencilOp(int f,int zf,int zp){gu_stencilop[0]=f;gu_stencilop[1]=zf;gu_stencilop[2]=zp;}
static inline void sceGuPixelMask(unsigned m){gu_pixelmask=m;}
static inline void sceGuDepthFunc(int f){gu_depthfunc=f;}
static inline void sceGuColor(unsigned c){}
static int gu_alpha[3],gu_clut_mode[4],gu_clut_blocks;
static const void *gu_clut;
static inline void sceGuAlphaFunc(int op,int ref,int mask) {gu_alpha[0]=op;gu_alpha[1]=ref;gu_alpha[2]=mask;}
static inline void sceGuClutMode(int a,int b,int c,int d) {gu_clut_mode[0]=a;gu_clut_mode[1]=b;gu_clut_mode[2]=c;gu_clut_mode[3]=d;}
static inline void sceGuClutLoad(int n,const void *p) {gu_clut_blocks=n;gu_clut=p;}
static inline void sceGuTexMode(int a,int b,int c,int d) { gu.texmode=a;gu.maxmips=b; }
static inline void sceGuTexImage(int a,int b,int c,int d,const void *p)
{ assert(a>=0 && a<8);gu.mipwidth[a]=b;gu.mipheight[a]=c;gu.mipstride[a]=d;gu.miptexture[a]=p;
  if(!a){gu.texwidth=b;gu.texheight=c;gu.texstride=d;gu.texture=p;} }
static inline void sceGuTexFunc(int a,int b) {}
static inline void sceGuTexFilter(int a,int b) {gu.minfilter=a;gu.magfilter=b;}
static inline void sceGuTexLevelMode(int a,float b) {gu.levelmode=a;gu.levelbias=b;}
static inline void sceGuTexWrap(int a,int b) {}
static float gu_uv_offset[2];
static inline void sceGuTexOffset(float u,float v){gu_uv_offset[0]=u;gu_uv_offset[1]=v;}
static int gu_scissor[4];
static inline void sceGuScissor(int a,int b,int c,int d)
{gu_scissor[0]=a;gu_scissor[1]=b;gu_scissor[2]=a+c;gu_scissor[3]=b+d;}
#endif
