/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
float SKATER_ARROW[16][6] __asm__("jgr_data_graffiti_arrow_skater_SKATER_ARROW") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1210103137,48,384,16,SKATER_ARROW},
};
int source_data_graffiti_arrow_skater_load(void){return source_data_sections_load("source_data_graffiti_arrow_skater.jsd",sections,1u,432u,0x51a36d49u);}
#endif
