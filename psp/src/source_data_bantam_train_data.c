/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {int parent;float pos[3];int32_t angles[3];float scale[3],vertices[24],center[3];} TrainBody;
typedef struct {unsigned indices[3];unsigned short surface,mask,flags;} TrainTriangle;
typedef struct {unsigned bone;float position[3];} TrainVertex;

#include "source_data_sections.h"
TrainBody bantam_train_bodies[4] __asm__("jgr_data_bantam_train_data_bantam_train_bodies") __attribute__((aligned(16)));
TrainVertex bantam_train_vertices0[104] __asm__("jgr_data_bantam_train_data_bantam_train_vertices0") __attribute__((aligned(16)));
TrainTriangle bantam_train_surfaces0[108] __asm__("jgr_data_bantam_train_data_bantam_train_surfaces0") __attribute__((aligned(16)));
float bantam_train_lights[71][8] __asm__("jgr_data_bantam_train_data_bantam_train_lights") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1865332987,96,592,16,bantam_train_bodies},
    {2492363515,688,1664,16,bantam_train_vertices0},
    {3704417998,2352,2160,16,bantam_train_surfaces0},
    {2596981474,4512,2272,16,bantam_train_lights},
};
int source_data_bantam_train_data_load(void){return source_data_sections_load("source_data_bantam_train_data.jsd",sections,4u,6784u,0x19d317fdu);}
#endif
