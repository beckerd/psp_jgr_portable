/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
float GANG_PAR_A[24] __asm__("jgr_ptr_bomb_source_GANG_PAR_A") __attribute__((aligned(16)));
unsigned BOMB_ATTACK_A[3] __asm__("jgr_ptr_bomb_source_BOMB_ATTACK_A") __attribute__((aligned(16)));
float GANG_PAR_B[24] __asm__("jgr_ptr_bomb_source_GANG_PAR_B") __attribute__((aligned(16)));
unsigned BOMB_ATTACK_B[3] __asm__("jgr_ptr_bomb_source_BOMB_ATTACK_B") __attribute__((aligned(16)));
signed char BOMB_MOTION[22] __asm__("jgr_ptr_bomb_source_BOMB_MOTION") __attribute__((aligned(16)));
int BOMB_GAIT_MOTION __asm__("jgr_ptr_bomb_source_BOMB_GAIT_MOTION") __attribute__((aligned(16)));
int BOMB_FRONT_GRAB_MOTION __asm__("jgr_ptr_bomb_source_BOMB_FRONT_GRAB_MOTION") __attribute__((aligned(16)));
unsigned char BOMB_FRAMES[26] __asm__("jgr_ptr_bomb_source_BOMB_FRAMES") __attribute__((aligned(16)));
unsigned BOMB_BLAST_FRAMES __asm__("jgr_ptr_bomb_source_BOMB_BLAST_FRAMES") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {449603157,192,96,16,GANG_PAR_A},
    {3654707380,288,12,16,BOMB_ATTACK_A},
    {2210740207,304,96,16,GANG_PAR_B},
    {1088370958,400,12,16,BOMB_ATTACK_B},
    {3853862627,416,22,16,BOMB_MOTION},
    {1470803797,448,4,16,&BOMB_GAIT_MOTION},
    {1868707938,464,4,16,&BOMB_FRONT_GRAB_MOTION},
    {3995430449,480,26,16,BOMB_FRAMES},
    {1058531246,512,4,16,&BOMB_BLAST_FRAMES},
};
int source_data_ptr_bomb_source_load(void){return source_data_pointers_load_file("source_data_ptr_bomb_source.jsd",sections,9u,516u,0x735582e6u,0u);}
#endif
