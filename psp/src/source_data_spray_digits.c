/* Compiler-derived BSS ABI; contains no original values. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>


#include "source_data_sections.h"
float SPRAY_DIGITS[10][6] __asm__("jgr_data_spray_digits_SPRAY_DIGITS") __attribute__((aligned(16)));
static const SourceDataSection sections[]={
    {1171271344,48,240,16,SPRAY_DIGITS},
};
int source_data_spray_digits_load(void){return source_data_sections_load("source_data_spray_digits.jsd",sections,1u,288u,0x9b69b6bfu);}
#endif
