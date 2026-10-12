#ifndef JGR_PORTABLE_BUNDLE_H
#define JGR_PORTABLE_BUNDLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum { PORTABLE_BUNDLE_ME=1, PORTABLE_BUNDLE_CRASH=2,
       PORTABLE_BUNDLE_NOTICE=3, PORTABLE_BUNDLE_RECIPE=4,
       PORTABLE_BUNDLE_GAME=5 };
enum { PORTABLE_BUNDLE_OK=0, PORTABLE_BUNDLE_IO=-1,
       PORTABLE_BUNDLE_FORMAT=-2, PORTABLE_BUNDLE_CHECKSUM=-3,
       PORTABLE_BUNDLE_PATH=-4, PORTABLE_BUNDLE_MISSING=-5 };
#define PORTABLE_BUNDLE_PATH_MAX 768
typedef struct {
    int status;
    unsigned written,reused,entries,index_crc;
} PortableBundleResult;
typedef struct {
    FILE *file;
    uint32_t remaining,crc,expected_crc;
    int error;
} PortableBundleStream;
/* Byte progress within one named verification/extraction operation. Counters
   restart for each operation; total=0 means its size is not yet known. The
   name is valid only during the callback. Callbacks must not reenter bundle
   APIs; callers may throttle screen updates without losing I/O progress. */
typedef void (*PortableBundleProgress)(unsigned current,unsigned total,
                                      const char *name,void *user);

/* Startup only. NULL uses getcwd(). A fresh EBOOT's bounded DATA.PSAR bundle
   is verified before helpers/notices are published to a versioned cache.
   A failed preparation invalidates all path/stream access. No game assets
   are accepted as module entries. Checksums detect damage, not authenticity. */
int portable_bundle_prepare(const char *application_root,PortableBundleResult *result);
int portable_bundle_prepare_progress(const char *application_root,PortableBundleResult *result,
                                     PortableBundleProgress progress,void *user);
/* Completed-install startup only. Returns 0 when the current EBOOT's complete
   index exactly matches a receipt from successful prepare, and every cached
   entry is still a regular file of the recorded size. Reads only the bounded
   index/receipt and file metadata; no writes, callbacks or payload hashing.
   Any negative return is a cache miss and invalidates path/stream access;
   call prepare to verify and repair. Same-size external payload damage needs
   full prepare; direct bundle streams always retain their checksum checks. */
int portable_bundle_try_cached(const char *application_root,PortableBundleResult *result);
/* Return an absolute prepared cache path for a helper, notice, or recipe. */
int portable_bundle_path(unsigned role,char *out,size_t capacity);
/* The bundle can also be streamed directly. Reads cannot cross an entry. close returns
   success only after the entire entry was read and its checksum matched. */
int portable_bundle_open_entry(unsigned role,PortableBundleStream *stream);
size_t portable_bundle_read(PortableBundleStream *stream,void *out,size_t bytes);
int portable_bundle_close(PortableBundleStream *stream);
const char *portable_bundle_error(int status);

#endif
