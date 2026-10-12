/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
float PAR_T3[24] __asm__("jgr_ptr_killer_source_PAR_T3") __attribute__((aligned(16)));
signed char KILLER_MOTION[20] __asm__("jgr_ptr_killer_source_KILLER_MOTION") __attribute__((aligned(16)));
int KILLER_GAIT_MOTION __asm__("jgr_ptr_killer_source_KILLER_GAIT_MOTION") __attribute__((aligned(16)));
int KILLER_FRONT_GRAB_MOTION __asm__("jgr_ptr_killer_source_KILLER_FRONT_GRAB_MOTION") __attribute__((aligned(16)));
unsigned char KILLER_FRAMES[35] __asm__("jgr_ptr_killer_source_KILLER_FRAMES") __attribute__((aligned(16)));
unsigned KILLER_ATTACK[3] __asm__("jgr_ptr_killer_source_KILLER_ATTACK") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {3424715211,144,96,16,PAR_T3},
    {4217930373,240,20,16,KILLER_MOTION},
    {3924282709,272,4,16,&KILLER_GAIT_MOTION},
    {707678133,288,4,16,&KILLER_FRONT_GRAB_MOTION},
    {4042828375,304,35,16,KILLER_FRAMES},
    {1230376534,352,12,16,KILLER_ATTACK},
};
int source_data_ptr_killer_source_load(void){return source_data_pointers_load_file("source_data_ptr_killer_source.jsd",sections,6u,364u,0xda1f40bdu,0u);}
#endif
