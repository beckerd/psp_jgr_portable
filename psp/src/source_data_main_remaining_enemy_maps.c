/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
signed char binding_para[35] __asm__("jgr_data_main_remaining_enemy_maps_binding_para") __attribute__((aligned(16)));
signed char binding_patcar[16] __asm__("jgr_data_main_remaining_enemy_maps_binding_patcar") __attribute__((aligned(16)));
signed char binding_benz[15] __asm__("jgr_data_main_remaining_enemy_maps_binding_benz") __attribute__((aligned(16)));
signed char binding_heri[7] __asm__("jgr_data_main_remaining_enemy_maps_binding_heri") __attribute__((aligned(16)));
signed char binding_bheri[7] __asm__("jgr_data_main_remaining_enemy_maps_binding_bheri") __attribute__((aligned(16)));
signed char binding_rivals[3][3] __asm__("jgr_data_main_remaining_enemy_maps_binding_rivals") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2968795112,128,35,16,binding_para},
    {2344730532,176,16,16,binding_patcar},
    {570397841,192,15,16,binding_benz},
    {747333750,208,7,16,binding_heri},
    {1470355203,224,7,16,binding_bheri},
    {2640119632,240,9,16,binding_rivals},
};
int source_data_main_remaining_enemy_maps_load(void){return source_data_sections_load("source_data_main_remaining_enemy_maps.jsd",sections,6u,249u,0xf168c980u);}
#endif
