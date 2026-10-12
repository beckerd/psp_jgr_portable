/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
float PAR_T7[24] __asm__("jgr_ptr_para_source_PAR_T7") __attribute__((aligned(16)));
signed char PARA_MOTION[25] __asm__("jgr_ptr_para_source_PARA_MOTION") __attribute__((aligned(16)));
int PARA_GAIT_MOTION __asm__("jgr_ptr_para_source_PARA_GAIT_MOTION") __attribute__((aligned(16)));
unsigned char PARA_FRAMES[30] __asm__("jgr_ptr_para_source_PARA_FRAMES") __attribute__((aligned(16)));
unsigned PARA_ATTACK[9] __asm__("jgr_ptr_para_source_PARA_ATTACK") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {1696483403,128,96,16,PAR_T7},
    {2132703284,224,25,16,PARA_MOTION},
    {570971039,256,4,16,&PARA_GAIT_MOTION},
    {1955483878,272,30,16,PARA_FRAMES},
    {3441489127,304,36,16,PARA_ATTACK},
};
int source_data_ptr_para_source_load(void){return source_data_pointers_load_file("source_data_ptr_para_source.jsd",sections,5u,340u,0x4cd70f6bu,0u);}
#endif
