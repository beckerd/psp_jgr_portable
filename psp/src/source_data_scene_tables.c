/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
short SCENE_CLIP[170] __asm__("jgr_data_scene_tables_SCENE_CLIP") __attribute__((aligned(16)));
unsigned short SCENE_FRAMES[16][176] __asm__("jgr_data_scene_tables_SCENE_FRAMES") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2418746003,64,340,16,SCENE_CLIP},
    {4025352571,416,5632,16,SCENE_FRAMES},
};
int source_data_scene_tables_load(void){return source_data_sections_load("source_data_scene_tables.jsd",sections,2u,6048u,0x71efee68u);}
#endif
