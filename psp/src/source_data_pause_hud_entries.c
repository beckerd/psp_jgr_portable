/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {int model,type,selected,intro;float x,y,z,scale;} PauseEntry;

#include "source_data_sections.h"
PauseEntry PAUSE_ENTRIES[6][7] __asm__("jgr_data_pause_hud_entries_PAUSE_ENTRIES") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {251062066,48,1344,16,PAUSE_ENTRIES},
};
int source_data_pause_hud_entries_load(void){return source_data_sections_load("source_data_pause_hud_entries.jsd",sections,1u,1392u,0x591606e7u);}
#endif
