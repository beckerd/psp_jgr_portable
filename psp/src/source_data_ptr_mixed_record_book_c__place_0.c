/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
const char * place[5] __asm__("jgr_ptr_mixed_record_book_c__place_0_place") __attribute__((aligned(16)));
unsigned char literal_pool_9[39] __asm__("jgr_ptr_mixed_record_book_c__place_0_literal_pool_9") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {1104996137,160,20,16,place},
    {1209147432,192,39,16,literal_pool_9},
};
int source_data_ptr_mixed_record_book_c__place_0_load(void){return source_data_pointers_load_file("source_data_ptr_mixed_record_book_c__place_0.jsd",sections,2u,231u,0x4334f19du,5u);}
#endif
