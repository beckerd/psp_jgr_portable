/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
const char * HUD_NOTICE_TEXT[2] __asm__("jgr_ptr_hud_text_HUD_NOTICE_TEXT") __attribute__((aligned(16)));
char HUD_DIGITS[11][3] __asm__("jgr_ptr_hud_text_HUD_DIGITS") __attribute__((aligned(16)));
unsigned char literal_pool_9[26] __asm__("jgr_ptr_hud_text_literal_pool_9") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {2250244833,128,8,16,HUD_NOTICE_TEXT},
    {3701309101,144,33,16,HUD_DIGITS},
    {1490271400,192,26,16,literal_pool_9},
};
int source_data_ptr_hud_text_load(void){return source_data_pointers_load_file("source_data_ptr_hud_text.jsd",sections,3u,218u,0xce1280a1u,2u);}
#endif
