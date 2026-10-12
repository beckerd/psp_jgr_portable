/* External mutable storage; generated from compiler ABI only. */
#ifndef JGR_SOURCE_DATA_KOGANE_COVER_DATA_INC
#define JGR_SOURCE_DATA_KOGANE_COVER_DATA_INC
extern float cover_position[3] __asm__("jgr_data_kogane_cover_data_cover_position");
typedef char jgr_data_kogane_cover_data_cover_position_abi_size[(sizeof(cover_position)==12u)?1:-1];
extern float cover_lights[114][8] __asm__("jgr_data_kogane_cover_data_cover_lights");
typedef char jgr_data_kogane_cover_data_cover_lights_abi_size[(sizeof(cover_lights)==3648u)?1:-1];
extern ColTri cover_closed[8] __asm__("jgr_data_kogane_cover_data_cover_closed");
typedef char jgr_data_kogane_cover_data_cover_closed_abi_size[(sizeof(cover_closed)==352u)?1:-1];
extern ColTri cover_open[108] __asm__("jgr_data_kogane_cover_data_cover_open");
typedef char jgr_data_kogane_cover_data_cover_open_abi_size[(sizeof(cover_open)==4752u)?1:-1];
#endif
