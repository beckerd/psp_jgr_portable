/* External mutable storage; generated from compiler ABI only. */
#ifndef JGR_SOURCE_DATA_BANTAM_OPENING_DATA_INC
#define JGR_SOURCE_DATA_BANTAM_OPENING_DATA_INC
extern OpeningType bantam_types[2] __asm__("jgr_data_bantam_opening_data_bantam_types");
typedef char jgr_data_bantam_opening_data_bantam_types_abi_size[(sizeof(bantam_types)==56u)?1:-1];
extern OpeningBox bantam_boxes[2] __asm__("jgr_data_bantam_opening_data_bantam_boxes");
typedef char jgr_data_bantam_opening_data_bantam_boxes_abi_size[(sizeof(bantam_boxes)==216u)?1:-1];
extern OpeningPlacement bantam_placements[12] __asm__("jgr_data_bantam_opening_data_bantam_placements");
typedef char jgr_data_bantam_opening_data_bantam_placements_abi_size[(sizeof(bantam_placements)==576u)?1:-1];
extern float bantam_lights[71][8] __asm__("jgr_data_bantam_opening_data_bantam_lights");
typedef char jgr_data_bantam_opening_data_bantam_lights_abi_size[(sizeof(bantam_lights)==2272u)?1:-1];
extern ColTri bantam_door_floors[4] __asm__("jgr_data_bantam_opening_data_bantam_door_floors");
typedef char jgr_data_bantam_opening_data_bantam_door_floors_abi_size[(sizeof(bantam_door_floors)==176u)?1:-1];
#endif
