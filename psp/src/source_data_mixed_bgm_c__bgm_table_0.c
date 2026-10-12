/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
typedef struct { int kind, n, loop; unsigned char seq[32]; } BgmEntry;

#include "source_data_sections.h"
BgmEntry BGM_TABLE[190] __asm__("jgr_data_mixed_bgm_c__bgm_table_0_BGM_TABLE") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {2843693663,48,8360,16,BGM_TABLE},
};
int source_data_mixed_bgm_c__bgm_table_0_load(void){return source_data_sections_load("source_data_mixed_bgm_c__bgm_table_0.jsd",sections,1u,8408u,0x5648762fu);}
#endif
