/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {int slot;float x,y,w,h,u0,v0,u1,v1;} HudEntry;

#include "source_data_sections.h"
HudEntry TRICK_MULTIPLY[1] __asm__("jgr_data_trick_hud_entries_TRICK_MULTIPLY") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {460329643,48,36,16,TRICK_MULTIPLY},
};
int source_data_trick_hud_entries_load(void){return source_data_sections_load("source_data_trick_hud_entries.jsd",sections,1u,84u,0x91eb7fb4u);}
#endif
