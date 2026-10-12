/* External mutable storage; generated from compiler ABI only. */
#ifndef JGR_SOURCE_DATA_SHIBUYA_GATE_DATA_INC
#define JGR_SOURCE_DATA_SHIBUYA_GATE_DATA_INC
extern BarrierPlacement barrier_placements[6] __asm__("jgr_data_shibuya_gate_data_barrier_placements");
typedef char jgr_data_shibuya_gate_data_barrier_placements_abi_size[(sizeof(barrier_placements)==384u)?1:-1];
extern ColTri barrier_collision[10] __asm__("jgr_data_shibuya_gate_data_barrier_collision");
typedef char jgr_data_shibuya_gate_data_barrier_collision_abi_size[(sizeof(barrier_collision)==440u)?1:-1];
#endif
