#ifndef TEST_PSPGUM_H
#define TEST_PSPGUM_H
#ifdef JGR_TEST_GUM_MODEL
static int gum_mode=GU_MODEL;static float gum_saved[16],gum_matrices[3][16];
static unsigned gum_depth,gum_dirty[3],gum_pushes,gum_pops,gum_publications;
static inline void sceGumMatrixMode(int mode) {gum_mode=mode;}
static inline void sceGumLoadIdentity(void)
{memset(gum_matrices[gum_mode],0,64);for(unsigned k=0;k<4;k++)gum_matrices[gum_mode][k*5]=1;gum_dirty[gum_mode]=1;}
static inline void sceGumPushMatrix(void)
{assert(!gum_depth++ && gum_mode==GU_MODEL);memcpy(gum_saved,gum_matrices[GU_MODEL],64);gum_pushes++;}
static inline void sceGumLoadMatrix(const ScePspFMatrix4 *matrix)
{memcpy(gum_matrices[gum_mode],matrix,64);gum_dirty[gum_mode]=1;}
static inline void sceGumPopMatrix(void)
{assert(gum_depth--==1 && gum_mode==GU_MODEL);memcpy(gum_matrices[GU_MODEL],gum_saved,64);gum_dirty[GU_MODEL]=1;gum_pops++;}
static inline void sceGumUpdateMatrix(void)
{for(unsigned mode=0;mode<3;mode++)if(gum_dirty[mode]){sceGuSetMatrix(mode,(const ScePspFMatrix4 *)gum_matrices[mode]);gum_dirty[mode]=0;gum_publications++;}}
/* Match the SDK: a GUM draw flushes dirty CPU state before its GE draw.
   Direct sceGuSetMatrix must neither change nor dirty the GUM copy. */
static inline void gum_test_draw(int primitive,int format,int count,const void *indices,const void *vertices)
{sceGumUpdateMatrix();sceGuDrawArray(primitive,format,count,indices,vertices);}
#undef sceGumDrawArray
#define sceGumDrawArray gum_test_draw
#else
static inline void sceGumMatrixMode(int mode) {}
static inline void sceGumLoadIdentity(void) {}
static inline void sceGumPushMatrix(void) {}
static inline void sceGumLoadMatrix(const ScePspFMatrix4 *matrix) {(void)matrix;}
static inline void sceGumPopMatrix(void) {}
#endif
#endif
