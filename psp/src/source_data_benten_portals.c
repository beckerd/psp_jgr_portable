/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {
    float min[3],max[3],vertex[4][3],normal[3],translation[3];
} StagePortal;

#include "source_data_sections.h"
StagePortal benten_portals[4] __asm__("jgr_data_benten_portals_benten_portals") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1983535008,48,384,16,benten_portals},
};
int source_data_benten_portals_load(void){return source_data_sections_load("source_data_benten_portals.jsd",sections,1u,432u,0x606de477u);}
#endif
