#ifndef RESULT_TRANSITION_H
#define RESULT_TRANSITION_H
#include "mission.h"
#include "finalarena.h"

/* 0361ec case 0 sends completed stage 3 directly to ending mode 14. The
   viewer numbers that stage 4. Keep the ordinary result/chain guards, and
   require the earned Final controller clear plus resident EVENT cleanup. */
static inline int result_terminal_ready(const MissionState *mission,
    const FinalArena *final, int level_id, int campaign_error,
    int scene_blocked, int confirmed)
{
    return mission && !campaign_error && !scene_blocked && mission->active &&
        (mission->phase==1 || mission->phase==2) && (mission->phase!=1 || mission->next_rec<0 ||
        mission->next_set<0) && (confirmed || (level_id==4 &&
        mission->phase==1 && finalarena_clear_ready(final)));
}
#endif
