/* External mutable storage; generated from compiler ABI only. */
#ifndef JGR_SOURCE_DATA_BENTEN_OPENING_DATA_INC
#define JGR_SOURCE_DATA_BENTEN_OPENING_DATA_INC
extern OpeningType benten_types[5] __asm__("jgr_data_benten_opening_data_benten_types");
typedef char jgr_data_benten_opening_data_benten_types_abi_size[(sizeof(benten_types)==140u)?1:-1];
extern OpeningBox benten_boxes[5] __asm__("jgr_data_benten_opening_data_benten_boxes");
typedef char jgr_data_benten_opening_data_benten_boxes_abi_size[(sizeof(benten_boxes)==540u)?1:-1];
extern OpeningPlacement benten_placements[11] __asm__("jgr_data_benten_opening_data_benten_placements");
typedef char jgr_data_benten_opening_data_benten_placements_abi_size[(sizeof(benten_placements)==528u)?1:-1];
extern float benten_lights[46][8] __asm__("jgr_data_benten_opening_data_benten_lights");
typedef char jgr_data_benten_opening_data_benten_lights_abi_size[(sizeof(benten_lights)==1472u)?1:-1];
#endif
