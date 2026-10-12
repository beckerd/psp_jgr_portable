/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {int slot;float x,y,w,h,u0,v0,u1,v1;} HudEntry;

#include "source_data_sections.h"
HudEntry HUD_ENTRIES[37] __asm__("jgr_data_game_hud_entries_HUD_ENTRIES") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3928178989,48,1332,16,HUD_ENTRIES},
};
int source_data_game_hud_entries_load(void){return source_data_sections_load("source_data_game_hud_entries.jsd",sections,1u,1380u,0xb3820533u);}
#endif
