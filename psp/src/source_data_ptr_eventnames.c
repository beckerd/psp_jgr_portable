/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
const char * EVENT_NAMES[112] __asm__("jgr_ptr_eventnames_EVENT_NAMES") __attribute__((aligned(16)));
unsigned char literal_pool_9[1344] __asm__("jgr_ptr_eventnames_literal_pool_9") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {1778616441,1872,448,16,EVENT_NAMES},
    {632910163,2320,1344,16,literal_pool_9},
};
int source_data_ptr_eventnames_load(void){return source_data_pointers_load_file("source_data_ptr_eventnames.jsd",sections,2u,3664u,0x6559b9fdu,112u);}
#endif
