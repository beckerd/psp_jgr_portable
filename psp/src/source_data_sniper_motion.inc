/* External mutable storage; generated from compiler ABI only. */
#ifndef JGR_SOURCE_DATA_SNIPER_MOTION_INC
#define JGR_SOURCE_DATA_SNIPER_MOTION_INC
extern signed char primary[2][4] __asm__("jgr_data_sniper_motion_primary");
typedef char jgr_data_sniper_motion_primary_abi_size[(sizeof(primary)==8u)?1:-1];
extern signed char turn[2] __asm__("jgr_data_sniper_motion_turn");
typedef char jgr_data_sniper_motion_turn_abi_size[(sizeof(turn)==2u)?1:-1];
#endif
