/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct {float delta[3],position[3];int32_t angles[3];} WaterReturn;
typedef struct {float point[6][3],normal[6][3];} WaterVolume;

#include "source_data_sections.h"
WaterReturn KOGANE_WATER_RETURN[3] __asm__("jgr_data_kogane_water_data_KOGANE_WATER_RETURN") __attribute__((aligned(16)));
WaterVolume KOGANE_WATER_VOLUME[10] __asm__("jgr_data_kogane_water_data_KOGANE_WATER_VOLUME") __attribute__((aligned(16)));
float KOGANE_WATER_HEIGHT[6] __asm__("jgr_data_kogane_water_data_KOGANE_WATER_HEIGHT") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {944662874,80,108,16,KOGANE_WATER_RETURN},
    {642423691,192,1440,16,KOGANE_WATER_VOLUME},
    {1788719962,1632,24,16,KOGANE_WATER_HEIGHT},
};
int source_data_kogane_water_data_load(void){return source_data_sections_load("source_data_kogane_water_data.jsd",sections,3u,1656u,0x9af2477fu);}
#endif
