/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {
    unsigned slot,color;
    float x,y,w,h,u0,v0,u1,v1;
} GarageQuad;


#include "source_data_pointers.h"
signed char PENDING_MODEL[15] __asm__("jgr_ptr_garage_pending_PENDING_MODEL") __attribute__((aligned(16)));
unsigned PENDING_FRAMES[15] __asm__("jgr_ptr_garage_pending_PENDING_FRAMES") __attribute__((aligned(16)));
float PENDING_POSITION[3] __asm__("jgr_ptr_garage_pending_PENDING_POSITION") __attribute__((aligned(16)));
float PENDING_CAMERA[244][6] __asm__("jgr_ptr_garage_pending_PENDING_CAMERA") __attribute__((aligned(16)));
int PENDING_ROLL[244] __asm__("jgr_ptr_garage_pending_PENDING_ROLL") __attribute__((aligned(16)));
int PENDING_FOV[244] __asm__("jgr_ptr_garage_pending_PENDING_FOV") __attribute__((aligned(16)));
const char * PENDING_TEXT[13] __asm__("jgr_ptr_garage_pending_PENDING_TEXT") __attribute__((aligned(16)));
GarageQuad PENDING_QUADS[9] __asm__("jgr_ptr_garage_pending_PENDING_QUADS") __attribute__((aligned(16)));
unsigned PENDING_LISTS[5][2] __asm__("jgr_ptr_garage_pending_PENDING_LISTS") __attribute__((aligned(16)));
unsigned char literal_pool_21[181] __asm__("jgr_ptr_garage_pending_literal_pool_21") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {801852936,416,15,16,PENDING_MODEL},
    {875304332,432,60,16,PENDING_FRAMES},
    {2749430924,496,12,16,PENDING_POSITION},
    {4049498803,512,5856,16,PENDING_CAMERA},
    {304581171,6368,976,16,PENDING_ROLL},
    {2034557413,7344,976,16,PENDING_FOV},
    {119086906,8320,52,16,PENDING_TEXT},
    {665287983,8384,360,16,PENDING_QUADS},
    {4034438004,8752,40,16,PENDING_LISTS},
    {2052184297,8800,181,16,literal_pool_21},
};
int source_data_ptr_garage_pending_load(void){return source_data_pointers_load_file("source_data_ptr_garage_pending.jsd",sections,10u,8981u,0xd163e72cu,13u);}
#endif
