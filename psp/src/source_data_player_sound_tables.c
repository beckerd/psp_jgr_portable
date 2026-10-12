/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
unsigned char PLAYER_FOOT_CONTACT[5][28] __asm__("jgr_data_player_sound_tables_PLAYER_FOOT_CONTACT") __attribute__((aligned(16)));
signed char PLAYER_SURFACE_SOUND[64] __asm__("jgr_data_player_sound_tables_PLAYER_SURFACE_SOUND") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3400606610,64,140,16,PLAYER_FOOT_CONTACT},
    {1187477675,208,64,16,PLAYER_SURFACE_SOUND},
};
int source_data_player_sound_tables_load(void){return source_data_sections_load("source_data_player_sound_tables.jsd",sections,2u,272u,0x91be57ceu);}
#endif
