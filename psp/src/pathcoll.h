#ifndef JGR_PATHCOLL_H
#define JGR_PATHCOLL_H
#include "scene.h"
#include "charpose.h"
#include "collide.h"
/* Original single-node path-object collider, not a triangulated body box.
   kind 6 = authored surface vertices, kind 4 = eight body-volume vertices.
   The caller owns output storage. No scene pointers survive the call. */
int pathcoll_sample(const Scene *,uint32_t node,uint32_t motion,float frame,
                    float *vertices,unsigned capacity,CharPose *pose);
/* Decode original type-6 strips against a sampled vertex cache, retaining
   source winding/mask/type. NULL output returns the required triangle count.
   Type-4 body volumes are explicitly rejected, never flattened into floors. */
int pathcoll_surfaces(const Scene *,uint32_t node,const float *vertices,unsigned nvertices,
                      ColTri *out,unsigned capacity);
/* 0be0e4/0c820c: authored body center and separate derived sphere bound. */
int pathcoll_body(const Scene *,uint32_t node,uint32_t motion,float frame,
                  float vertices[24],float center[3],float bounds[4]);
/* Conservative root-centered cube, without transformed vertices/normals. */
int pathcoll_body_bound(const Scene *,uint32_t node,uint32_t motion,float frame,float bounds[4]);
#endif
