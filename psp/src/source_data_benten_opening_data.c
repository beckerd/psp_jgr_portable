/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {float vertices[24],center[3];} OpeningBox;
typedef struct {unsigned address,type,sector;float position[3];int32_t angles[3];float scale[3];} OpeningPlacement;
typedef struct {unsigned type,kind,frames,first_box,boxes,sound,bank;} OpeningType;


#include "source_data_sections.h"
OpeningType benten_types[5] __asm__("jgr_data_benten_opening_data_benten_types") __attribute__((aligned(16)));
OpeningBox benten_boxes[5] __asm__("jgr_data_benten_opening_data_benten_boxes") __attribute__((aligned(16)));
OpeningPlacement benten_placements[11] __asm__("jgr_data_benten_opening_data_benten_placements") __attribute__((aligned(16)));
float benten_lights[46][8] __asm__("jgr_data_benten_opening_data_benten_lights") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2059416849,96,140,16,benten_types},
    {3993046728,240,540,16,benten_boxes},
    {1553386078,784,528,16,benten_placements},
    {1962299914,1312,1472,16,benten_lights},
};
int source_data_benten_opening_data_load(void){return source_data_sections_load("source_data_benten_opening_data.jsd",sections,4u,2784u,0x372dcf00u);}
#endif
