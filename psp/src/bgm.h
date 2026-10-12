#ifndef BGM_H
#define BGM_H
typedef struct { int kind, n, loop; unsigned char seq[32]; } BgmEntry;   /* kind (song table word 1: 0 chain, 2/4 single ADX with embedded loops), ADX indices, loop node (-1 = stop at the end) */
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_mixed_bgm_c__bgm_name_0.inc"
#else
extern const char *const BGM_NAME[211];
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_bgm_c__bgm_table_0.inc"
#else
extern const BgmEntry BGM_TABLE[];
#endif
extern const int BGM_NENTRIES;
int bgm_pick(int mode, int stage, int slot, int prev, int rnd, int cur, int w80);
int bgm_clear_pick(int mode, int cur, int w80);
void bgm_play(int entry); int bgm_current(void); void bgm_stage(int mode, int stage, int slot); void bgm_clear(int mode); void bgm_fail(void);
void bgm_leave(void); /* 02e840: quitting preserves the next playlist variant */
#endif
