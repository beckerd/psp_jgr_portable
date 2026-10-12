/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
unsigned char MANATEE_TL[256] __asm__("jgr_data_audio_spatial_tables_MANATEE_TL") __attribute__((aligned(16)));
unsigned char MANATEE_SEND[64] __asm__("jgr_data_audio_spatial_tables_MANATEE_SEND") __attribute__((aligned(16)));
unsigned char MANATEE_PAN[128] __asm__("jgr_data_audio_spatial_tables_MANATEE_PAN") __attribute__((aligned(16)));
unsigned char MANATEE_PAN_BASE[32] __asm__("jgr_data_audio_spatial_tables_MANATEE_PAN_BASE") __attribute__((aligned(16)));
unsigned short AICA_TL_GAIN[256] __asm__("jgr_data_audio_spatial_tables_AICA_TL_GAIN") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2130127614,112,256,16,MANATEE_TL},
    {2635107801,368,64,16,MANATEE_SEND},
    {2881125818,432,128,16,MANATEE_PAN},
    {3978040798,560,32,16,MANATEE_PAN_BASE},
    {453863300,592,512,16,AICA_TL_GAIN},
};
int source_data_audio_spatial_tables_load(void){return source_data_sections_load("source_data_audio_spatial_tables.jsd",sections,5u,1104u,0x0e7f5dd7u);}
#endif
