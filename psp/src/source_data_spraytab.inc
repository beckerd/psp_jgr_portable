#define N_CUES 11
#define N_PATROWS 21
/* External mutable storage; generated from compiler ABI only. */
#ifndef JGR_SOURCE_DATA_SPRAYTAB_INC
#define JGR_SOURCE_DATA_SPRAYTAB_INC
extern int CUES[11][7] __asm__("jgr_data_spraytab_CUES");
typedef char jgr_data_spraytab_CUES_abi_size[(sizeof(CUES)==308u)?1:-1];
extern int PATROWS[21][4] __asm__("jgr_data_spraytab_PATROWS");
typedef char jgr_data_spraytab_PATROWS_abi_size[(sizeof(PATROWS)==336u)?1:-1];
#endif
