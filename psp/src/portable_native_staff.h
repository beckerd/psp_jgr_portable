#ifndef JGR_PORTABLE_NATIVE_STAFF_H
#define JGR_PORTABLE_NATIVE_STAFF_H
#include "portable_native_world.h"
/* Source-only native credits generation. Caller owns every seekable stream.
 * record simulates the shared world once into an empty w+b common stream.
 * generate rereads that stream for one cast and writes its compact route and
 * metadata; rows, output and metadata must be empty. Tables must be loaded
 * before either operation. Return 1 on success, 0 on error/cancellation. */
int jgr_native_staff_record(const JgrNativeConfig *,FILE *common);
int jgr_native_staff_generate(const JgrNativeConfig *,unsigned character,
    FILE *common,FILE *rows,FILE *output,FILE *metadata);
#endif
