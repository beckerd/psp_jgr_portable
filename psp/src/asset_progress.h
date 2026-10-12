#ifndef JGR_ASSET_PROGRESS_H
#define JGR_ASSET_PROGRESS_H
/* Cooperative, main-thread-only load progress. No second GU producer or
   asset/renderer race. Host data tools do not own a display loop. */
#if defined(__PSP__) || defined(JGR_TEST_ASSET_PROGRESS)
void jgr_loading_poll(void);
static inline void asset_progress(void)
{ jgr_loading_poll(); }
#else
static inline void asset_progress(void) {}
#endif
#define ASSET_READ_CHUNK (64*1024)
#endif
