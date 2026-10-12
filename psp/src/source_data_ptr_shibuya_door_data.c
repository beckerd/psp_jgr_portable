/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {unsigned type,kind,frames,first_box,boxes,sound,bank;} OpeningType;
typedef struct {float vertices[24],center[3];} OpeningBox;
typedef struct {unsigned address,type,sector;float position[3];int32_t angles[3];float scale[3];} OpeningPlacement;
typedef struct { float cx, cy, cz, r; unsigned first_batch, n_batch, flags, pad; } JgsObj;
typedef struct {unsigned object;JgsObj original;} OpeningBaked;
enum {OPENING_AUTO,OPENING_GATE,OPENING_WINDOW,OPENING_DOOR,OPENING_HINGED,OPENING_BARRIER};


#include "source_data_pointers.h"
OpeningType shibuya_door_types[4] __asm__("jgr_ptr_shibuya_door_data_shibuya_door_types") __attribute__((aligned(16)));
unsigned shibuya_door_motions[4][2] __asm__("jgr_ptr_shibuya_door_data_shibuya_door_motions") __attribute__((aligned(16)));
OpeningBox shibuya_door_boxes[4] __asm__("jgr_ptr_shibuya_door_data_shibuya_door_boxes") __attribute__((aligned(16)));
OpeningPlacement shibuya_door_placements[8] __asm__("jgr_ptr_shibuya_door_data_shibuya_door_placements") __attribute__((aligned(16)));
OpeningBaked shibuya_door_baked[8] __asm__("jgr_ptr_shibuya_door_data_shibuya_door_baked") __attribute__((aligned(16)));
float shibuya_door_lights[63][8] __asm__("jgr_ptr_shibuya_door_data_shibuya_door_lights") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {3518575158,144,112,16,shibuya_door_types},
    {3217235691,256,32,16,shibuya_door_motions},
    {1165494767,288,432,16,shibuya_door_boxes},
    {2759014158,720,384,16,shibuya_door_placements},
    {940893746,1104,288,16,shibuya_door_baked},
    {3512180326,1392,2016,16,shibuya_door_lights},
};
int source_data_ptr_shibuya_door_data_load(void){return source_data_pointers_load_file("source_data_ptr_shibuya_door_data.jsd",sections,6u,3408u,0x2bb9fe66u,0u);}
#endif
