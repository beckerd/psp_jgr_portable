/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
int CUES[11][7] __asm__("jgr_data_spraytab_CUES") __attribute__((aligned(16)));
int PATROWS[21][4] __asm__("jgr_data_spraytab_PATROWS") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2007451895,64,308,16,CUES},
    {1758155231,384,336,16,PATROWS},
};
int source_data_spraytab_load(void){return source_data_sections_load("source_data_spraytab.jsd",sections,2u,720u,0xd8258cf6u);}
#endif
