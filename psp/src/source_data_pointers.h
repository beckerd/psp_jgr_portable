#ifndef JGR_SOURCE_DATA_POINTERS_H
#define JGR_SOURCE_DATA_POINTERS_H
#include <stddef.h>
typedef struct {
    unsigned id,offset,bytes,alignment;
    void *destination;
} SourceDataPointerSection;
/* Startup-only: accepts the exact compiled ABI and internal 32-bit data pointers.
   No destination is modified until the complete image and all fixups validate. */
int source_data_pointers_apply(const void *image,size_t bytes,
    const SourceDataPointerSection *sections,unsigned count,unsigned expected_bytes,
    unsigned layout_crc,unsigned fixup_count);
int source_data_pointers_load_file(const char *path,const SourceDataPointerSection *sections,
    unsigned count,unsigned bytes,unsigned layout_crc,unsigned fixup_count);
int source_data_pointers_load(void);
#endif
