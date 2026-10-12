/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float SH4_SIN[32768] __asm__("jgr_data_player_fsca_SH4_SIN") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3791995794,48,131072,16,SH4_SIN},
};
int source_data_player_fsca_load(void){return source_data_sections_load("source_data_player_fsca.jsd",sections,1u,131120u,0x49d948a7u);}
#endif
