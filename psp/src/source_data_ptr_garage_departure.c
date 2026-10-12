/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {float point[6];int roll,fov;} GarageDepartureKey;


#include "source_data_pointers.h"
GarageDepartureKey DEPART_CAMERA_0[137] __asm__("jgr_ptr_garage_departure_DEPART_CAMERA_0") __attribute__((aligned(16)));
GarageDepartureKey DEPART_CAMERA_1[160] __asm__("jgr_ptr_garage_departure_DEPART_CAMERA_1") __attribute__((aligned(16)));
GarageDepartureKey DEPART_CAMERA_2[160] __asm__("jgr_ptr_garage_departure_DEPART_CAMERA_2") __attribute__((aligned(16)));
GarageDepartureKey DEPART_CAMERA_3[160] __asm__("jgr_ptr_garage_departure_DEPART_CAMERA_3") __attribute__((aligned(16)));
GarageDepartureKey DEPART_CAMERA_4[150] __asm__("jgr_ptr_garage_departure_DEPART_CAMERA_4") __attribute__((aligned(16)));
GarageDepartureKey DEPART_CAMERA_5[160] __asm__("jgr_ptr_garage_departure_DEPART_CAMERA_5") __attribute__((aligned(16)));
GarageDepartureKey DEPART_CAMERA_6[160] __asm__("jgr_ptr_garage_departure_DEPART_CAMERA_6") __attribute__((aligned(16)));
GarageDepartureKey DEPART_CAMERA_7[150] __asm__("jgr_ptr_garage_departure_DEPART_CAMERA_7") __attribute__((aligned(16)));
GarageDepartureKey DEPART_CAMERA_8[160] __asm__("jgr_ptr_garage_departure_DEPART_CAMERA_8") __attribute__((aligned(16)));
GarageDepartureKey DEPART_CAMERA_9[150] __asm__("jgr_ptr_garage_departure_DEPART_CAMERA_9") __attribute__((aligned(16)));
GarageDepartureKey DEPART_CAMERA_14[140] __asm__("jgr_ptr_garage_departure_DEPART_CAMERA_14") __attribute__((aligned(16)));
unsigned DEPART_FRAMES[15] __asm__("jgr_ptr_garage_departure_DEPART_FRAMES") __attribute__((aligned(16)));
unsigned DEPART_CAMERA_FRAMES[15] __asm__("jgr_ptr_garage_departure_DEPART_CAMERA_FRAMES") __attribute__((aligned(16)));
const GarageDepartureKey * DEPART_CAMERAS[15] __asm__("jgr_ptr_garage_departure_DEPART_CAMERAS") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {1242343613,448,4384,16,DEPART_CAMERA_0},
    {1024170027,4832,5120,16,DEPART_CAMERA_1},
    {2751646097,9952,5120,16,DEPART_CAMERA_2},
    {3540384007,15072,5120,16,DEPART_CAMERA_3},
    {1298230436,20192,4800,16,DEPART_CAMERA_4},
    {979786802,24992,5120,16,DEPART_CAMERA_5},
    {2741964168,30112,5120,16,DEPART_CAMERA_6},
    {3563601182,35232,4800,16,DEPART_CAMERA_7},
    {1154949263,40032,5120,16,DEPART_CAMERA_8},
    {869273625,45152,4800,16,DEPART_CAMERA_9},
    {1597499872,49952,4480,16,DEPART_CAMERA_14},
    {2073668200,54432,60,16,DEPART_FRAMES},
    {503888907,54496,60,16,DEPART_CAMERA_FRAMES},
    {3587293273,54560,60,16,DEPART_CAMERAS},
};
int source_data_ptr_garage_departure_load(void){return source_data_pointers_load_file("source_data_ptr_garage_departure.jsd",sections,14u,54620u,0xae14ee27u,11u);}
#endif
