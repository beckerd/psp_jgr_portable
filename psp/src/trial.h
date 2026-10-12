#ifndef JGR_TRIAL_H
#define JGR_TRIAL_H
#include "ctrl.h"
/* Calls, not inferred poses: the original player invokes these at distinct
   points in its state handlers. +614 and +618 are BOTH floating point. */
enum TrialAction {
    TRIAL_ADVANCE, TRIAL_ADVANCE_DEFER, TRIAL_FAIL_VOLUME,
    TRIAL_GRIND, TRIAL_WALL, TRIAL_TAG, TRIAL_TAG_DEFER, TRIAL_FINISH,
    TRIAL_GROUND_TRICK, TRIAL_JUMP, TRIAL_GROUND_SIX, TRIAL_FAIL
};
typedef struct {
    const TrialEntry *entries;
    int count, index, result, result_bound;
    float distance, counter;
} Trial;
void trial_begin(Trial *t,const TrialEntry *entries,int count);
int trial_inside(const float *volume,const float feet[3]); /* 0c7ff4 */
void trial_action(Trial *t,int action,const float feet[3],float speed,int air_trick);
#endif
