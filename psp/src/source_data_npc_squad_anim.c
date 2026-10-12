/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
typedef struct { signed char motion[4], frames[4]; } SquadAnim;

#include "source_data_sections.h"
SquadAnim SQUAD_ANIMS[4] __asm__("jgr_data_npc_squad_anim_SQUAD_ANIMS") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3135373645,48,32,16,SQUAD_ANIMS},
};
int source_data_npc_squad_anim_load(void){return source_data_sections_load("source_data_npc_squad_anim.jsd",sections,1u,80u,0xbcbe2dabu);}
#endif
