#ifndef JGR_PORTABLE_REPLAY_STAFF_REUSE_H
#define JGR_PORTABLE_REPLAY_STAFF_REUSE_H
#include "portable_replay_bootstrap.h"
#include <stdio.h>
/* Install-only optimization. The first complete credits run writes transient
 * world inputs, then each remaining character executes its own original pose
 * driver. Scratch is generated locally from the current disc, never shipped. */
typedef int (*PortableReplayStaffPoseSample)(void *,const unsigned char[0x900],
 const unsigned char[0x290],const unsigned char[0xd0],const unsigned char[0x400],
 unsigned part,unsigned cut,unsigned variant);
/* Call after selecting character 0 STAFF and before stepping. Caller owns FILE.
 * Finish requires a completed run and flushes a checked end record. */
int portable_replay_bootstrap_staff_record(PortableReplayBootstrap *,FILE *);
int portable_replay_bootstrap_staff_record_finish(PortableReplayBootstrap *);
/* Selects an install-only STAFF pose job. Use the normal budgeted step/status
 * APIs. Completion restores the original scheduler context for the next job.
 * Samples are read-only views valid only during the callback. */
int portable_replay_bootstrap_staff_poses(PortableReplayBootstrap *,FILE *,unsigned character,
 PortableReplayStaffPoseSample,void *);
#endif
