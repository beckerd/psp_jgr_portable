/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {
    unsigned flags;
    float vertex[4][3], normal[3], center[3], radius_sq;
} MissionQuad;
typedef struct {
    MissionQuad quad;
    int mission_id;
    unsigned direction,index;
} StageExitBoundary;

#include "source_data_sections.h"
StageExitBoundary benten_exits[19] __asm__("jgr_data_stage_exit_data_benten_exits") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1969218912,48,1748,16,benten_exits},
};
int source_data_stage_exit_data_load(void){return source_data_sections_load("source_data_stage_exit_data.jsd",sections,1u,1796u,0x993751bau);}
#endif
