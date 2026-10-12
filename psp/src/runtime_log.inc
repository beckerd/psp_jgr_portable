/* Main-thread logging: no Memory Stick writes while normal play runs. */
#include "deferred_log.h"
#include "release_report.h"
static DeferredLog runtime_log_queue;
static int runtime_log_deferred,runtime_verbose;
static unsigned runtime_log_appends;
static int runtime_log_disk(void *context,const void *data,unsigned bytes)
{
    int fd=*(const int *)context;
    if(!spike_enabled)return sceIoWrite(fd,data,bytes);
    unsigned start=sceKernelGetSystemTimeLow();int result=sceIoWrite(fd,data,bytes);
    spike_log_us+=sceKernelGetSystemTimeLow()-start;spike_log_calls++;
    if(result>0 && (unsigned)result<=bytes)spike_log_bytes+=(unsigned)result;
    if(result<0 || (unsigned)result!=bytes)spike_log_errors++;
    return result;
}
static void runtime_log_drain(void)
{
    if(runtime_log_deferred || logfd<0)return;
    unsigned phase=hang_mark(HANG_LOG_IO);
    deferred_log_drain(&runtime_log_queue,runtime_log_disk,&logfd);
    hang_mark(phase);
}
static int runtime_log_write(int fd,const void *data,unsigned bytes)
{
    /* Failure history is independent of disk availability and queue overflow. */
    if(fd==logfd)release_report_log(data,bytes);
    if(fd<0 || fd!=logfd)return -1;
    if(!runtime_log_deferred && bytes>deferred_log_space(&runtime_log_queue))runtime_log_drain();
    int result=deferred_log_append(&runtime_log_queue,data,bytes);
    if(runtime_log_deferred)runtime_log_appends++;
    else runtime_log_drain();
    return result;
}
static char *log_batch;
static unsigned log_batch_used;
#define LOG_BATCH_SIZE 4096
static void runtime_log_flush(void)
{
    if(log_batch_used){unsigned phase=hang_mark(HANG_LOG_IO);runtime_log_write(logfd,log_batch,log_batch_used);hang_mark(phase);}
    log_batch_used=0;runtime_log_drain();
}
static void runtime_log(const char *format,...)
{
    char text[512];va_list args;va_start(args,format);int n=vsnprintf(text,sizeof text,format,args);va_end(args);
    if(n<=0)return;
    unsigned length=n<(int)sizeof text ? n:sizeof text-1;
    if(n>=(int)sizeof text)text[length-1]='\n';
    if(log_batch) {
        if(log_batch_used+length>LOG_BATCH_SIZE)runtime_log_flush();
        memcpy(log_batch+log_batch_used,text,length);log_batch_used+=length;
    } else {unsigned phase=hang_mark(HANG_LOG_IO);runtime_log_write(logfd,text,length);hang_mark(phase);}
}

/* Explicit safe points only: diagnostics have already latched their times;
   normal shutdown has stopped producing gameplay frames. Overflow never
   forces I/O. Losses are reported, rather than silently growing the buffer. */
static void runtime_log_checkpoint(const char *reason)
{
    if(logfd<0)return; /* Neither queued text nor loss notices were delivered. */
    runtime_log_flush();
    unsigned queued=runtime_log_queue.used;
    runtime_log_deferred=0;runtime_log_drain();
    if(runtime_log_queue.used)return; /* retain the suffix for a later retry */
    unsigned bytes=runtime_log_queue.dropped_bytes,messages=runtime_log_queue.dropped_messages;
    runtime_log("LOG_DEFERRED reason=%s queued_bytes=%u remaining_bytes=0 dropped_bytes=%u dropped_messages=%u deferred_calls=%u capacity=%u\n",
        reason,queued,bytes,messages,runtime_log_appends,DEFERRED_LOG_CAPACITY);
    /* A caller may still own a formatting batch. Its loss notice must reach
       the queue/disk before an empty queue can prove delivery. */
    if(log_batch_used)runtime_log_flush();
    if(!runtime_log_queue.used)deferred_log_ack_drops(&runtime_log_queue,bytes,messages);
}
