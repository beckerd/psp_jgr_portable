#ifndef JGR_DEFERRED_LOG_H
#define JGR_DEFERRED_LOG_H

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define DEFERRED_LOG_CAPACITY 8192u
enum { DEFERRED_LOG_INVALID=-1,DEFERRED_LOG_NO_PROGRESS=-2,DEFERRED_LOG_BAD_WRITE=-3 };

/* Main-thread ownership only. Appending never writes, allocates, or invokes
   callbacks. Each append is accepted in full or dropped in full, preserving
   ordered complete chunks already retained. A chunk may contain many lines. */
typedef struct {
    unsigned head,used,dropped_bytes,dropped_messages;
    unsigned char data[DEFERRED_LOG_CAPACITY];
} DeferredLog;

typedef int (*DeferredLogWriter)(void *context,const void *data,unsigned bytes);
typedef struct {unsigned written,remaining,calls;int error;} DeferredLogDrain;

static inline void deferred_log_init(DeferredLog *queue)
{if(queue)memset(queue,0,sizeof *queue);}

static inline unsigned deferred_log_space(const DeferredLog *queue)
{return queue ? DEFERRED_LOG_CAPACITY-queue->used:0;}

static inline unsigned deferred_log_add_saturated(unsigned value,unsigned increment)
{return increment>UINT_MAX-value ? UINT_MAX:value+increment;}

/* Returns the accepted byte count, zero for a dropped nonempty chunk, or
   DEFERRED_LOG_INVALID for an invalid argument. Zero-length input is a no-op.
   Loss counts saturate at UINT_MAX: a saturated value is a lower bound. */
static inline int deferred_log_append(DeferredLog *queue,const void *data,unsigned bytes)
{
    if(!queue || (!data && bytes))return DEFERRED_LOG_INVALID;
    if(bytes>deferred_log_space(queue)) {
        queue->dropped_bytes=deferred_log_add_saturated(queue->dropped_bytes,bytes);
        queue->dropped_messages=deferred_log_add_saturated(queue->dropped_messages,1u);
        return 0;
    }
    if(!bytes)return 0;
    unsigned tail=(queue->head+queue->used)%DEFERRED_LOG_CAPACITY;
    unsigned first=DEFERRED_LOG_CAPACITY-tail;
    if(first>bytes)first=bytes;
    memcpy(queue->data+tail,data,first);
    if(bytes>first)memcpy(queue->data,(const unsigned char *)data+first,bytes-first);
    queue->used+=bytes;
    return (int)bytes;
}

/* Only the caller chooses a safe point to drain. The writer must not mutate
   this queue or recursively drain it. Positive short writes retire only the
   acknowledged prefix; zero, errors and impossible lengths stop immediately
   and retain all unacknowledged bytes. At most capacity+1 calls can occur,
   even for a writer accepting one byte at a time. No loss counters are reset. */
static inline DeferredLogDrain deferred_log_drain(DeferredLog *queue,DeferredLogWriter writer,void *context)
{
    DeferredLogDrain result={0};
    if(!queue || !writer){result.error=DEFERRED_LOG_INVALID;result.remaining=queue ? queue->used:0;return result;}
    while(queue->used) {
        unsigned count=DEFERRED_LOG_CAPACITY-queue->head;
        if(count>queue->used)count=queue->used;
        int written=writer(context,queue->data+queue->head,count);result.calls++;
        if(written<=0){result.error=written ? written:DEFERRED_LOG_NO_PROGRESS;break;}
        if((unsigned)written>count){result.error=DEFERRED_LOG_BAD_WRITE;break;}
        queue->head=(queue->head+(unsigned)written)%DEFERRED_LOG_CAPACITY;
        queue->used-=(unsigned)written;result.written+=(unsigned)written;
    }
    result.remaining=queue->used;
    return result;
}

/* Acknowledge a previously reported loss snapshot only after that report
   has itself been written successfully. A failed drain/report leaves the
   counters intact. Subtracting a snapshot also preserves later losses. */
static inline void deferred_log_ack_drops(DeferredLog *queue,unsigned bytes,unsigned messages)
{
    if(!queue)return;
    queue->dropped_bytes=bytes<queue->dropped_bytes ? queue->dropped_bytes-bytes:0;
    queue->dropped_messages=messages<queue->dropped_messages ? queue->dropped_messages-messages:0;
}

#endif
