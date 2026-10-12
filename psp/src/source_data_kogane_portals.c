/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {
    float min[3],max[3],vertex[4][3],normal[3],translation[3];
} StagePortal;

#include "source_data_sections.h"
StagePortal kogane_portals[14] __asm__("jgr_data_kogane_portals_kogane_portals") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1102176552,48,1344,16,kogane_portals},
};
int source_data_kogane_portals_load(void){return source_data_sections_load("source_data_kogane_portals.jsd",sections,1u,1392u,0xd7f14f08u);}
#endif
