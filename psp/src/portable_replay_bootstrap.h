#ifndef JGR_PORTABLE_REPLAY_BOOTSTRAP_H
#define JGR_PORTABLE_REPLAY_BOOTSTRAP_H
#include "portable_disc.h"
#include "portable_replay_cues.h"
/* Install-only execution. The caller owns the validated disc and the generic
 * CPU hardware reference throughout this context's lifetime. No source code
 * or original work memory is retained after destroy. */
typedef struct PortableReplayBootstrap PortableReplayBootstrap;
enum {PORTABLE_REPLAY_DEMONSTRATION=0,PORTABLE_REPLAY_TITLE=1,PORTABLE_REPLAY_STAFF=2,PORTABLE_REPLAY_RIVAL=3,PORTABLE_REPLAY_GANG=4};
typedef struct {
    unsigned stage,chapter,record,subset,character;
    unsigned kind;
} PortableReplaySelection;
/* Explicit spectator input scenario. This contains no rival route samples,
 * skeletal poses, collision results, or executable instructions. */
typedef struct {
    float approach[3][3];
    unsigned idle_phase[3][3];
} PortableReplayGangFixture;
typedef int (*PortableReplaySample)(void *context,const unsigned char actor[0x900],
    const unsigned char camera[0x290],const unsigned char controller[0xd0],
    const unsigned char pose[0x400]);
typedef int (*PortableReplayFrame)(void *context);
typedef struct {
    uint64_t instructions,virtual_clock,idle_ticks_skipped,sectors_read;
    uint32_t fault,pc,address,opcode,pr,sp,argument[3];
    unsigned frames,samples,cues,completed;
    uint32_t main_task,main_callback,player;
    unsigned cleanup,requested,selected;
} PortableReplayStatus;
/* Returns NULL on allocation, unsupported source, or hardware-reference error.
 * Allocates approximately 18 MiB; create only after other converter buffers
 * have been released. No pre-initialized game memory is an accepted input. */
PortableReplayBootstrap *portable_replay_bootstrap_create(JgrDisc *,
    const uint32_t *cpu_hardware_reference,unsigned reference_words);
/* Metadata selects an original mission replay. The sample callback must
 * consume its four views before returning, and return nonzero on success. */
int portable_replay_bootstrap_select(PortableReplayBootstrap *,
    const PortableReplaySelection *,PortableReplaySample,void *context);
int portable_replay_bootstrap_gang_fixture(PortableReplayBootstrap *,const PortableReplayGangFixture *);
void portable_replay_bootstrap_frame_observer(PortableReplayBootstrap *,PortableReplayFrame,void *context);
/* 0 = yielded, 1 = complete, -1 = error. Device events and original callbacks
 * execute between bounded slices; caller can update UI/cancel after a yield. */
int portable_replay_bootstrap_step(PortableReplayBootstrap *,unsigned instruction_budget);
void portable_replay_bootstrap_status(const PortableReplayBootstrap *,PortableReplayStatus *);
const PortableReplayCue *portable_replay_bootstrap_cues(const PortableReplayBootstrap *,unsigned *count);
/* Bounded install-time observation of original work memory. This never reads
 * device registers or accesses the original disc. */
int portable_replay_bootstrap_read(const PortableReplayBootstrap *,uint32_t address,
    void *destination,unsigned bytes);
void portable_replay_bootstrap_destroy(PortableReplayBootstrap *);
#endif
