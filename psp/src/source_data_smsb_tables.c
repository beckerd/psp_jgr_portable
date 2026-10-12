/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
unsigned char SMSB_NOTE[228] __asm__("jgr_data_smsb_tables_SMSB_NOTE") __attribute__((aligned(16)));
unsigned char SMSB_PITCH[1536] __asm__("jgr_data_smsb_tables_SMSB_PITCH") __attribute__((aligned(16)));
uint32_t SMSB_EG_ATTACK[64] __asm__("jgr_data_smsb_tables_SMSB_EG_ATTACK") __attribute__((aligned(16)));
uint32_t SMSB_EG_DSR[64] __asm__("jgr_data_smsb_tables_SMSB_EG_DSR") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2505759850,96,228,16,SMSB_NOTE},
    {2870355212,336,1536,16,SMSB_PITCH},
    {518474426,1872,256,16,SMSB_EG_ATTACK},
    {3374969356,2128,256,16,SMSB_EG_DSR},
};
int source_data_smsb_tables_load(void){return source_data_sections_load("source_data_smsb_tables.jsd",sections,4u,2384u,0x046da206u);}
#endif
