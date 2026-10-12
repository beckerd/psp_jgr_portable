/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
uint64_t street_prop_sound_masks[7] __asm__("jgr_ptr_street_prop_audio_street_prop_sound_masks") __attribute__((aligned(16)));
unsigned street_prop_sequence_masks[7] __asm__("jgr_ptr_street_prop_audio_street_prop_sequence_masks") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {2374353323,80,56,16,street_prop_sound_masks},
    {4195387132,144,28,16,street_prop_sequence_masks},
};
int source_data_ptr_street_prop_audio_load(void){return source_data_pointers_load_file("source_data_ptr_street_prop_audio.jsd",sections,2u,172u,0xab333916u,0u);}
#endif
