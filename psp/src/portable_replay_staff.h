#ifndef JGR_PORTABLE_REPLAY_STAFF_H
#define JGR_PORTABLE_REPLAY_STAFF_H
#include "portable_replay_bootstrap.h"
#include <stdio.h>
typedef struct JgrReplayStaff JgrReplayStaff;
/* Install-only observer; retains bounded sparse metadata, never route-sized
 * pose images. The bootstrap context must outlive this observer. */
JgrReplayStaff *jgr_replay_staff_create(PortableReplayBootstrap *,unsigned character);
int jgr_replay_staff_sample(JgrReplayStaff *,const unsigned char actor[0x900],
    const unsigned char camera[0x290],const unsigned char controller[0xd0],
    unsigned *part,unsigned *cut,unsigned *spray_variant);
/* Register as the bootstrap's frame observer. Original game state is read
 * only; this records complete wall-paint/reveal state at the same boundary. */
int jgr_replay_staff_frame(void *);
int jgr_replay_staff_finish(JgrReplayStaff *,FILE *out);
void jgr_replay_staff_destroy(JgrReplayStaff *);
#endif
