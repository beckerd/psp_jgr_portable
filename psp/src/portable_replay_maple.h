#ifndef PORTABLE_REPLAY_MAPLE_H
#define PORTABLE_REPLAY_MAPLE_H
#include <stdint.h>
#include "portable_replay_vm.h"
/* Install-only virtual controller. Buttons use the active-low wire format. */
int portable_replay_maple_dma(PortableReplayVM *, uint32_t descriptors,
                              unsigned little_endian, uint16_t buttons);
#endif
