#ifndef JGR_PORTABLE_REPLAY_SESSION_H
#define JGR_PORTABLE_REPLAY_SESSION_H
#include "portable_tables.h"
#include <stdint.h>
#ifndef JGR_REPLAY_STATS
#ifdef JGR_DEVICE_SETUP
#define JGR_REPLAY_STATS JGR_DEVICE_SETUP
#else
#define JGR_REPLAY_STATS 0
#endif
#endif
enum { JGR_REPLAY_IDLE, JGR_REPLAY_TABLES, JGR_REPLAY_BOOTSTRAP,
 JGR_REPLAY_SETUP, JGR_REPLAY_GENERATION, JGR_REPLAY_PACKING, JGR_REPLAY_COPY };
enum { JGR_REPLAY_DEMO, JGR_REPLAY_CREDITS, JGR_REPLAY_OPPONENT,
 JGR_REPLAY_TITLE, JGR_REPLAY_FAMILIES };
typedef struct {
 unsigned output_requests,outputs,reused_outputs,jobs,native_jobs,interpreted_jobs,failures;
 unsigned family_jobs[JGR_REPLAY_FAMILIES];
 uint64_t output_bytes,total_us,tables_us,bootstrap_us,setup_us,generation_us,packing_us,copy_us;
 uint64_t family_us[JGR_REPLAY_FAMILIES];
 unsigned active,job_index,state,backend,family; /* backend: 0 none, 1 native, 2 interpreter */
 uint64_t active_us,state_us;
 char output[96];
} JgrReplayStats;
/* jobs counts generator selections (a gang selection can produce three files);
 * reused_outputs counts successful sibling copies from an already generated
 * selection, not files reused by the installer from an earlier launch.
 * Family times and stages are exclusive; generation includes streamed sample
 * recording, packing is final serialization, setup includes cleanup. Tables
 * includes shared native tables/math and the completed demonstration cue table.
 * Counters survive session destruction. Reset only between installer runs.
 * Snapshots include live stage elapsed time without mutating accumulators. */
void jgr_replay_stats(JgrReplayStats *out);
void jgr_replay_stats_reset(void);
#if JGR_REPLAY_STATS
/* Coarse call boundaries only; never call from frame/sample loops. */
void jgr_replay_stats_stage(unsigned state);
#else
#define jgr_replay_stats_stage(state) ((void)(state))
#endif
typedef struct JgrReplaySession JgrReplaySession;
typedef int (*JgrReplayProgress)(unsigned long long instructions,void *user);
JgrReplaySession *jgr_replay_session_create(JgrDisc *,const char *scratch,
    JgrTableDependency dependency,void *dependency_user,
    JgrReplayProgress progress,void *progress_user,char *error,size_t error_size);
int jgr_replay_session_known(const char *output_name);
int jgr_replay_session_generate(JgrReplaySession *,const char *output_name,FILE *out);
const char *jgr_replay_session_error(const JgrReplaySession *);
/* Opt-in during integration validation, before the first job. The ordinary
 * installer does not select this until its output identities are validated.
 * Assets are still authorized individually by the dependency callback. */
int jgr_replay_session_native_assets(JgrReplaySession *,const char *asset_directory);
void jgr_replay_session_destroy(JgrReplaySession *);
#endif
