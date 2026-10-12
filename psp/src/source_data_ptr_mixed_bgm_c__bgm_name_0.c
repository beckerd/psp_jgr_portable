/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
const char * BGM_NAME[211] __asm__("jgr_ptr_mixed_bgm_c__bgm_name_0_BGM_NAME") __attribute__((aligned(16)));
unsigned char literal_pool_9[2064] __asm__("jgr_ptr_mixed_bgm_c__bgm_name_0_literal_pool_9") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {4151398497,3456,844,16,BGM_NAME},
    {1945228713,4304,2064,16,literal_pool_9},
};
int source_data_ptr_mixed_bgm_c__bgm_name_0_load(void){return source_data_pointers_load_file("source_data_ptr_mixed_bgm_c__bgm_name_0.jsd",sections,2u,6368u,0xe753111du,211u);}
#endif
