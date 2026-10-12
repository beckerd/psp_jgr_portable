/* External mutable storage; generated from compiler ABI only. */
#ifndef JGR_SOURCE_DATA_BENTEN_TRAIN_DATA_INC
#define JGR_SOURCE_DATA_BENTEN_TRAIN_DATA_INC
extern unsigned train_selectors[8][2] __asm__("jgr_data_benten_train_data_train_selectors");
typedef char jgr_data_benten_train_data_train_selectors_abi_size[(sizeof(train_selectors)==64u)?1:-1];
extern MissionQuad train_boundaries[5] __asm__("jgr_data_benten_train_data_train_boundaries");
typedef char jgr_data_benten_train_data_train_boundaries_abi_size[(sizeof(train_boundaries)==400u)?1:-1];
extern TrainBody train_bodies[4] __asm__("jgr_data_benten_train_data_train_bodies");
typedef char jgr_data_benten_train_data_train_bodies_abi_size[(sizeof(train_bodies)==592u)?1:-1];
extern TrainVertex train_vertices0[8] __asm__("jgr_data_benten_train_data_train_vertices0");
typedef char jgr_data_benten_train_data_train_vertices0_abi_size[(sizeof(train_vertices0)==128u)?1:-1];
extern TrainTriangle train_surfaces0[4] __asm__("jgr_data_benten_train_data_train_surfaces0");
typedef char jgr_data_benten_train_data_train_surfaces0_abi_size[(sizeof(train_surfaces0)==80u)?1:-1];
extern TrainVertex train_vertices1[104] __asm__("jgr_data_benten_train_data_train_vertices1");
typedef char jgr_data_benten_train_data_train_vertices1_abi_size[(sizeof(train_vertices1)==1664u)?1:-1];
extern TrainTriangle train_surfaces1[108] __asm__("jgr_data_benten_train_data_train_surfaces1");
typedef char jgr_data_benten_train_data_train_surfaces1_abi_size[(sizeof(train_surfaces1)==2160u)?1:-1];
extern float train_lights[46][8] __asm__("jgr_data_benten_train_data_train_lights");
typedef char jgr_data_benten_train_data_train_lights_abi_size[(sizeof(train_lights)==1472u)?1:-1];
#endif
