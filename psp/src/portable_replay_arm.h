#ifndef JGR_PORTABLE_REPLAY_ARM_H
#define JGR_PORTABLE_REPLAY_ARM_H
#include <stdint.h>
/* Bounded ARM control interpreter for the user's loaded sound driver. No
 * instruction, initialized mailbox, sample or driver bytes are embedded. */
typedef struct {
    uint32_t r[16],cpsr,spsr_fiq,bank[7],fault,pc,opcode,address;
    uint64_t instructions;
    unsigned char *ram,*registers;
    uint32_t ram_bytes,register_bytes;
} PortableReplayArm;
void portable_replay_arm_init(PortableReplayArm *,void *,uint32_t,void *,uint32_t);
int portable_replay_arm_fiq(PortableReplayArm *,uint32_t budget);
int portable_replay_arm_run(PortableReplayArm *,uint32_t stop_pc,uint32_t budget);
#endif
