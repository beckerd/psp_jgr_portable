/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {
    unsigned flags;
    float vertex[4][3], normal[3], center[3], radius_sq;
} MissionQuad;
typedef struct {int parent;float pos[3];int32_t angles[3];float scale[3],vertices[24],center[3];} TrainBody;
typedef struct {unsigned indices[3];unsigned short surface,mask,flags;} TrainTriangle;
typedef struct {unsigned bone;float position[3];} TrainVertex;

#include "source_data_sections.h"
unsigned train_selectors[8][2] __asm__("jgr_data_benten_train_data_train_selectors") __attribute__((aligned(16)));
MissionQuad train_boundaries[5] __asm__("jgr_data_benten_train_data_train_boundaries") __attribute__((aligned(16)));
TrainBody train_bodies[4] __asm__("jgr_data_benten_train_data_train_bodies") __attribute__((aligned(16)));
TrainVertex train_vertices0[8] __asm__("jgr_data_benten_train_data_train_vertices0") __attribute__((aligned(16)));
TrainTriangle train_surfaces0[4] __asm__("jgr_data_benten_train_data_train_surfaces0") __attribute__((aligned(16)));
TrainVertex train_vertices1[104] __asm__("jgr_data_benten_train_data_train_vertices1") __attribute__((aligned(16)));
TrainTriangle train_surfaces1[108] __asm__("jgr_data_benten_train_data_train_surfaces1") __attribute__((aligned(16)));
float train_lights[46][8] __asm__("jgr_data_benten_train_data_train_lights") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3981119968,160,64,16,train_selectors},
    {2630166174,224,400,16,train_boundaries},
    {3751412122,624,592,16,train_bodies},
    {613462328,1216,128,16,train_vertices0},
    {1825713421,1344,80,16,train_surfaces0},
    {1402438062,1424,1664,16,train_vertices1},
    {466951579,3088,2160,16,train_surfaces1},
    {712930179,5248,1472,16,train_lights},
};
int source_data_benten_train_data_load(void){return source_data_sections_load("source_data_benten_train_data.jsd",sections,8u,6720u,0x394f69a9u);}
#endif
