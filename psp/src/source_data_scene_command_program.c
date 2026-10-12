/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>

#include "source_data_sections.h"
int program[8] __asm__("jgr_data_scene_command_program_program") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {3578617535,48,32,16,program},
};
int source_data_scene_command_program_load(void){return source_data_sections_load("source_data_scene_command_program.jsd",sections,1u,80u,0x53945d2du);}
#endif
