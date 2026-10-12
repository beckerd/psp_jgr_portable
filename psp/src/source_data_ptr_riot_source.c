/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
float PAR_T4[24] __asm__("jgr_ptr_riot_source_PAR_T4") __attribute__((aligned(16)));
signed char RIOT_MOTION[23] __asm__("jgr_ptr_riot_source_RIOT_MOTION") __attribute__((aligned(16)));
int RIOT_GAIT_MOTION __asm__("jgr_ptr_riot_source_RIOT_GAIT_MOTION") __attribute__((aligned(16)));
int RIOT_FRONT_GRAB_MOTION __asm__("jgr_ptr_riot_source_RIOT_FRONT_GRAB_MOTION") __attribute__((aligned(16)));
signed char RIOT_GAS_OVERLAY[3] __asm__("jgr_ptr_riot_source_RIOT_GAS_OVERLAY") __attribute__((aligned(16)));
signed char RIOT_TURN_OVERLAY[2] __asm__("jgr_ptr_riot_source_RIOT_TURN_OVERLAY") __attribute__((aligned(16)));
unsigned char RIOT_FRAMES[36] __asm__("jgr_ptr_riot_source_RIOT_FRAMES") __attribute__((aligned(16)));
unsigned RIOT_ATTACK[2] __asm__("jgr_ptr_riot_source_RIOT_ATTACK") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {2270912751,176,96,16,PAR_T4},
    {568133892,272,23,16,RIOT_MOTION},
    {3883627555,304,4,16,&RIOT_GAIT_MOTION},
    {394967066,320,4,16,&RIOT_FRONT_GRAB_MOTION},
    {4184925139,336,3,16,RIOT_GAS_OVERLAY},
    {222083118,352,2,16,RIOT_TURN_OVERLAY},
    {709698006,368,36,16,RIOT_FRAMES},
    {2481161687,416,8,16,RIOT_ATTACK},
};
int source_data_ptr_riot_source_load(void){return source_data_pointers_load_file("source_data_ptr_riot_source.jsd",sections,8u,424u,0xd954867fu,0u);}
#endif
