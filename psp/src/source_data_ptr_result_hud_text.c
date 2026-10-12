/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
const char * RESULT_TEXT[4] __asm__("jgr_ptr_result_hud_text_RESULT_TEXT") __attribute__((aligned(16)));
char RESULT_DIGITS[11][3] __asm__("jgr_ptr_result_hud_text_RESULT_DIGITS") __attribute__((aligned(16)));
unsigned RESULT_VOICE_DELAY[2][16] __asm__("jgr_ptr_result_hud_text_RESULT_VOICE_DELAY") __attribute__((aligned(16)));
const char * RESULT_NAMES[2][15] __asm__("jgr_ptr_result_hud_text_RESULT_NAMES") __attribute__((aligned(16)));
unsigned char literal_pool_9[283] __asm__("jgr_ptr_result_hud_text_literal_pool_9") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {3230977863,672,16,16,RESULT_TEXT},
    {53335270,688,33,16,RESULT_DIGITS},
    {1144604156,736,128,16,RESULT_VOICE_DELAY},
    {606008768,864,120,16,RESULT_NAMES},
    {1685063063,992,283,16,literal_pool_9},
};
int source_data_ptr_result_hud_text_load(void){return source_data_pointers_load_file("source_data_ptr_result_hud_text.jsd",sections,5u,1275u,0x1a8be5f7u,34u);}
#endif
