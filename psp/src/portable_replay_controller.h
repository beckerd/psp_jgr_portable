#ifndef JGR_PORTABLE_REPLAY_CONTROLLER_H
#define JGR_PORTABLE_REPLAY_CONTROLLER_H
#include "portable_replay_vm.h"
/* Working proof of original input playback. This does not produce a route:
 * original player/collision/pose/camera scheduling remains to be implemented. */
typedef struct {
    PortableReplayVM vm;
    unsigned char work[4096];
    uint32_t stream, tick, samples, maximum_ticks;
} PortableReplayController;
typedef struct {
    uint32_t tick, record, input, previous, repeat, ended, direction, magnitude;
} PortableReplayInput;
int portable_replay_controller_begin(PortableReplayController *c,
    const void *executable,uint32_t executable_bytes,const void *mission_set,
    uint32_t mission_set_bytes,unsigned subset);
int portable_replay_controller_step(PortableReplayController *c,PortableReplayInput *sample);
#endif
