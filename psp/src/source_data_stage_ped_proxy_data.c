/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {unsigned key,n_tex,n_obj,n_batch,n_vert,n_idx,kind;} StagePedProxyBank;
typedef struct {float low[3],high[3];unsigned model;} StagePedProxyBody;
typedef struct {
    float point[4][3],center[3],radius,inverse_area;
    unsigned short owner,count,first_batch,last_batch;
} StagePedProxyRoof;

#include "source_data_sections.h"
StagePedProxyRoof stage_ped_proxy_roofs[63] __asm__("jgr_data_stage_ped_proxy_data_stage_ped_proxy_roofs") __attribute__((aligned(16)));
StagePedProxyBody stage_ped_proxy_bodies[4] __asm__("jgr_data_stage_ped_proxy_data_stage_ped_proxy_bodies") __attribute__((aligned(16)));
StagePedProxyBank stage_ped_proxy_banks[2] __asm__("jgr_data_stage_ped_proxy_data_stage_ped_proxy_banks") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3791476944,80,4788,16,stage_ped_proxy_roofs},
    {1580177399,4880,112,16,stage_ped_proxy_bodies},
    {3584994331,4992,56,16,stage_ped_proxy_banks},
};
int source_data_stage_ped_proxy_data_load(void){return source_data_sections_load("source_data_stage_ped_proxy_data.jsd",sections,3u,5048u,0x112881fbu);}
#endif
