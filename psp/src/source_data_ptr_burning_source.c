/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
float PAR_T6[24] __asm__("jgr_ptr_burning_source_PAR_T6") __attribute__((aligned(16)));
float PAR_T9[24] __asm__("jgr_ptr_burning_source_PAR_T9") __attribute__((aligned(16)));
float PAR_T25[24] __asm__("jgr_ptr_burning_source_PAR_T25") __attribute__((aligned(16)));
signed char FIRE_MOTION[24] __asm__("jgr_ptr_burning_source_FIRE_MOTION") __attribute__((aligned(16)));
int FIRE_GAIT_MOTION __asm__("jgr_ptr_burning_source_FIRE_GAIT_MOTION") __attribute__((aligned(16)));
unsigned char FIRE_FRAMES[29] __asm__("jgr_ptr_burning_source_FIRE_FRAMES") __attribute__((aligned(16)));
int FIRE_FRONT_GRAB_MOTION __asm__("jgr_ptr_burning_source_FIRE_FRONT_GRAB_MOTION") __attribute__((aligned(16)));
int FIRE_AIM_MOTION[4] __asm__("jgr_ptr_burning_source_FIRE_AIM_MOTION") __attribute__((aligned(16)));
signed char KAIRIKI_MOTION[22] __asm__("jgr_ptr_burning_source_KAIRIKI_MOTION") __attribute__((aligned(16)));
int KAIRIKI_GAIT_MOTION __asm__("jgr_ptr_burning_source_KAIRIKI_GAIT_MOTION") __attribute__((aligned(16)));
unsigned char KAIRIKI_FRAMES[18] __asm__("jgr_ptr_burning_source_KAIRIKI_FRAMES") __attribute__((aligned(16)));
struct {int bone;float pos[3];} FIRE_ANCHORS[2] __asm__("jgr_ptr_burning_source_FIRE_ANCHORS") __attribute__((aligned(16)));
struct {int bone;float pos[3];} KAIRIKI_ANCHORS[3] __asm__("jgr_ptr_burning_source_KAIRIKI_ANCHORS") __attribute__((aligned(16)));
float FIRE_IDLE_TARGETS[4][4] __asm__("jgr_ptr_burning_source_FIRE_IDLE_TARGETS") __attribute__((aligned(16)));
float KAIRIKI_RADIUS __asm__("jgr_ptr_burning_source_KAIRIKI_RADIUS") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {3921153796,288,96,16,PAR_T6},
    {2030506645,384,96,16,PAR_T9},
    {3881407164,480,96,16,PAR_T25},
    {4081037982,576,24,16,FIRE_MOTION},
    {2950046042,608,4,16,&FIRE_GAIT_MOTION},
    {4172257868,624,29,16,FIRE_FRAMES},
    {182237448,656,4,16,&FIRE_FRONT_GRAB_MOTION},
    {1706977731,672,16,16,FIRE_AIM_MOTION},
    {3755884343,688,22,16,KAIRIKI_MOTION},
    {1556308834,720,4,16,&KAIRIKI_GAIT_MOTION},
    {3561887717,736,18,16,KAIRIKI_FRAMES},
    {3084101193,768,32,16,FIRE_ANCHORS},
    {418733192,800,48,16,KAIRIKI_ANCHORS},
    {1961633489,848,64,16,FIRE_IDLE_TARGETS},
    {291303557,912,4,16,&KAIRIKI_RADIUS},
};
int source_data_ptr_burning_source_load(void){return source_data_pointers_load_file("source_data_ptr_burning_source.jsd",sections,15u,916u,0x281f1b47u,0u);}
#endif
