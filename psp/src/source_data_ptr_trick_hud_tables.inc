/* Payload-free external storage and reviewed dispatch code. */
#ifndef JGR_SOURCE_DATA_PTR_TRICK_HUD_TABLES_INC
#define JGR_SOURCE_DATA_PTR_TRICK_HUD_TABLES_INC
/* Generated from original US executable and PLAY; language 1. */
extern int TRICK_POINTS[20] __asm__("jgr_ptr_trick_hud_tables_TRICK_POINTS");
typedef char jgr_ptr_trick_hud_tables_TRICK_POINTS_abi[(sizeof(TRICK_POINTS)==80u)?1:-1];
extern int TRICK_BONUS[16] __asm__("jgr_ptr_trick_hud_tables_TRICK_BONUS");
typedef char jgr_ptr_trick_hud_tables_TRICK_BONUS_abi[(sizeof(TRICK_BONUS)==64u)?1:-1];
extern const char * TRICK_NAMES[7][30] __asm__("jgr_ptr_trick_hud_tables_TRICK_NAMES");
typedef char jgr_ptr_trick_hud_tables_TRICK_NAMES_abi[(sizeof(TRICK_NAMES)==840u)?1:-1];
extern char TRICK_SUFFIX[16] __asm__("jgr_ptr_trick_hud_tables_TRICK_SUFFIX");
typedef char jgr_ptr_trick_hud_tables_TRICK_SUFFIX_abi[(sizeof(TRICK_SUFFIX)==16u)?1:-1];
extern unsigned char TRICK_GRIND_IDS[16][2][6] __asm__("jgr_ptr_trick_hud_tables_TRICK_GRIND_IDS");
typedef char jgr_ptr_trick_hud_tables_TRICK_GRIND_IDS_abi[(sizeof(TRICK_GRIND_IDS)==192u)?1:-1];
extern unsigned char TRICK_AIR_IDS[16][2][4] __asm__("jgr_ptr_trick_hud_tables_TRICK_AIR_IDS");
typedef char jgr_ptr_trick_hud_tables_TRICK_AIR_IDS_abi[(sizeof(TRICK_AIR_IDS)==128u)?1:-1];

#endif
