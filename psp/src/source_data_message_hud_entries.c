/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {unsigned flags,color;float x,y,w,h,u0,v0,u1,v1;} MessageEntry;

#include "source_data_sections.h"
MessageEntry MESSAGE_ENTRIES[50] __asm__("jgr_data_message_hud_entries_MESSAGE_ENTRIES") __attribute__((aligned(16)));
unsigned MESSAGE_RANGES[4] __asm__("jgr_data_message_hud_entries_MESSAGE_RANGES") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3978227391,64,2000,16,MESSAGE_ENTRIES},
    {1219257914,2064,16,16,MESSAGE_RANGES},
};
int source_data_message_hud_entries_load(void){return source_data_sections_load("source_data_message_hud_entries.jsd",sections,2u,2080u,0x4d4adb2bu);}
#endif
