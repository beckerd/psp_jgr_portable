/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {
    unsigned frames;const int16_t *angle;
    float position[3];int32_t angles[3];float scale[3];unsigned axis;
} StreetPropAnimation;
typedef struct {
    float vertices[24],center[3];unsigned attribute;
    float sphere[4],position[3];int32_t angles[3];float scale[3];int parent;unsigned kind;
} StreetPropBox;
typedef struct {
    unsigned address,type,sector,first_box,boxes,reaction;int sound;
    float position[3];int32_t angles[3];float scale[3];unsigned animation;
} StreetPropPlacement;
typedef struct {
    unsigned stage,sectors,count,box_count;
    const StreetPropPlacement *placements;const StreetPropBox *boxes;
} StreetPropStage;
enum {STREET_REACTION_BOUNCE,STREET_REACTION_FIXED,STREET_REACTION_SINGLE_BOUNCE,STREET_REACTION_ANIMATED,STREET_REACTION_VIEW_FIXED,STREET_REACTION_FULL_SINGLE_BOUNCE,STREET_REACTION_LOW_FIXED,STREET_REACTION_ANIMATED_SINGLE_BOUNCE};

#define STREET_PROP_MAX_COUNT 561u
#define STREET_PROP_MAX_SHAPES 2u
#include "source_data_pointers.h"
int16_t street_prop_yaw_51[141] __asm__("jgr_ptr_street_props_data_street_prop_yaw_51") __attribute__((aligned(16)));
int16_t street_prop_yaw_52[130] __asm__("jgr_ptr_street_props_data_street_prop_yaw_52") __attribute__((aligned(16)));
int16_t street_prop_yaw_53[190] __asm__("jgr_ptr_street_props_data_street_prop_yaw_53") __attribute__((aligned(16)));
int16_t street_prop_yaw_30[180] __asm__("jgr_ptr_street_props_data_street_prop_yaw_30") __attribute__((aligned(16)));
StreetPropAnimation street_prop_animations[4] __asm__("jgr_ptr_street_props_data_street_prop_animations") __attribute__((aligned(16)));
StreetPropBox street_prop_boxes_1[13] __asm__("jgr_ptr_street_props_data_street_prop_boxes_1") __attribute__((aligned(16)));
StreetPropPlacement street_prop_placements_1[155] __asm__("jgr_ptr_street_props_data_street_prop_placements_1") __attribute__((aligned(16)));
StreetPropBox street_prop_boxes_2[42] __asm__("jgr_ptr_street_props_data_street_prop_boxes_2") __attribute__((aligned(16)));
StreetPropPlacement street_prop_placements_2[561] __asm__("jgr_ptr_street_props_data_street_prop_placements_2") __attribute__((aligned(16)));
StreetPropBox street_prop_boxes_3[45] __asm__("jgr_ptr_street_props_data_street_prop_boxes_3") __attribute__((aligned(16)));
StreetPropPlacement street_prop_placements_3[196] __asm__("jgr_ptr_street_props_data_street_prop_placements_3") __attribute__((aligned(16)));
StreetPropBox street_prop_boxes_5[14] __asm__("jgr_ptr_street_props_data_street_prop_boxes_5") __attribute__((aligned(16)));
StreetPropPlacement street_prop_placements_5[110] __asm__("jgr_ptr_street_props_data_street_prop_placements_5") __attribute__((aligned(16)));
StreetPropStage street_prop_stages[4] __asm__("jgr_ptr_street_props_data_street_prop_stages") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {1204003913,464,282,16,street_prop_yaw_51},
    {3737843187,752,260,16,street_prop_yaw_52},
    {2848834917,1024,380,16,street_prop_yaw_53},
    {1721643865,1408,360,16,street_prop_yaw_30},
    {2626698213,1776,192,16,street_prop_animations},
    {1105675308,1968,2236,16,street_prop_boxes_1},
    {2833787808,4208,10540,16,street_prop_placements_1},
    {3639481750,14752,7224,16,street_prop_boxes_2},
    {836860442,21984,38148,16,street_prop_placements_2},
    {2951292160,60144,7740,16,street_prop_boxes_3},
    {1189497484,67888,13328,16,street_prop_placements_3},
    {1183481909,81216,2408,16,street_prop_boxes_5},
    {2944790457,83632,7480,16,street_prop_placements_5},
    {859528798,91120,96,16,street_prop_stages},
};
int source_data_ptr_street_props_data_load(void){return source_data_pointers_load_file("source_data_ptr_street_props_data.jsd",sections,14u,91216u,0x21feac67u,12u);}
#endif
