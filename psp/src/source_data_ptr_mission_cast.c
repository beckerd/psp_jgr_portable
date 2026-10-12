/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
unsigned long long MISSION_CAST[60] __asm__("jgr_ptr_mission_cast_MISSION_CAST") __attribute__((aligned(16)));
unsigned long long MISSION_CAST_VALID __asm__("jgr_ptr_mission_cast_MISSION_CAST_VALID") __attribute__((aligned(16)));
unsigned TITLE_CAST_MODEL __asm__("jgr_ptr_mission_cast_TITLE_CAST_MODEL") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {2104091248,96,480,16,MISSION_CAST},
    {2992292135,576,8,16,&MISSION_CAST_VALID},
    {2560604177,592,4,16,&TITLE_CAST_MODEL},
};
int source_data_ptr_mission_cast_load(void){return source_data_pointers_load_file("source_data_ptr_mission_cast.jsd",sections,3u,596u,0x00ef56a0u,0u);}
#endif
