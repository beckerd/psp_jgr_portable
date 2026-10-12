/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct { float v[9]; unsigned short surf, mask, dbl, src; } ColTri;

#include "source_data_sections.h"
float cover_position[3] __asm__("jgr_data_kogane_cover_data_cover_position") __attribute__((aligned(16)));
float cover_lights[114][8] __asm__("jgr_data_kogane_cover_data_cover_lights") __attribute__((aligned(16)));
ColTri cover_closed[8] __asm__("jgr_data_kogane_cover_data_cover_closed") __attribute__((aligned(16)));
ColTri cover_open[108] __asm__("jgr_data_kogane_cover_data_cover_open") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {506511346,96,12,16,cover_position},
    {2658669931,112,3648,16,cover_lights},
    {2718165607,3760,352,16,cover_closed},
    {1398637804,4112,4752,16,cover_open},
};
int source_data_kogane_cover_data_load(void){return source_data_sections_load("source_data_kogane_cover_data.jsd",sections,4u,8864u,0xb23d1754u);}
#endif
