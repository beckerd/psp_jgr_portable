#ifndef JGR_GARAGE_SHADOW_H
#define JGR_GARAGE_SHADOW_H
#include "charanim.h"
#include <stdint.h>
typedef struct {char magic[4];uint32_t nodes,vertices,indices,off_nodes,off_vertices,off_indices,size;} GarageShadowHeader;
typedef struct {
    int16_t parent;uint16_t ev;float pos[3];int32_t ang[3];float scale[3];
    uint32_t v0,nv,i0,ni,strips;
} GarageShadowNode;
typedef struct {
    unsigned char *image;const GarageShadowHeader *header;
    const uint32_t (*residents)[2];const GarageShadowNode *nodes;
    const float (*vertices)[3];const int16_t *indices;
    float *radius; /* Conservative local cubes for the modifier meshes. */
} GarageShadow;
int garage_shadow_load(GarageShadow *,const char *path);
void garage_shadow_free(GarageShadow *);
/* Source hierarchy/motion pairing. Geometry callback owns PSP adaptation. */
typedef void (*GarageShadowSubmit)(void *,const float (*)[3],unsigned,const uint16_t *,unsigned);
int garage_shadow_resident(const GarageShadow *,const Chars *,int resident,int motion,
    float frame,const float root[16],const float view[16],GarageShadowSubmit,void *);
/* Scoped world-space camera planes; NULL retains the full reference path. */
void garage_shadow_frustum(const float planes[6][4]);
void garage_shadow_profile(unsigned out[4]); /* pose, transform, volume, submit (us) */
void garage_shadow_exact(int enabled); /* ordered binary64 vertex transform (host default) or native float (PSP default) */
void garage_shadow_begin(void);
void garage_shadow_submit(void *,const float (*)[3],unsigned,const uint16_t *,unsigned);
void garage_shadow_end(const float view[16]);
#endif
