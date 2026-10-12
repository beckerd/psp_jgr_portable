/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {
    float min[3],max[3],vertex[4][3],normal[3],translation[3];
} StagePortal;

#include "source_data_sections.h"
StagePortal shibuya_portals[4] __asm__("jgr_data_shibuya_portals_shibuya_portals") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3236683313,48,384,16,shibuya_portals},
};
int source_data_shibuya_portals_load(void){return source_data_sections_load("source_data_shibuya_portals.jsd",sections,1u,432u,0x72ab93b4u);}
#endif
