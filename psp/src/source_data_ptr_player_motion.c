/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {
    int type, clip, oneshot, low_clip, clip_flags;
    int end_clip, end_oneshot, end_low_clip, end_flags, blend_kind, flags;
    float rate, blend_step, entry_blend;
} MotionSpec;

#define PLAYER_MOTION_COUNT 134
#define PLAYER_REFERENCE_COUNT 170
#include "source_data_pointers.h"
MotionSpec MOTION_SPEC[PLAYER_MOTION_COUNT][2] __asm__("jgr_ptr_player_motion_MOTION_SPEC") __attribute__((aligned(16)));
short PLAYER_ANIMATION_REFS[PLAYER_REFERENCE_COUNT][4] __asm__("jgr_ptr_player_motion_PLAYER_ANIMATION_REFS") __attribute__((aligned(16)));
short LOCOMOTION_CLIPS[62][2] __asm__("jgr_ptr_player_motion_LOCOMOTION_CLIPS") __attribute__((aligned(16)));
unsigned short PLAYER_CLIP_FRAMES[16][176] __asm__("jgr_ptr_player_motion_PLAYER_CLIP_FRAMES") __attribute__((aligned(16)));
int PLAYER_LOW_HEALTH[16] __asm__("jgr_ptr_player_motion_PLAYER_LOW_HEALTH") __attribute__((aligned(16)));
float MOTION_BLEND_CURVE[2][11] __asm__("jgr_ptr_player_motion_MOTION_BLEND_CURVE") __attribute__((aligned(16)));
unsigned SPRAY_MOTION_MASK[11] __asm__("jgr_ptr_player_motion_SPRAY_MOTION_MASK") __attribute__((aligned(16)));
unsigned char SPRAY_STYLE_MOTION[17][11] __asm__("jgr_ptr_player_motion_SPRAY_STYLE_MOTION") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {2909874932,176,15008,16,MOTION_SPEC},
    {532259213,15184,1360,16,PLAYER_ANIMATION_REFS},
    {826392426,16544,248,16,LOCOMOTION_CLIPS},
    {2540734585,16800,5632,16,PLAYER_CLIP_FRAMES},
    {208815026,22432,64,16,PLAYER_LOW_HEALTH},
    {843383725,22496,88,16,MOTION_BLEND_CURVE},
    {4220722713,22592,44,16,SPRAY_MOTION_MASK},
    {2261529627,22640,187,16,SPRAY_STYLE_MOTION},
};
int source_data_ptr_player_motion_load(void){return source_data_pointers_load_file("source_data_ptr_player_motion.jsd",sections,8u,22827u,0x762d0aeeu,0u);}
#endif
