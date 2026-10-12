/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char scene_dialogue_modes[112] __asm__("jgr_data_scene_dialogue_data_scene_dialogue_modes") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2287285908,48,112,16,scene_dialogue_modes},
};
int source_data_scene_dialogue_data_load(void){return source_data_sections_load("source_data_scene_dialogue_data.jsd",sections,1u,160u,0xddac021au);}
#endif
