/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct { float v[9]; unsigned short surf, mask, dbl, src; } ColTri;
typedef struct {float vertices[24],center[3];} OpeningBox;
typedef struct {unsigned address,type,sector;float position[3];int32_t angles[3];float scale[3];} OpeningPlacement;
typedef struct {unsigned type,kind,frames,first_box,boxes,sound,bank;} OpeningType;

#include "source_data_sections.h"
OpeningType bantam_types[2] __asm__("jgr_data_bantam_opening_data_bantam_types") __attribute__((aligned(16)));
OpeningBox bantam_boxes[2] __asm__("jgr_data_bantam_opening_data_bantam_boxes") __attribute__((aligned(16)));
OpeningPlacement bantam_placements[12] __asm__("jgr_data_bantam_opening_data_bantam_placements") __attribute__((aligned(16)));
float bantam_lights[71][8] __asm__("jgr_data_bantam_opening_data_bantam_lights") __attribute__((aligned(16)));
ColTri bantam_door_floors[4] __asm__("jgr_data_bantam_opening_data_bantam_door_floors") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {4050069511,112,56,16,bantam_types},
    {1705382878,176,216,16,bantam_boxes},
    {352660827,400,576,16,bantam_placements},
    {2158575686,976,2272,16,bantam_lights},
    {2960774700,3248,176,16,bantam_door_floors},
};
int source_data_bantam_opening_data_load(void){return source_data_sections_load("source_data_bantam_opening_data.jsd",sections,5u,3424u,0xf7988d9eu);}
#endif
