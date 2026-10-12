/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
float PAR_TABAN[24] __asm__("jgr_ptr_taban_source_PAR_TABAN") __attribute__((aligned(16)));
signed char TABAN_MOTION[12] __asm__("jgr_ptr_taban_source_TABAN_MOTION") __attribute__((aligned(16)));
unsigned char TABAN_FRAMES[31] __asm__("jgr_ptr_taban_source_TABAN_FRAMES") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {1731931736,96,96,16,PAR_TABAN},
    {1440652588,192,12,16,TABAN_MOTION},
    {1582216702,208,31,16,TABAN_FRAMES},
};
int source_data_ptr_taban_source_load(void){return source_data_pointers_load_file("source_data_ptr_taban_source.jsd",sections,3u,239u,0x5f9bcacfu,0u);}
#endif
