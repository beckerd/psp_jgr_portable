/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
unsigned char POLICE_CLIP_FRAMES[41] __asm__("jgr_data_mixed_police_member_inc__police_clip_frames_0_POLICE_CLIP_FRAMES") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1226797834,48,41,16,POLICE_CLIP_FRAMES},
};
int source_data_mixed_police_member_inc__police_clip_frames_0_load(void){return source_data_sections_load("source_data_mixed_police_member_inc__police_clip_frames_0.jsd",sections,1u,89u,0xa71e4a93u);}
#endif
