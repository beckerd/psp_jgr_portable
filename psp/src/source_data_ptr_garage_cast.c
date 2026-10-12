/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {
    int primary,secondary;
    float x,y,z;int yaw;unsigned frames[2];
} GarageResident;
typedef struct {
    unsigned slot,color;
    float x,y,w,h,u0,v0,u1,v1;
} GarageQuad;


#include "source_data_pointers.h"
GarageResident GARAGE_RESIDENTS[15] __asm__("jgr_ptr_garage_cast_GARAGE_RESIDENTS") __attribute__((aligned(16)));
unsigned GARAGE_SPECIAL_PROP __asm__("jgr_ptr_garage_cast_GARAGE_SPECIAL_PROP") __attribute__((aligned(16)));
unsigned GARAGE_PET_MODELS[3] __asm__("jgr_ptr_garage_cast_GARAGE_PET_MODELS") __attribute__((aligned(16)));
unsigned GARAGE_PET_FRAMES[3] __asm__("jgr_ptr_garage_cast_GARAGE_PET_FRAMES") __attribute__((aligned(16)));
float GARAGE_PET_POSITION[3] __asm__("jgr_ptr_garage_cast_GARAGE_PET_POSITION") __attribute__((aligned(16)));
int GARAGE_PET_YAW __asm__("jgr_ptr_garage_cast_GARAGE_PET_YAW") __attribute__((aligned(16)));
unsigned GARAGE_VOICE_PROGRAM[15] __asm__("jgr_ptr_garage_cast_GARAGE_VOICE_PROGRAM") __attribute__((aligned(16)));
unsigned GARAGE_VOICE_TICK[15] __asm__("jgr_ptr_garage_cast_GARAGE_VOICE_TICK") __attribute__((aligned(16)));
float GARAGE_CAMERA_0[150][6] __asm__("jgr_ptr_garage_cast_GARAGE_CAMERA_0") __attribute__((aligned(16)));
unsigned GARAGE_CAMERA_FOV_0 __asm__("jgr_ptr_garage_cast_GARAGE_CAMERA_FOV_0") __attribute__((aligned(16)));
float GARAGE_CAMERA_1[120][6] __asm__("jgr_ptr_garage_cast_GARAGE_CAMERA_1") __attribute__((aligned(16)));
unsigned GARAGE_CAMERA_FOV_1 __asm__("jgr_ptr_garage_cast_GARAGE_CAMERA_FOV_1") __attribute__((aligned(16)));
float GARAGE_STOPS_0[8] __asm__("jgr_ptr_garage_cast_GARAGE_STOPS_0") __attribute__((aligned(16)));
float GARAGE_STOPS_1[8] __asm__("jgr_ptr_garage_cast_GARAGE_STOPS_1") __attribute__((aligned(16)));
unsigned char GARAGE_SELECT_ORDER[15] __asm__("jgr_ptr_garage_cast_GARAGE_SELECT_ORDER") __attribute__((aligned(16)));
float GARAGE_SELECT_CAMERA[15][6] __asm__("jgr_ptr_garage_cast_GARAGE_SELECT_CAMERA") __attribute__((aligned(16)));
unsigned GARAGE_SELECT_FOV __asm__("jgr_ptr_garage_cast_GARAGE_SELECT_FOV") __attribute__((aligned(16)));
unsigned char GARAGE_SPECIAL_CHAR[2] __asm__("jgr_ptr_garage_cast_GARAGE_SPECIAL_CHAR") __attribute__((aligned(16)));
float GARAGE_SPECIAL_CAMERA[2][6] __asm__("jgr_ptr_garage_cast_GARAGE_SPECIAL_CAMERA") __attribute__((aligned(16)));
GarageQuad GARAGE_QUADS[20] __asm__("jgr_ptr_garage_cast_GARAGE_QUADS") __attribute__((aligned(16)));
unsigned char GARAGE_LISTS[18][2] __asm__("jgr_ptr_garage_cast_GARAGE_LISTS") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {1631969346,384,480,16,GARAGE_RESIDENTS},
    {458780463,864,4,16,&GARAGE_SPECIAL_PROP},
    {1615959195,880,12,16,GARAGE_PET_MODELS},
    {2062101928,896,12,16,GARAGE_PET_FRAMES},
    {2047535985,912,12,16,GARAGE_PET_POSITION},
    {2320234203,928,4,16,&GARAGE_PET_YAW},
    {1235838315,944,60,16,GARAGE_VOICE_PROGRAM},
    {4276454287,1008,60,16,GARAGE_VOICE_TICK},
    {4068105257,1072,3600,16,GARAGE_CAMERA_0},
    {3650458625,4672,4,16,&GARAGE_CAMERA_FOV_0},
    {2239589567,4688,2880,16,GARAGE_CAMERA_1},
    {2928845975,7568,4,16,&GARAGE_CAMERA_FOV_1},
    {914015567,7584,32,16,GARAGE_STOPS_0},
    {1098773977,7616,32,16,GARAGE_STOPS_1},
    {2888925983,7648,15,16,GARAGE_SELECT_ORDER},
    {1945616478,7664,360,16,GARAGE_SELECT_CAMERA},
    {347457774,8032,4,16,&GARAGE_SELECT_FOV},
    {952696154,8048,2,16,GARAGE_SPECIAL_CHAR},
    {360200100,8064,48,16,GARAGE_SPECIAL_CAMERA},
    {3501101667,8112,800,16,GARAGE_QUADS},
    {124865592,8912,36,16,GARAGE_LISTS},
};
int source_data_ptr_garage_cast_load(void){return source_data_pointers_load_file("source_data_ptr_garage_cast.jsd",sections,21u,8948u,0x20dfaf4eu,0u);}
#endif
