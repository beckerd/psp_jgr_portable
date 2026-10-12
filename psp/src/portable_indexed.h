#ifndef JGR_PORTABLE_INDEXED_H
#define JGR_PORTABLE_INDEXED_H
#include "portable_models.h"
/* Optional setup-scope cache of immutable dimension-derived resize filters.
 * Main CPU only; begin/end must not run during a conversion. Retained tables
 * and their metadata use at most 128 KiB. No source pixels are retained.
 * Without begin, conversion follows the ordinary uncached allocation path. */
void jgr_indexed_cache_begin(void);
void jgr_indexed_cache_end(void);
/* First-launch texture conversion. Input is mutable and remains caller-owned.
 * Dimensions are bounded to 1024. Output must use 16x8 swizzle tile multiples.
 * Appends an aligned RGBA palette and T8 pixels; offsets are stream-relative. */
int jgr_indexed_texture(unsigned char *rgba,unsigned source_width,
    unsigned source_height,unsigned width,unsigned height,FILE *pixels,
    JgrJgcTexture *record);
/* Same encoder into caller memory: capacity must cover 1024+w*h bytes.
 * Input may be premultiplied in place; output starts with the RGBA palette. */
int jgr_indexed_memory(unsigned char *rgba,unsigned source_width,
    unsigned source_height,unsigned width,unsigned height,unsigned char *output,
    size_t capacity,unsigned *flags);
/* CPU preparation canonicalizes fully transparent RGB after exact resizing.
 * The result is immutable worker input, separately allocated and caller-owned. */
unsigned char *jgr_indexed_prepare(unsigned char *,unsigned,unsigned,unsigned,unsigned,unsigned *flags);
/* Quantize already prepared pixels without modifying them. This CPU fallback
 * uses the ordinary allocator; it must never be called by an ME worker. */
int jgr_indexed_prepared_memory(const unsigned char *,unsigned,unsigned,unsigned,
    unsigned char *,size_t);
/* Compatibility resampler, exposed for host parity checks. Always returns a
 * separate malloc allocation, or NULL; input may be premultiplied in place. */
unsigned char *jgr_rgba_lanczos(unsigned char *,unsigned,unsigned,unsigned,unsigned);
#ifdef JGR_INDEXED_TESTING
typedef struct {size_t bytes;unsigned entries,hits,builds,evictions;} JgrIndexedCacheStats;
void jgr_indexed_cache_stats(JgrIndexedCacheStats *stats);
#endif
#endif
