/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
float PAR_T2[24] __asm__("jgr_data_dog_source_PAR_T2") __attribute__((aligned(16)));
signed char DOG_MOTIONS[14] __asm__("jgr_data_dog_source_DOG_MOTIONS") __attribute__((aligned(16)));
unsigned char DOG_CLIP_FRAMES[13] __asm__("jgr_data_dog_source_DOG_CLIP_FRAMES") __attribute__((aligned(16)));
float DOG_CAPSULE[7] __asm__("jgr_data_dog_source_DOG_CAPSULE") __attribute__((aligned(16)));
float DOG_RELEASE[6] __asm__("jgr_data_dog_source_DOG_RELEASE") __attribute__((aligned(16)));
int DOG_ATTACK[3] __asm__("jgr_data_dog_source_DOG_ATTACK") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1272009413,128,96,16,PAR_T2},
    {1989086996,224,14,16,DOG_MOTIONS},
    {3847207569,240,13,16,DOG_CLIP_FRAMES},
    {30274935,256,28,16,DOG_CAPSULE},
    {1575114729,288,24,16,DOG_RELEASE},
    {3994712294,320,12,16,DOG_ATTACK},
};
int source_data_dog_source_load(void){return source_data_sections_load("source_data_dog_source.jsd",sections,6u,332u,0x9fe54dc6u);}
#endif
