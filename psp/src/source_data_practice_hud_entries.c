/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {int model,selected,intro;float x,y,z,scale;} PracticeEntry;

#include "source_data_sections.h"
PracticeEntry PRACTICE_ENTRIES[3] __asm__("jgr_data_practice_hud_entries_PRACTICE_ENTRIES") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1507350146,48,84,16,PRACTICE_ENTRIES},
};
int source_data_practice_hud_entries_load(void){return source_data_sections_load("source_data_practice_hud_entries.jsd",sections,1u,132u,0x17e72875u);}
#endif
