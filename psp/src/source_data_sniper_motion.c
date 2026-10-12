/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
signed char primary[2][4] __asm__("jgr_data_sniper_motion_primary") __attribute__((aligned(16)));
signed char turn[2] __asm__("jgr_data_sniper_motion_turn") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1565769167,64,8,16,primary},
    {3705213151,80,2,16,turn},
};
int source_data_sniper_motion_load(void){return source_data_sections_load("source_data_sniper_motion.jsd",sections,2u,82u,0x384b07ccu);}
#endif
