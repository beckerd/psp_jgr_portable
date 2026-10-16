#ifndef JGR_SETUP_MODULE_H
#define JGR_SETUP_MODULE_H
#include <stdint.h>

/* Only plain data crosses the module boundary. No allocator, FILE, callback,
 * or pointer into converter code may survive stop/unload. The host keeps
 * result alive until setup completes and module_stop joins its thread. */
#define JGR_SETUP_MODULE_ABI 0x4a534d01u
#define JGR_SETUP_ROOT_BYTES 768
typedef struct {
    uint32_t abi,bytes;
    char root[JGR_SETUP_ROOT_BYTES];
    volatile int *result;
} JgrSetupModuleArgs;
#define JGR_SETUP_PENDING (-32767)
#endif
