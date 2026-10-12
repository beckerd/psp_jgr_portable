/* Payload-free external storage and reviewed dispatch code. */
#ifndef JGR_SOURCE_DATA_PTR_RESULT_HUD_TEXT_INC
#define JGR_SOURCE_DATA_PTR_RESULT_HUD_TEXT_INC
/* Original GAME +1e4 formats and 173ba0 numeric glyphs. */
extern const char * RESULT_TEXT[4] __asm__("jgr_ptr_result_hud_text_RESULT_TEXT");
typedef char jgr_ptr_result_hud_text_RESULT_TEXT_abi[(sizeof(RESULT_TEXT)==16u)?1:-1];
extern char RESULT_DIGITS[11][3] __asm__("jgr_ptr_result_hud_text_RESULT_DIGITS");
typedef char jgr_ptr_result_hud_text_RESULT_DIGITS_abi[(sizeof(RESULT_DIGITS)==33u)?1:-1];
/* 0366e0 / 036930: +94 tally count, indexed by playable resident id. */
extern unsigned RESULT_VOICE_DELAY[2][16] __asm__("jgr_ptr_result_hud_text_RESULT_VOICE_DELAY");
typedef char jgr_ptr_result_hud_text_RESULT_VOICE_DELAY_abi[(sizeof(RESULT_VOICE_DELAY)==128u)?1:-1];
/* 179504 language table: Japanese, then the shared Western names. */
extern const char * RESULT_NAMES[2][15] __asm__("jgr_ptr_result_hud_text_RESULT_NAMES");
typedef char jgr_ptr_result_hud_text_RESULT_NAMES_abi[(sizeof(RESULT_NAMES)==120u)?1:-1];

#endif
