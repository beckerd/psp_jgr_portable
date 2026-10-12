/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
struct {int bone;float pos[3];} ENEMY_HARDPOINTS[17] __asm__("jgr_ptr_enemy_hardpoints_ENEMY_HARDPOINTS") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {3265324741,64,272,16,ENEMY_HARDPOINTS},
};
int source_data_ptr_enemy_hardpoints_load(void){return source_data_pointers_load_file("source_data_ptr_enemy_hardpoints.jsd",sections,1u,336u,0x4338a0dau,0u);}
#endif
