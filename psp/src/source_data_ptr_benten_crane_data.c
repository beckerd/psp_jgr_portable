/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {unsigned address,sector;float position[3];int32_t angles[3];float scale[3];} CranePlacement;
typedef struct CharPose { float pos[3], scl[3]; int ang[3]; unsigned ev, keyed; } CharPose;
typedef struct {unsigned indices[3];unsigned short surface,mask,flags;} CraneTriangle;


#include "source_data_pointers.h"
CranePlacement crane_placements[1] __asm__("jgr_ptr_benten_crane_data_crane_placements") __attribute__((aligned(16)));
CharPose crane_bind[2] __asm__("jgr_ptr_benten_crane_data_crane_bind") __attribute__((aligned(16)));
float crane_vertices[16][3] __asm__("jgr_ptr_benten_crane_data_crane_vertices") __attribute__((aligned(16)));
CraneTriangle crane_triangles[20] __asm__("jgr_ptr_benten_crane_data_crane_triangles") __attribute__((aligned(16)));
float crane_lights[46][8] __asm__("jgr_ptr_benten_crane_data_crane_lights") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {1444656522,128,44,16,crane_placements},
    {417458311,176,88,16,crane_bind},
    {1420752942,272,192,16,crane_vertices},
    {2278417035,464,400,16,crane_triangles},
    {1468867890,864,1472,16,crane_lights},
};
int source_data_ptr_benten_crane_data_load(void){return source_data_pointers_load_file("source_data_ptr_benten_crane_data.jsd",sections,5u,2336u,0x5538dab7u,0u);}
#endif
