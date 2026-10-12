/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
int STAFF_COMMANDS[31][3] __asm__("jgr_data_ending_tables_STAFF_COMMANDS") __attribute__((aligned(16)));
float STAFF_SLIDE_START[4] __asm__("jgr_data_ending_tables_STAFF_SLIDE_START") __attribute__((aligned(16)));
float STAFF_SLIDE_END[4] __asm__("jgr_data_ending_tables_STAFF_SLIDE_END") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {304285344,80,372,16,STAFF_COMMANDS},
    {862432433,464,16,16,STAFF_SLIDE_START},
    {1743997618,480,16,16,STAFF_SLIDE_END},
};
int source_data_ending_tables_load(void){return source_data_sections_load("source_data_ending_tables.jsd",sections,3u,496u,0xd03b5a7eu);}
#endif
