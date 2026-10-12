/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {float pos[3];int32_t ang[3];float scale[3];} GrindPlacement;
typedef struct CharPose { float pos[3], scl[3]; int ang[3]; unsigned ev, keyed; } CharPose;
typedef struct {float vertices[9];unsigned bone,surface,mask,flags;} GrindTriangle;
typedef struct {unsigned first,count,texture,period,times[8];float position[3];unsigned source_flags;} GrindLight;


#include "source_data_pointers.h"
GrindPlacement grind_placements[2] __asm__("jgr_ptr_grind_props_data_grind_placements") __attribute__((aligned(16)));
CharPose grind_floor_bind[6] __asm__("jgr_ptr_grind_props_data_grind_floor_bind") __attribute__((aligned(16)));
int grind_floor_parents[6] __asm__("jgr_ptr_grind_props_data_grind_floor_parents") __attribute__((aligned(16)));
CharPose grind_body_bind[5] __asm__("jgr_ptr_grind_props_data_grind_body_bind") __attribute__((aligned(16)));
int grind_body_parents[5] __asm__("jgr_ptr_grind_props_data_grind_body_parents") __attribute__((aligned(16)));
GrindTriangle grind_triangles[76] __asm__("jgr_ptr_grind_props_data_grind_triangles") __attribute__((aligned(16)));
float grind_box[24] __asm__("jgr_ptr_grind_props_data_grind_box") __attribute__((aligned(16)));
GrindLight grind_lights[13] __asm__("jgr_ptr_grind_props_data_grind_lights") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {2225552312,176,72,16,grind_placements},
    {308886196,256,264,16,grind_floor_bind},
    {3984187515,528,24,16,grind_floor_parents},
    {1300442233,560,220,16,grind_body_bind},
    {675896198,784,20,16,grind_body_parents},
    {918957485,816,3952,16,grind_triangles},
    {1435294207,4768,96,16,grind_box},
    {2357627138,4864,832,16,grind_lights},
};
int source_data_ptr_grind_props_data_load(void){return source_data_pointers_load_file("source_data_ptr_grind_props_data.jsd",sections,8u,5696u,0x4365e3a8u,0u);}
#endif
