/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char groups[20] __asm__("jgr_data_mixed_traffic_contact_h__groups_0_groups") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3923628693,48,20,16,groups},
};
int source_data_mixed_traffic_contact_h__groups_0_load(void){return source_data_sections_load("source_data_mixed_traffic_contact_h__groups_0.jsd",sections,1u,68u,0xc2ef1ffeu);}
#endif
