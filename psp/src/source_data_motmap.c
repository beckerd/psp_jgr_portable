/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
short MOTMAP[112] __asm__("jgr_data_motmap_MOTMAP") __attribute__((aligned(16)));
short MOTMAP_ALT[112] __asm__("jgr_data_motmap_MOTMAP_ALT") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2138280852,64,224,16,MOTMAP},
    {4201088057,288,224,16,MOTMAP_ALT},
};
int source_data_motmap_load(void){return source_data_sections_load("source_data_motmap.jsd",sections,2u,512u,0xf836c8afu);}
#endif
