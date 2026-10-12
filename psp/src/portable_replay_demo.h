#ifndef JGR_PORTABLE_REPLAY_DEMO_H
#define JGR_PORTABLE_REPLAY_DEMO_H
#include "portable_replay_cues.h"
#include <stdio.h>
typedef struct JgrReplayDemo JgrReplayDemo;
JgrReplayDemo *jgr_replay_demo_create(void);
int jgr_replay_demo_sample(JgrReplayDemo *,const unsigned char actor[0x900]);
/* Serialize bounded audio/spray observations as RDC1 for the later aggregate
 * JSD writer. The final JSD is checked against the expected asset catalog. */
int jgr_replay_demo_finish(JgrReplayDemo *,const PortableReplayCue *,unsigned count,FILE *);
void jgr_replay_demo_destroy(JgrReplayDemo *);
#endif
