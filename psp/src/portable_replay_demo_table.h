#ifndef JGR_PORTABLE_REPLAY_DEMO_TABLE_H
#define JGR_PORTABLE_REPLAY_DEMO_TABLE_H
#include "portable_disc.h"
#include <stdio.h>
/* Ordered by the19 demo selectors in capture_jobs.json. Every observed RDC1
 * buffer must come from the current native setup session, never recipes. */
int jgr_replay_demo_table(JgrDisc *,const unsigned char *observed[19],const unsigned sizes[19],FILE *);
#endif
