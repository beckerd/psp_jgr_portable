/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
struct { unsigned root, texlist; } INTRO7_MODELS[9] __asm__("jgr_ptr_story_intro_tables_INTRO7_MODELS") __attribute__((aligned(16)));
unsigned INTRO7_MOTIONS[2] __asm__("jgr_ptr_story_intro_tables_INTRO7_MOTIONS") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {148579665,80,72,16,INTRO7_MODELS},
    {3944263675,160,8,16,INTRO7_MOTIONS},
};
int source_data_ptr_story_intro_tables_load(void){return source_data_pointers_load_file("source_data_ptr_story_intro_tables.jsd",sections,2u,168u,0xf7b11308u,0u);}
#endif
