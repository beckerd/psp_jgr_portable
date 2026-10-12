/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {unsigned type,frames,first_box,boxes,sound;} HouseType;
typedef struct {float vertices[24],center[3];} HouseBox;
typedef struct {unsigned address,type,sector;float position[3];int32_t angles[3];} HousePlacement;


#include "source_data_pointers.h"
float house_lod_distance2 __asm__("jgr_ptr_kogane_house_data_house_lod_distance2") __attribute__((aligned(16)));
HouseType house_types[9] __asm__("jgr_ptr_kogane_house_data_house_types") __attribute__((aligned(16)));
HouseBox house_boxes[10] __asm__("jgr_ptr_kogane_house_data_house_boxes") __attribute__((aligned(16)));
HousePlacement house_placements[39] __asm__("jgr_ptr_kogane_house_data_house_placements") __attribute__((aligned(16)));
float house_lights[114][8] __asm__("jgr_ptr_kogane_house_data_house_lights") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {3152630048,128,4,16,&house_lod_distance2},
    {1296100375,144,180,16,house_types},
    {3649165262,336,1080,16,house_boxes},
    {4189614681,1424,1404,16,house_placements},
    {2644670438,2832,3648,16,house_lights},
};
int source_data_ptr_kogane_house_data_load(void){return source_data_pointers_load_file("source_data_ptr_kogane_house_data.jsd",sections,5u,6480u,0x14b93c78u,0u);}
#endif
