/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {float x,y,z,scale;int models[6];} PauseMapEntry;


#include "source_data_pointers.h"
PauseMapEntry PAUSE_MAPS[6][6] __asm__("jgr_ptr_pause_map_entries_PAUSE_MAPS") __attribute__((aligned(16)));
unsigned PAUSE_MAP_COUNTS[6] __asm__("jgr_ptr_pause_map_entries_PAUSE_MAP_COUNTS") __attribute__((aligned(16)));
int PAUSE_MAP_ICONS[9] __asm__("jgr_ptr_pause_map_entries_PAUSE_MAP_ICONS") __attribute__((aligned(16)));
const char * PAUSE_MAP_TEXT[4] __asm__("jgr_ptr_pause_map_entries_PAUSE_MAP_TEXT") __attribute__((aligned(16)));
char PAUSE_MAP_DIGITS[11][3] __asm__("jgr_ptr_pause_map_entries_PAUSE_MAP_DIGITS") __attribute__((aligned(16)));
float PAUSE_MAP_TEXT_XY[4] __asm__("jgr_ptr_pause_map_entries_PAUSE_MAP_TEXT_XY") __attribute__((aligned(16)));
unsigned char literal_pool_15[63] __asm__("jgr_ptr_pause_map_entries_literal_pool_15") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {266318729,224,1440,16,PAUSE_MAPS},
    {2497471215,1664,24,16,PAUSE_MAP_COUNTS},
    {947916768,1696,36,16,PAUSE_MAP_ICONS},
    {163109579,1744,16,16,PAUSE_MAP_TEXT},
    {4190514790,1760,33,16,PAUSE_MAP_DIGITS},
    {3689144592,1808,16,16,PAUSE_MAP_TEXT_XY},
    {1015308443,1824,63,16,literal_pool_15},
};
int source_data_ptr_pause_map_entries_load(void){return source_data_pointers_load_file("source_data_ptr_pause_map_entries.jsd",sections,7u,1887u,0xca56cbf8u,4u);}
#endif
