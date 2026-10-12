#ifndef JGR_SOURCE_DATA_BUBBLE_H
#define JGR_SOURCE_DATA_BUBBLE_H
#include <stddef.h>
int source_data_bubble_load(const void *image,size_t bytes);
int source_data_bubble_load_file(const char *path);
int source_data_bubble_ready(void);
#endif
