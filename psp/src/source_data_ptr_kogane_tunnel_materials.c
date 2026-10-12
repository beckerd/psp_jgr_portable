/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>


#define KOGANE_TUNNEL_STAGE_KEY 0x3058a56eu
#include "source_data_pointers.h"
unsigned short kogane_tunnel_opaque[26] __asm__("jgr_ptr_kogane_tunnel_materials_kogane_tunnel_opaque") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {4045019070,64,52,16,kogane_tunnel_opaque},
};
int source_data_ptr_kogane_tunnel_materials_load(void){return source_data_pointers_load_file("source_data_ptr_kogane_tunnel_materials.jsd",sections,1u,116u,0xaccf5eb5u,0u);}
#endif
