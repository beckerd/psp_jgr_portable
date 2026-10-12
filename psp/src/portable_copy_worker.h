#ifndef JGR_PORTABLE_COPY_WORKER_H
#define JGR_PORTABLE_COPY_WORKER_H
#include "portable_disc.h"
#include <stddef.h>
#include <stdint.h>

#define JGR_COPY_WORKER_MAX_TASKS 512u
#define JGR_COPY_WORKER_PAYLOAD_BYTES (128u * 1024u)
#define JGR_COPY_WORKER_BUFFER_BYTES ((JGR_COPY_WORKER_PAYLOAD_BYTES / 2048u + 1u) * 2352u)
enum { JGR_COPY_OK=0, JGR_COPY_IO=-1, JGR_COPY_CHECKSUM=-2,
       JGR_COPY_CANCELLED=-3, JGR_COPY_DISC=-4 };
/* The caller validates destination ownership/parents and keeps this entire
 * immutable array, including path strings, alive until worker destruction.
 * Source extents must have the expected size and CRC32. The computed CRC
 * is returned with each completed task. */
typedef struct {
    JgrDiscFile source;
    const char *temporary;
    uint32_t bytes,crc32,tag;
} JgrCopyTask;
typedef struct {
    int status,system_error;
    uint32_t tag,crc32,chunks;
    uint64_t bytes,read_us,write_us,hash_us;
    char detail[160];
} JgrCopyCompletion;
typedef struct {
    unsigned completed,total;
    int finished,paused,status;
} JgrCopyProgress;
typedef struct JgrCopyWorker JgrCopyWorker;
/* Independent validated disc handle; no plan, converter, scratch or UI access.
 * Tasks run serially on the worker while the caller can run other conversion.
 * A failed task stops the batch. Temporary files are never renamed/removed. */
JgrCopyWorker *jgr_copy_worker_create(const JgrDisc *disc,const JgrCopyTask *tasks,
    unsigned count,int start_paused,char *error,size_t error_size);
int jgr_copy_worker_progress(JgrCopyWorker *,JgrCopyProgress *);
/* Returns1 for a completed immutable result,0 when pending,-1 for bad index. */
int jgr_copy_worker_result(JgrCopyWorker *,unsigned index,JgrCopyCompletion *);
/* Pause is acknowledged between bounded chunks, after no disc/file call is
 * active. Request first, then wait_paused before entering an exclusive phase.
 * A paused worker retains buffers/stack. Destroy before low-memory phases. */
void jgr_copy_worker_pause(JgrCopyWorker *,int paused);
int jgr_copy_worker_wait_paused(JgrCopyWorker *);
void jgr_copy_worker_cancel(JgrCopyWorker *);
/* Wait drains/closes the active task. Returns the terminal batch status. */
int jgr_copy_worker_wait(JgrCopyWorker *);
/* Cancels if necessary, wakes pause waiters, joins, then releases all state. */
void jgr_copy_worker_destroy(JgrCopyWorker *);
#endif
