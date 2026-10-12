#ifndef MISSION_QUAD_H
#define MISSION_QUAD_H
/* Original 0x50-byte JMDc quad. Stored vertex order is 0,1,3,2 around
   its perimeter; normal is source data, not recomputed from the vertices. */
typedef struct {
    unsigned flags;
    float vertex[4][3], normal[3], center[3], radius_sq;
} MissionQuad;
void mission_quad_prepare(MissionQuad *q); /* 8c0c2d78 */
int mission_quad_near(const MissionQuad *q, const float segment[6]); /* 8c0c2f6c */
unsigned mission_quad_cross(const MissionQuad *q, const float segment[6]); /* 8c0c2fdc */
#endif
