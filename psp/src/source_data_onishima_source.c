/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
float PAR_ONISHIMA[24] __asm__("jgr_data_onishima_source_PAR_ONISHIMA") __attribute__((aligned(16)));
unsigned char ONISHIMA_FRAMES[24] __asm__("jgr_data_onishima_source_ONISHIMA_FRAMES") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2115030895,64,96,16,PAR_ONISHIMA},
    {3816943283,160,24,16,ONISHIMA_FRAMES},
};
int source_data_onishima_source_load(void){return source_data_sections_load("source_data_onishima_source.jsd",sections,2u,184u,0x90fe0b2bu);}
#endif
