#ifndef JGR_PORTABLE_NATIVE_RIVAL_H
#define JGR_PORTABLE_NATIVE_RIVAL_H
#include "portable_native_world.h"
#include "portable_replay_bootstrap.h"
/* Generate one mode-3 actor's complete authored controller table. Gang
 * fixtures describe spectator inputs and waiting animation clocks only;
 * no captured world positions, skeletal poses, or collision results enter
 * the simulation. Member indexes the source controller table, not a model.
 * Both streams belong to the caller; rows must be empty and seekable.
 * Returns zero on success, negative on failure/cancellation. */
int jgr_native_rival_generate(const JgrNativeConfig *,const char *mission,
    unsigned record,unsigned subset,unsigned character,unsigned member,
    unsigned sections,const PortableReplayGangFixture *fixture,
    FILE *rows,FILE *output);
#endif
