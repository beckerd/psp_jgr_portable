/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
float STAFF_XY[4][3][8] __asm__("jgr_data_staff_draw_STAFF_XY") __attribute__((aligned(16)));
unsigned short STAFF_UV_PATTERN_X[4][8] __asm__("jgr_data_staff_draw_STAFF_UV_PATTERN_X") __attribute__((aligned(16)));
unsigned short STAFF_UV_PATTERN_Y[4][8] __asm__("jgr_data_staff_draw_STAFF_UV_PATTERN_Y") __attribute__((aligned(16)));
unsigned short STAFF_UV_PAGE_X[2][8] __asm__("jgr_data_staff_draw_STAFF_UV_PAGE_X") __attribute__((aligned(16)));
unsigned short STAFF_UV_PAGE_Y[2][8] __asm__("jgr_data_staff_draw_STAFF_UV_PAGE_Y") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {887755747,112,384,16,STAFF_XY},
    {2768641138,496,64,16,STAFF_UV_PATTERN_X},
    {3523292388,560,64,16,STAFF_UV_PATTERN_Y},
    {2342846736,624,32,16,STAFF_UV_PAGE_X},
    {4238487942,656,32,16,STAFF_UV_PAGE_Y},
};
int source_data_staff_draw_load(void){return source_data_sections_load("source_data_staff_draw.jsd",sections,5u,688u,0x064275d1u);}
#endif
