/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
float TABAN_ANCHORS[2][3] __asm__("jgr_ptr_taban_attack_source_TABAN_ANCHORS") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {4194403608,64,24,16,TABAN_ANCHORS},
};
int source_data_ptr_taban_attack_source_load(void){return source_data_pointers_load_file("source_data_ptr_taban_attack_source.jsd",sections,1u,88u,0x7665b086u,0u);}
#endif
