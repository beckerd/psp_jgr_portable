#ifndef FINALTAGS_H
#define FINALTAGS_H
#include "object_graffiti.h"
#include "graffiti.h"
typedef struct {
    GfoObject objects[5];
    const uint32_t *materials[5];
    const SprayAnchor *anchors[5];
    const unsigned char *subvolumes[5];
    unsigned style[5],nanchors[5],nvolumes[5];
    /* Exact immutable (source XYZ bits, position/direction) aliases across
       descriptor, camera volume and anchors, built once at load. */
    unsigned char point_map[5][34],point_count[5];
    /* Fixed, bank-owned result cache: no borrowed Spot/GfoObject output or
       optional allocation. Exact matrix changes use the ordinary transform. */
    float pose_matrix[5][16],pose_points[5][34][3];unsigned pose_valid;
    float camera_root[5][16],camera_result[5][16];unsigned camera_valid;
} FinalTags;
/* Caller owns immutable FGT1 image. Transactional, no partial target bank. */
int finaltags_init(FinalTags *,const void *,unsigned length);
/* 0c4ab4 consumes attachment matrix BEFORE descriptor TRS; anchors only
   transform their position, copying the other five words unchanged. */
void finaltags_pose(FinalTags *,unsigned index,const float matrix[16],Spot *);
/* Camera descriptor TRS is owned by the caller. Cache its exact completed
   row-major result without borrowing or modifying a Spot/progress object. */
int finaltags_camera_reuse(const FinalTags *,unsigned index,const float root[16],float out[16]);
void finaltags_camera_store(FinalTags *,unsigned index,const float root[16],const float result[16]);
void finaltags_progress(FinalTags *,unsigned index,const Spot *);
void finaltags_anchor(const FinalTags *,unsigned index,unsigned stroke,SprayAnchor *);
void finaltags_subvolume(const FinalTags *,unsigned index,unsigned volume,unsigned char out[176]);
/* GU-only bindings, loaded once for the selected source S/XL designs. */
typedef struct { unsigned short *small,*large,*mask; } FinalTagArt;
int finaltags_art_load(FinalTagArt *,unsigned small,unsigned large);
void finaltags_art_free(FinalTagArt *);
void finaltags_draw(const FinalTags *,const Graffiti *,const FinalTagArt *);
#endif
