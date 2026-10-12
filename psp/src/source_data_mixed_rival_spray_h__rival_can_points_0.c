/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
float RIVAL_CAN_POINTS[3][2][3] __asm__("jgr_data_mixed_rival_spray_h__rival_can_points_0_RIVAL_CAN_POINTS") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {422565846,48,72,16,RIVAL_CAN_POINTS},
};
int source_data_mixed_rival_spray_h__rival_can_points_0_load(void){return source_data_sections_load("source_data_mixed_rival_spray_h__rival_can_points_0.jsd",sections,1u,120u,0x66bbc46du);}
#endif
