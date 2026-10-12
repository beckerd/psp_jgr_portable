/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char stage[10] __asm__("jgr_data_mixed_garage_pending_h__stage_0_stage") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1552568500,48,10,16,stage},
};
int source_data_mixed_garage_pending_h__stage_0_load(void){return source_data_sections_load("source_data_mixed_garage_pending_h__stage_0.jsd",sections,1u,58u,0x0c728d57u);}
#endif
