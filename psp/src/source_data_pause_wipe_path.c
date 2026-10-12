/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
float PAUSE_WIPE_PATH[32][2] __asm__("jgr_data_pause_wipe_path_PAUSE_WIPE_PATH") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2818116043,48,256,16,PAUSE_WIPE_PATH},
};
int source_data_pause_wipe_path_load(void){return source_data_sections_load("source_data_pause_wipe_path.jsd",sections,1u,304u,0xf9fbcdfbu);}
#endif
