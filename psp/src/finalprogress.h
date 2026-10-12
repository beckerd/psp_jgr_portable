#ifndef FINALPROGRESS_H
#define FINALPROGRESS_H
#include <stdint.h>
/* 0e51a4 private+8 and global 2fcb5c. Phase is shared with the four
   0e9368 children and 0ecd0c; do not infer it from wall-tag totals. */
typedef struct { uint32_t flags; int contact; } FinalProgress;
typedef struct { int preload,play,remove_environment,reposition,clear; } FinalProgressEvents;
/* scene_status = 1d0fc8 (04ed2c sets scene ID on execution, -1 only
   after cleanup). Preloading alone MUST NOT count as execution. */
/* event_alive is task 0x956: the EVENT manager, NOT a collapsing platform.
   Phase five waits for that task's deletion before preloading descriptor 103. */
FinalProgressEvents finalprogress_tick(FinalProgress *,int *phase,int scene_status,int event_alive);
/* 0ed42c consumes the previous collision registration's player contact. */
void finalprogress_contact(FinalProgress *,int phase,int cinematic,int player_contact);
/* 0ecd0c: the final GF's +14c, not its first press or reveal start. */
void finalprogress_boss_tag(int *phase,int cinematic,int complete);
float finalprogress_rate(int phase); /* exact 1df2f0 including ending freeze */
#endif
