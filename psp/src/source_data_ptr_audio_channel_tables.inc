/* Payload-free external storage and reviewed dispatch code. */
#ifndef JGR_SOURCE_DATA_PTR_AUDIO_CHANNEL_TABLES_INC
#define JGR_SOURCE_DATA_PTR_AUDIO_CHANNEL_TABLES_INC
/* Original 0e7558 profile +4 channel classes. */
extern unsigned char AUDIO_CLASSES_0[16] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_0");
typedef char jgr_ptr_audio_channel_tables_AUDIO_CLASSES_0_abi[(sizeof(AUDIO_CLASSES_0)==16u)?1:-1];
extern unsigned char AUDIO_CLASSES_2[15] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_2");
typedef char jgr_ptr_audio_channel_tables_AUDIO_CLASSES_2_abi[(sizeof(AUDIO_CLASSES_2)==15u)?1:-1];
extern unsigned char AUDIO_CLASSES_4[19] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_4");
typedef char jgr_ptr_audio_channel_tables_AUDIO_CLASSES_4_abi[(sizeof(AUDIO_CLASSES_4)==19u)?1:-1];
extern unsigned char AUDIO_CLASSES_5[19] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_5");
typedef char jgr_ptr_audio_channel_tables_AUDIO_CLASSES_5_abi[(sizeof(AUDIO_CLASSES_5)==19u)?1:-1];
extern unsigned char AUDIO_CLASSES_6[16] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_6");
typedef char jgr_ptr_audio_channel_tables_AUDIO_CLASSES_6_abi[(sizeof(AUDIO_CLASSES_6)==16u)?1:-1];
extern unsigned char AUDIO_CLASSES_7[16] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_7");
typedef char jgr_ptr_audio_channel_tables_AUDIO_CLASSES_7_abi[(sizeof(AUDIO_CLASSES_7)==16u)?1:-1];
extern unsigned char AUDIO_CLASSES_16[49] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_16");
typedef char jgr_ptr_audio_channel_tables_AUDIO_CLASSES_16_abi[(sizeof(AUDIO_CLASSES_16)==49u)?1:-1];
extern unsigned char AUDIO_CLASSES_17[17] __asm__("jgr_ptr_audio_channel_tables_AUDIO_CLASSES_17");
typedef char jgr_ptr_audio_channel_tables_AUDIO_CLASSES_17_abi[(sizeof(AUDIO_CLASSES_17)==17u)?1:-1];
static int audio_source_class(unsigned bank,unsigned program)
{switch(bank){
case 0:return program<16 ? AUDIO_CLASSES_0[program]:-1;
case 2:return program<15 ? AUDIO_CLASSES_2[program]:-1;
case 4:return program<19 ? AUDIO_CLASSES_4[program]:-1;
case 5:return program<19 ? AUDIO_CLASSES_5[program]:-1;
case 6:return program<16 ? AUDIO_CLASSES_6[program]:-1;
case 7:return program<16 ? AUDIO_CLASSES_7[program]:-1;
case 16:return program<49 ? AUDIO_CLASSES_16[program]:-1;
case 17:return program<17 ? AUDIO_CLASSES_17[program]:-1;
default:return -1;}}

#endif
