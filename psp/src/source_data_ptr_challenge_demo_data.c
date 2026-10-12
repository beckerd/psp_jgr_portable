/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {unsigned short tick;unsigned char kind;signed char program;} RecruitAudioCue;
typedef struct {
    unsigned short tick,ref;
    unsigned char active,variant;
    float frame,target[3];
} RecruitSprayCue;
typedef struct {
    const char *mission,*path,*label;
    unsigned set,character;
    const RecruitAudioCue *cues;
    unsigned cue_count;
    const RecruitSprayCue *sprays;
    unsigned spray_count;
} RecruitDemo;


#include "source_data_pointers.h"
RecruitAudioCue CHALLENGE_S1M11_0[38] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M11_0") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S1M11_0_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M11_0_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S1M11_1[12] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M11_1") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S1M11_1_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M11_1_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S1M11_2[55] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M11_2") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S1M11_2_SPRAY[9] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M11_2_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S1M11_3[29] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M11_3") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S1M11_3_SPRAY[3] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M11_3_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S1M11_4[25] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M11_4") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S1M11_4_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M11_4_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S1M11_5[78] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M11_5") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S1M11_5_SPRAY[3] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M11_5_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S1M06_0[18] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M06_0") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S1M06_0_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M06_0_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S1M06_1[45] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M06_1") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S1M06_1_SPRAY[7] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M06_1_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S1M06_2[13] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M06_2") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S1M06_2_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M06_2_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S1M07_0[15] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M07_0") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S1M07_0_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S1M07_0_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S2M05_0[11] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S2M05_0") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S2M05_0_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S2M05_0_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S2M05_1[13] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S2M05_1") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S2M05_1_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S2M05_1_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S2M05_2[21] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S2M05_2") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S2M05_2_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S2M05_2_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S3M06_0[13] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S3M06_0") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S3M06_0_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S3M06_0_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S3M06_1[34] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S3M06_1") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S3M06_1_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S3M06_1_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S3M06_2[20] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S3M06_2") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S3M06_2_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S3M06_2_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S3M11_0[17] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S3M11_0") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S3M11_0_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S3M11_0_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S3M11_1[23] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S3M11_1") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S3M11_1_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S3M11_1_SPRAY") __attribute__((aligned(16)));
RecruitAudioCue CHALLENGE_S3M11_2[61] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S3M11_2") __attribute__((aligned(16)));
RecruitSprayCue CHALLENGE_S3M11_2_SPRAY[1] __asm__("jgr_ptr_challenge_demo_data_CHALLENGE_S3M11_2_SPRAY") __attribute__((aligned(16)));
RecruitDemo RECRUIT_DEMOS[19] __asm__("jgr_ptr_challenge_demo_data_RECRUIT_DEMOS") __attribute__((aligned(16)));
unsigned char literal_pool_85[502] __asm__("jgr_ptr_challenge_demo_data_literal_pool_85") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {439158194,2208,152,16,CHALLENGE_S1M11_0},
    {2957156537,2368,24,16,CHALLENGE_S1M11_0_SPRAY},
    {1831482660,2400,48,16,CHALLENGE_S1M11_1},
    {372615949,2448,24,16,CHALLENGE_S1M11_1_SPRAY},
    {4095960222,2480,220,16,CHALLENGE_S1M11_2},
    {668840336,2704,216,16,CHALLENGE_S1M11_2_SPRAY},
    {2200196104,2928,116,16,CHALLENGE_S1M11_3},
    {2175449636,3056,72,16,CHALLENGE_S1M11_3_SPRAY},
    {490783147,3136,100,16,CHALLENGE_S1M11_4},
    {1141736618,3248,24,16,CHALLENGE_S1M11_4_SPRAY},
    {1783099709,3280,312,16,CHALLENGE_S1M11_5},
    {3799682846,3600,72,16,CHALLENGE_S1M11_5_SPRAY},
    {2816373842,3680,72,16,CHALLENGE_S1M06_0},
    {1004643918,3760,24,16,CHALLENGE_S1M06_0_SPRAY},
    {3503899844,3792,180,16,CHALLENGE_S1M06_1},
    {2643897850,3984,168,16,CHALLENGE_S1M06_1_SPRAY},
    {1238373758,4160,52,16,CHALLENGE_S1M06_2},
    {2893985639,4224,24,16,CHALLENGE_S1M06_2_SPRAY},
    {2786860645,4256,60,16,CHALLENGE_S1M07_0},
    {748335629,4320,24,16,CHALLENGE_S1M07_0_SPRAY},
    {588036261,4352,44,16,CHALLENGE_S2M05_0},
    {1978812539,4400,24,16,CHALLENGE_S2M05_0_SPRAY},
    {1410041907,4432,52,16,CHALLENGE_S2M05_1},
    {3548726223,4496,24,16,CHALLENGE_S2M05_1_SPRAY},
    {3439516041,4528,84,16,CHALLENGE_S2M05_2},
    {3798817106,4624,24,16,CHALLENGE_S2M05_2_SPRAY},
    {3927364953,4656,52,16,CHALLENGE_S3M06_0},
    {3621402833,4720,24,16,CHALLENGE_S3M06_0_SPRAY},
    {2635195855,4752,136,16,CHALLENGE_S3M06_1},
    {1907176293,4896,24,16,CHALLENGE_S3M06_1_SPRAY},
    {68727925,4928,80,16,CHALLENGE_S3M06_2},
    {1078274552,5008,24,16,CHALLENGE_S3M06_2_SPRAY},
    {1474667705,5040,68,16,CHALLENGE_S3M11_0},
    {1551449638,5120,24,16,CHALLENGE_S3M11_0_SPRAY},
    {551719983,5152,92,16,CHALLENGE_S3M11_1},
    {4195235218,5248,24,16,CHALLENGE_S3M11_1_SPRAY},
    {3119236501,5280,244,16,CHALLENGE_S3M11_2},
    {3420860175,5536,24,16,CHALLENGE_S3M11_2_SPRAY},
    {138024998,5568,684,16,RECRUIT_DEMOS},
    {2432337081,6256,502,16,literal_pool_85},
};
int source_data_ptr_challenge_demo_data_load(void){return source_data_pointers_load_file("source_data_ptr_challenge_demo_data.jsd",sections,40u,6758u,0x0e3d74c4u,95u);}
#endif
