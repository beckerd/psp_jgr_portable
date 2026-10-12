#ifndef JGR_PORTABLE_SETUP_ME_H
#define JGR_PORTABLE_SETUP_ME_H
#include <stddef.h>
#include <stdint.h>

/* Setup owns a separate ME transport; gameplay and replay pose workers are
 * never resident clients. Workers may only use their preallocated storage,
 * pure computation and immutable program data. No allocation or OS calls. */
typedef int (*JgrSetupMeWorker)(intptr_t argument);
enum { JGR_SETUP_ME_READ=1, JGR_SETUP_ME_WRITE=2, JGR_SETUP_ME_MAX_RANGES=8 };
typedef struct {void *address;size_t bytes;unsigned flags;} JgrSetupMeRange;
enum { JGR_SETUP_ME_CANCELLED=-1, JGR_SETUP_ME_PENDING=0, JGR_SETUP_ME_COMPLETE=1 };
typedef struct {
    unsigned available,active,disabled;
    int load_error,boot_status;
    uint64_t jobs,completed,cancelled,timeouts,cache_us,wait_us;
} JgrSetupMeStats;

/* Main setup thread only, except the three note hooks. helper must be a
 * verified bundle path. Unsupported firmware, an emulator or a "nome" file
 * returns zero; callers retain the CPU implementation. No worker is run
 * synchronously here. Once stopped, this transport cannot restart this boot. */
int jgr_setup_me_init(const char *helper);
int jgr_setup_me_available(void);
/* One outstanding job. All ranges must be distinct cached RAM, 64-byte
 * aligned in address and length, including every descriptor/arena write.
 * On success CPU ownership is borrowed until COMPLETE or CANCELLED. A zero
 * submit result borrows nothing. Poll/wait never modify result on cancel. */
int jgr_setup_me_submit(JgrSetupMeWorker worker,intptr_t argument,
                       const JgrSetupMeRange *ranges,unsigned count);
int jgr_setup_me_poll(int *result);
/* timeout requests a physical stop. PENDING after timeout means that reset
 * could not be confirmed: retain every borrowed allocation and abort setup.
 * Never recompute into, free, or reuse a still-owned buffer. */
int jgr_setup_me_wait(unsigned timeout_us,int *result);
/* Cancel returns one only after a physical fence; shutdown additionally
 * requires successful kernel module stop/unload before executable handoff. */
int jgr_setup_me_cancel(void);
int jgr_setup_me_shutdown(void);
void jgr_setup_me_note_suspend(void);
void jgr_setup_me_note_resume(void);
void jgr_setup_me_note_exit(void);
void jgr_setup_me_stats(JgrSetupMeStats *out);
#endif
