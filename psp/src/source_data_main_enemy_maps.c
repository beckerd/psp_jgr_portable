/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
signed char keikan[37] __asm__("jgr_data_main_enemy_maps_keikan") __attribute__((aligned(16)));
signed char kidoutai[36] __asm__("jgr_data_main_enemy_maps_kidoutai") __attribute__((aligned(16)));
signed char inu[14] __asm__("jgr_data_main_enemy_maps_inu") __attribute__((aligned(16)));
signed char killer[33] __asm__("jgr_data_main_enemy_maps_killer") __attribute__((aligned(16)));
signed char bomb[26] __asm__("jgr_data_main_enemy_maps_bomb") __attribute__((aligned(16)));
signed char kairiki[29] __asm__("jgr_data_main_enemy_maps_kairiki") __attribute__((aligned(16)));
signed char fire[35] __asm__("jgr_data_main_enemy_maps_fire") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2685170066,144,37,16,keikan},
    {760259039,192,36,16,kidoutai},
    {2565016653,240,14,16,inu},
    {654369647,256,33,16,killer},
    {764626103,304,26,16,bomb},
    {1026111382,336,29,16,kairiki},
    {4114756719,368,35,16,fire},
};
int source_data_main_enemy_maps_load(void){return source_data_sections_load("source_data_main_enemy_maps.jsd",sections,7u,403u,0x5c64d859u);}
#endif
