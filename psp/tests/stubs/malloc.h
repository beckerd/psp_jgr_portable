#ifndef TEST_MALLOC_H
#define TEST_MALLOC_H
#include <stdlib.h>
static inline void *memalign(size_t alignment, size_t size) {
    void *p = 0;
    return posix_memalign(&p, alignment, size) ? 0 : p;
}
#endif
