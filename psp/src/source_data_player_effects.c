/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct { int bone; float pos[3]; } SkateAnchor;

#include "source_data_sections.h"
SkateAnchor SKATE_ANCHORS[16][29] __asm__("jgr_data_player_effects_SKATE_ANCHORS") __attribute__((aligned(16)));
unsigned char SKATE_GRIND_MASK[16][2][6] __asm__("jgr_data_player_effects_SKATE_GRIND_MASK") __attribute__((aligned(16)));
unsigned char SKATE_DASH_CONTACT[2][28] __asm__("jgr_data_player_effects_SKATE_DASH_CONTACT") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2783691792,80,7424,16,SKATE_ANCHORS},
    {3984776715,7504,192,16,SKATE_GRIND_MASK},
    {2843678893,7696,56,16,SKATE_DASH_CONTACT},
};
int source_data_player_effects_load(void){return source_data_sections_load("source_data_player_effects.jsd",sections,3u,7752u,0x9b55c53cu);}
#endif
