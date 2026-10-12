/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
int TRICK_POINTS[20] __asm__("jgr_ptr_trick_hud_tables_TRICK_POINTS") __attribute__((aligned(16)));
int TRICK_BONUS[16] __asm__("jgr_ptr_trick_hud_tables_TRICK_BONUS") __attribute__((aligned(16)));
const char * TRICK_NAMES[7][30] __asm__("jgr_ptr_trick_hud_tables_TRICK_NAMES") __attribute__((aligned(16)));
char TRICK_SUFFIX[16] __asm__("jgr_ptr_trick_hud_tables_TRICK_SUFFIX") __attribute__((aligned(16)));
unsigned char TRICK_GRIND_IDS[16][2][6] __asm__("jgr_ptr_trick_hud_tables_TRICK_GRIND_IDS") __attribute__((aligned(16)));
unsigned char TRICK_AIR_IDS[16][2][4] __asm__("jgr_ptr_trick_hud_tables_TRICK_AIR_IDS") __attribute__((aligned(16)));
unsigned char literal_pool_13[826] __asm__("jgr_ptr_trick_hud_tables_literal_pool_13") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {1804468083,1648,80,16,TRICK_POINTS},
    {3254352763,1728,64,16,TRICK_BONUS},
    {2891074881,1792,840,16,TRICK_NAMES},
    {4186437252,2640,16,16,TRICK_SUFFIX},
    {3050181351,2656,192,16,TRICK_GRIND_IDS},
    {270472193,2848,128,16,TRICK_AIR_IDS},
    {4181619069,2976,826,16,literal_pool_13},
};
int source_data_ptr_trick_hud_tables_load(void){return source_data_pointers_load_file("source_data_ptr_trick_hud_tables.jsd",sections,7u,3802u,0xf9e1a12au,93u);}
#endif
