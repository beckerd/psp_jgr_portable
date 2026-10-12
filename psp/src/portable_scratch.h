#ifndef JGR_PORTABLE_SCRATCH_H
#define JGR_PORTABLE_SCRATCH_H
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
/* Setup-only scratch storage. Paths are checked lazily before any disk open;
 * an in-RAM stream never touches its path. A pool outlives all its streams. */
#define JGR_SCRATCH_DISK_FILES 16
#ifndef JGR_SCRATCH_DEFAULT_BUDGET
#define JGR_SCRATCH_DEFAULT_BUDGET (1024u*1024u)
#endif
typedef struct JgrScratchPool JgrScratchPool;
typedef struct JgrScratchReclaim {
    JgrScratchPool *pool;
    struct JgrScratchReclaim *previous;
    void (*release)(void *);
    void *context;
} JgrScratchReclaim;
struct JgrScratchPool {
    size_t budget,allocated,peak,headroom;
    FILE *file[JGR_SCRATCH_DISK_FILES];
    void *owner[JGR_SCRATCH_DISK_FILES];
    uint32_t age[JGR_SCRATCH_DISK_FILES],clock;
    unsigned spills,opens,streams;
    /* A separate, lazy 64 KiB read cache covers spilled streams together.
     * It is released when the last stream closes, and is optional on OOM. */
    void *read_cache;
    void *first_stream;
    unsigned read_cache_failed;
    JgrScratchReclaim *reclaim;
    unsigned reclaiming;
};
void jgr_scratch_init(JgrScratchPool *,size_t memory_budget);
/* Optional residency can yield before scratch spills or retries a failed
 * allocation. A zeroed caller-owned scope is consumed before its callback;
 * callbacks only free optional memory and never operate on scratch streams.
 * Scopes nest in LIFO order and must end before their storage goes away. */
int jgr_scratch_reclaim_begin(JgrScratchPool *,JgrScratchReclaim *,void (*)(void *),void *);
int jgr_scratch_reclaim_end(JgrScratchReclaim *);
FILE *jgr_scratch_open(JgrScratchPool *,const char *private_path);
/* Close and remove only a backing file created by this stream, including one
 * retained across reset. Preserve the first close/remove error. Plain fclose
 * remains available for callers that manage spill cleanup themselves. */
int jgr_scratch_discard(JgrScratchPool *,FILE *);
/* Discard a completed temporary stream before reusing its public FILE. */
int jgr_scratch_reset(JgrScratchPool *,FILE *);
/* Completed in-RAM contents can be borrowed without copying. A view prevents
 * writes, reset and selective spilling until the matching unview call. */
const void *jgr_scratch_view(JgrScratchPool *,FILE *,size_t *size);
void jgr_scratch_unview(JgrScratchPool *,FILE *);
/* Configure the next outermost setup scope. Normal conversion may retain up
 * to 18 MiB while preserving allocator headroom; replay keeps the small pool.
 * Changing this with live scopes/streams is rejected. */
int jgr_scratch_setup_phase(unsigned phase);
/* Proven allocator space, including the unused portion of PSP's reserved
 * program heap. Host builds return SIZE_MAX (no platform heap estimate). */
size_t jgr_scratch_heap_available(void);
/* The setup worker is synchronous. Nested converter scopes share one budget
 * and one handle cache; close a scope's own streams before leaving it. */
JgrScratchPool *jgr_scratch_begin(void);
int jgr_scratch_end(void);
#endif
