#ifndef JGR_PORTABLE_STARTUP_CACHE_H
#define JGR_PORTABLE_STARTUP_CACHE_H
#include <stddef.h>
typedef struct { unsigned char *image; size_t bytes; } PortableStartupCache;
/* Generated local cache, never a shipped asset. All temporary storage is
 * bounded by the compiled source-table ABI (currently less than 512 KiB). */
int portable_startup_cache_read(PortableStartupCache *,const char *root);
int portable_startup_cache_collect(PortableStartupCache *,const char *root);
int portable_startup_cache_write(const PortableStartupCache *,const char *root);
void portable_startup_cache_free(PortableStartupCache *);
int portable_startup_cache_view(void *,const char *,const void **,size_t *);
/* Called after the installer has verified every asset, before its receipt. */
int portable_startup_cache_prepare(const char *root);
#endif
