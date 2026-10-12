#ifndef JGR_PORTABLE_REPLAY_CUES_H
#define JGR_PORTABLE_REPLAY_CUES_H
#include "portable_replay_vm.h"
typedef struct {
    uint32_t tick, kind, bank, program, parent, task;
} PortableReplayCue;
typedef struct {
    PortableReplayCue *events;
    unsigned count, capacity;
} PortableReplayCues;
/* Observe original audio initializer entry arguments without replacing its
 * behavior. Attach as a VM hook once the original task scheduler is running.
 * This recorder alone cannot generate the missing gameplay cue timeline. */
int portable_replay_cues_observe(PortableReplayVM *vm,void *context);
#endif
