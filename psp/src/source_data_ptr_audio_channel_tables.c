/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
unsigned char AUDIO_CLASSES_0[16] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_0") __attribute__((aligned(16)));
unsigned char AUDIO_CLASSES_2[15] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_2") __attribute__((aligned(16)));
unsigned char AUDIO_CLASSES_4[19] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_4") __attribute__((aligned(16)));
unsigned char AUDIO_CLASSES_5[19] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_5") __attribute__((aligned(16)));
unsigned char AUDIO_CLASSES_6[16] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_6") __attribute__((aligned(16)));
unsigned char AUDIO_CLASSES_7[16] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_7") __attribute__((aligned(16)));
unsigned char AUDIO_CLASSES_16[49] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_16") __attribute__((aligned(16)));
unsigned char AUDIO_CLASSES_17[17] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_17") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {2653192505,176,16,16,AUDIO_CLASSES_0},
    {1881858069,192,15,16,AUDIO_CLASSES_2},
    {2571715872,208,19,16,AUDIO_CLASSES_4},
    {3998119350,240,19,16,AUDIO_CLASSES_5},
    {2001150988,272,16,16,AUDIO_CLASSES_6},
    {4199578,288,16,16,AUDIO_CLASSES_7},
    {1530456028,304,49,16,AUDIO_CLASSES_16},
    {742381386,368,17,16,AUDIO_CLASSES_17},
};
int source_data_ptr_audio_channel_tables_load(void){return source_data_pointers_load_file("source_data_ptr_audio_channel_tables.jsd",sections,8u,385u,0x13fe2bdeu,0u);}
#endif
