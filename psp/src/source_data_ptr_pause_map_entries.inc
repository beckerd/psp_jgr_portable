/* Payload-free external storage and reviewed dispatch code. */
#ifndef JGR_SOURCE_DATA_PTR_PAUSE_MAP_ENTRIES_INC
#define JGR_SOURCE_DATA_PTR_PAUSE_MAP_ENTRIES_INC
/* Generated from original 178c84 descriptors and U_GAME. */
extern PauseMapEntry PAUSE_MAPS[6][6] __asm__("jgr_ptr_pause_map_entries_PAUSE_MAPS");
typedef char jgr_ptr_pause_map_entries_PAUSE_MAPS_abi[(sizeof(PAUSE_MAPS)==1440u)?1:-1];
extern unsigned PAUSE_MAP_COUNTS[6] __asm__("jgr_ptr_pause_map_entries_PAUSE_MAP_COUNTS");
typedef char jgr_ptr_pause_map_entries_PAUSE_MAP_COUNTS_abi[(sizeof(PAUSE_MAP_COUNTS)==24u)?1:-1];
extern int PAUSE_MAP_ICONS[9] __asm__("jgr_ptr_pause_map_entries_PAUSE_MAP_ICONS");
typedef char jgr_ptr_pause_map_entries_PAUSE_MAP_ICONS_abi[(sizeof(PAUSE_MAP_ICONS)==36u)?1:-1];
extern const char * PAUSE_MAP_TEXT[4] __asm__("jgr_ptr_pause_map_entries_PAUSE_MAP_TEXT");
typedef char jgr_ptr_pause_map_entries_PAUSE_MAP_TEXT_abi[(sizeof(PAUSE_MAP_TEXT)==16u)?1:-1];
extern char PAUSE_MAP_DIGITS[11][3] __asm__("jgr_ptr_pause_map_entries_PAUSE_MAP_DIGITS");
typedef char jgr_ptr_pause_map_entries_PAUSE_MAP_DIGITS_abi[(sizeof(PAUSE_MAP_DIGITS)==33u)?1:-1];
extern float PAUSE_MAP_TEXT_XY[4] __asm__("jgr_ptr_pause_map_entries_PAUSE_MAP_TEXT_XY");
typedef char jgr_ptr_pause_map_entries_PAUSE_MAP_TEXT_XY_abi[(sizeof(PAUSE_MAP_TEXT_XY)==16u)?1:-1];

#endif
