#ifndef JGR_PORTABLE_NATIVE_TITLE_H
#define JGR_PORTABLE_NATIVE_TITLE_H
#include "portable_native_world.h"
/* Source-driven attract playback. Reads the first authored PLAYDEMO bank and
 * verified native world assets; no recorded positions or poses are inputs.
 * Caller initializes native tables/math and owns the empty seekable streams.
 * Returns zero on success, -1 on error/cancellation. */
int jgr_native_title_generate(const JgrNativeConfig *,unsigned character,FILE *scratch,FILE *output);
#endif
