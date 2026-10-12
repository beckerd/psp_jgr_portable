#ifndef JGR_SOURCE_DATA_SECTIONS_H
#define JGR_SOURCE_DATA_SECTIONS_H
#include <stddef.h>
typedef struct {
    unsigned id,offset,bytes,alignment;
    void *destination;
} SourceDataSection;
/* Startup only. Every section is checked before any destination is changed. */
int source_data_sections_apply(const void *image,size_t bytes,
                              const SourceDataSection *sections,unsigned count,
                              unsigned expected_bytes,unsigned layout_crc);
int source_data_sections_load(const char *path,const SourceDataSection *sections,
                             unsigned count,unsigned bytes,unsigned layout_crc);
#endif
