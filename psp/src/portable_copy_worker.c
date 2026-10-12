/* Setup-only audio copying. This worker owns every stream it touches; no
 * converter globals, shared scratch streams, plan cursor or UI callbacks. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#include "portable_copy_worker.h"
#ifdef __EMSCRIPTEN__
/* The browser converter already runs off the UI thread. Its filesystem awaits
 * browser file operations in that one Worker, so the optional PSP/host
 * copying thread is unavailable. A failed create explicitly selects the
 * installer's existing serial copy path; conversion and checks stay shared. */
#include <errno.h>
#include <stdio.h>
JgrCopyWorker *jgr_copy_worker_create(const JgrDisc *disc,const JgrCopyTask *tasks,
    unsigned count,int start_paused,char *error,size_t error_size)
{
    (void)disc;(void)tasks;(void)count;(void)start_paused;
    if(error && error_size)snprintf(error,error_size,"Browser copying runs in the converter worker");
    errno=ENOSYS;return NULL;
}
int jgr_copy_worker_progress(JgrCopyWorker *w,JgrCopyProgress *out)
{(void)w;(void)out;return -1;}
int jgr_copy_worker_result(JgrCopyWorker *w,unsigned i,JgrCopyCompletion *out)
{(void)w;(void)i;(void)out;return -1;}
void jgr_copy_worker_pause(JgrCopyWorker *w,int paused){(void)w;(void)paused;}
int jgr_copy_worker_wait_paused(JgrCopyWorker *w){(void)w;return -1;}
void jgr_copy_worker_cancel(JgrCopyWorker *w){(void)w;}
int jgr_copy_worker_wait(JgrCopyWorker *w){(void)w;return -1;}
void jgr_copy_worker_destroy(JgrCopyWorker *w){(void)w;}
#else
#include "portable_crc32.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __PSP__
#include <pspkernel.h>
#include <malloc.h>
#else
#include <pthread.h>
#include <sys/time.h>
#endif
#define OUTPUT_BUFFER_BYTES (64u*1024u)
#define WORKER_STACK_BYTES (32u*1024u)
struct JgrCopyWorker {
    JgrDisc *disc;
    const JgrCopyTask *tasks;
    JgrCopyCompletion *results;
    unsigned count,completed;
    int finished,status,pause_requested,paused,cancel;
    unsigned char *buffer,*output_buffer;
#ifdef __PSP__
    SceUID thread,lock,worker_event,main_event;
#else
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t worker_event,main_event;
#endif
};
static uint64_t clock_us(void)
{
#ifdef __PSP__
    return sceKernelGetSystemTimeWide();
#else
    struct timeval now;gettimeofday(&now,NULL);
    return (uint64_t)now.tv_sec*1000000u+now.tv_usec;
#endif
}
#ifdef __PSP__
static void lock(JgrCopyWorker *w){sceKernelWaitSema(w->lock,1,NULL);}
static void unlock(JgrCopyWorker *w){sceKernelSignalSema(w->lock,1);}
static void wake_main(JgrCopyWorker *w){sceKernelSignalSema(w->main_event,1);}
static void wake_worker(JgrCopyWorker *w){sceKernelSignalSema(w->worker_event,1);}
static void wait_main(JgrCopyWorker *w){unlock(w);sceKernelWaitSema(w->main_event,1,NULL);lock(w);}
static void wait_worker(JgrCopyWorker *w){unlock(w);sceKernelWaitSema(w->worker_event,1,NULL);lock(w);}
#else
static void lock(JgrCopyWorker *w){pthread_mutex_lock(&w->lock);}
static void unlock(JgrCopyWorker *w){pthread_mutex_unlock(&w->lock);}
static void wake_main(JgrCopyWorker *w){pthread_cond_broadcast(&w->main_event);}
static void wake_worker(JgrCopyWorker *w){pthread_cond_broadcast(&w->worker_event);}
static void wait_main(JgrCopyWorker *w){pthread_cond_wait(&w->main_event,&w->lock);}
static void wait_worker(JgrCopyWorker *w){pthread_cond_wait(&w->worker_event,&w->lock);}
#endif
/* Returns only when resumed or cancelled. Acknowledgement always precedes
 * waiting, and no source/output call can be active while paused is true. */
static int checkpoint(JgrCopyWorker *w)
{
    lock(w);
    while(w->pause_requested && !w->cancel){w->paused=1;wake_main(w);wait_worker(w);}
    if(w->paused){w->paused=0;wake_main(w);}
    int cancel=w->cancel;unlock(w);return cancel;
}
static int failure(JgrCopyCompletion *r,int status,const char *detail)
{
    r->status=status;r->system_error=errno;
    snprintf(r->detail,sizeof r->detail,"%s",detail);return status;
}
static void copy_task(JgrCopyWorker *w,const JgrCopyTask *task,JgrCopyCompletion *r)
{
    FILE *out=NULL;struct stat st;uint32_t crc=0xffffffffu;
    memset(r,0,sizeof *r);r->tag=task->tag;
    if(checkpoint(w)){failure(r,JGR_COPY_CANCELLED,"Audio copy cancelled");return;}
#ifdef __PSP__
    int exists=stat(task->temporary,&st);
#else
    int exists=lstat(task->temporary,&st);
#endif
    if((!exists && !S_ISREG(st.st_mode)) || (exists && errno!=ENOENT)){
        failure(r,JGR_COPY_IO,"Unsafe or unreadable audio temporary file");return;
    }
    out=fopen(task->temporary,"w+b");
    if(!out){failure(r,JGR_COPY_IO,"Cannot open audio temporary file");return;}
    if(setvbuf(out,(char *)w->output_buffer,_IOFBF,OUTPUT_BUFFER_BYTES)){
        failure(r,JGR_COPY_IO,"Cannot buffer audio output");goto done;
    }
    for(uint32_t at=0;at<task->bytes;){
        if(checkpoint(w)){failure(r,JGR_COPY_CANCELLED,"Audio copy cancelled");goto done;}
        unsigned n=task->bytes-at;if(n>JGR_COPY_WORKER_PAYLOAD_BYTES)n=JGR_COPY_WORKER_PAYLOAD_BYTES;
        uint64_t started=clock_us();errno=0;
        int bad=jgr_disc_read_sequential(w->disc,&task->source,at,w->buffer,n,JGR_COPY_WORKER_BUFFER_BYTES);
        r->read_us+=clock_us()-started;
        if(bad){failure(r,JGR_COPY_DISC,jgr_disc_error(w->disc));goto done;}
        started=clock_us();
        crc=jgr_crc32_add(crc,w->buffer,n);
        r->hash_us+=clock_us()-started;
        started=clock_us();errno=0;
        size_t written=fwrite(w->buffer,1,n,out);r->write_us+=clock_us()-started;
        if(written!=n){failure(r,JGR_COPY_IO,"Cannot write audio temporary file");goto done;}
        r->bytes+=n;r->chunks++;at+=n;
    }
    if(checkpoint(w)){failure(r,JGR_COPY_CANCELLED,"Audio copy cancelled");goto done;}
    r->crc32=crc^0xffffffffu;
    if(r->crc32!=task->crc32){errno=0;failure(r,JGR_COPY_CHECKSUM,"Audio source CRC does not match recipe");goto done;}
done:
    if(out){
        uint64_t started=clock_us();errno=0;
        if(fflush(out) && !r->status)failure(r,JGR_COPY_IO,"Cannot flush audio temporary file");
        if(fclose(out) && !r->status)failure(r,JGR_COPY_IO,"Cannot close audio temporary file");
        r->write_us+=clock_us()-started;
    }
}
static void run(JgrCopyWorker *w)
{
    for(unsigned i=0;i<w->count;i++){
        JgrCopyCompletion result;copy_task(w,w->tasks+i,&result);
        lock(w);w->results[i]=result;w->completed=i+1;
        if(result.status){w->status=result.status;w->finished=1;}
        wake_main(w);int stop=w->finished;unlock(w);
        if(stop)break;
    }
    lock(w);w->finished=1;w->paused=0;wake_main(w);unlock(w);
}
#ifdef __PSP__
static int entry(SceSize bytes,void *args)
{(void)bytes;JgrCopyWorker *w=*(JgrCopyWorker **)args;run(w);return 0;}
#else
static void *entry(void *arg){run(arg);return NULL;}
#endif
JgrCopyWorker *jgr_copy_worker_create(const JgrDisc *disc,const JgrCopyTask *tasks,
    unsigned count,int start_paused,char *error,size_t error_size)
{
    const char *why="Cannot start audio copy worker";
    if(!disc || !tasks || !count || count>JGR_COPY_WORKER_MAX_TASKS)goto invalid;
    for(unsigned i=0;i<count;i++)if(!tasks[i].temporary || !*tasks[i].temporary ||
        tasks[i].source.size!=tasks[i].bytes)goto invalid;
    JgrCopyWorker *w=calloc(1,sizeof *w);if(!w)goto invalid;
    w->tasks=tasks;w->count=count;w->pause_requested=!!start_paused;
    w->results=calloc(count,sizeof *w->results);
#ifdef __PSP__
    w->thread=w->lock=w->worker_event=w->main_event=-1;
    w->buffer=memalign(64,JGR_COPY_WORKER_BUFFER_BYTES);
#else
    w->buffer=malloc(JGR_COPY_WORKER_BUFFER_BYTES);
#endif
    w->output_buffer=malloc(OUTPUT_BUFFER_BYTES);
    if(!w->results || !w->buffer || !w->output_buffer){why="Not enough memory for audio worker";goto bad;}
    if(jgr_disc_clone(&w->disc,disc,error,error_size))goto clone_bad;
#ifdef __PSP__
    w->lock=sceKernelCreateSema("jgr_copy_lock",0,1,1,NULL);
    w->worker_event=sceKernelCreateSema("jgr_copy_work",0,0,65535,NULL);
    w->main_event=sceKernelCreateSema("jgr_copy_done",0,0,65535,NULL);
    if(w->lock<0 || w->worker_event<0 || w->main_event<0)goto bad;
    w->thread=sceKernelCreateThread("jgr_audio_copy",entry,0x1c,WORKER_STACK_BYTES,0,NULL);
    if(w->thread<0 || sceKernelStartThread(w->thread,sizeof w,&w)<0)goto bad;
#else
    int mutex_ok=0,worker_ok=0,main_ok=0;
    if(!pthread_mutex_init(&w->lock,NULL))mutex_ok=1;else goto sync_bad;
    if(!pthread_cond_init(&w->worker_event,NULL))worker_ok=1;else goto sync_bad;
    if(!pthread_cond_init(&w->main_event,NULL))main_ok=1;else goto sync_bad;
    if(pthread_create(&w->thread,NULL,entry,w))goto sync_bad;
#endif
    return w;
#ifndef __PSP__
sync_bad:
    if(main_ok)pthread_cond_destroy(&w->main_event);
    if(worker_ok)pthread_cond_destroy(&w->worker_event);
    if(mutex_ok)pthread_mutex_destroy(&w->lock);
#endif
bad:
    if(error && error_size)snprintf(error,error_size,"%s",why);
clone_bad:
#ifdef __PSP__
    if(w->thread>=0)sceKernelDeleteThread(w->thread);
    if(w->main_event>=0)sceKernelDeleteSema(w->main_event);
    if(w->worker_event>=0)sceKernelDeleteSema(w->worker_event);
    if(w->lock>=0)sceKernelDeleteSema(w->lock);
#endif
    jgr_disc_close(w->disc);free(w->results);free(w->buffer);free(w->output_buffer);free(w);return NULL;
invalid:
    if(error && error_size)snprintf(error,error_size,"%s",why);
    return NULL;
}
int jgr_copy_worker_progress(JgrCopyWorker *w,JgrCopyProgress *out)
{
    if(!w || !out)return -1;
    lock(w);out->completed=w->completed;out->total=w->count;
    out->finished=w->finished;out->paused=w->paused;out->status=w->status;unlock(w);return 0;
}
int jgr_copy_worker_result(JgrCopyWorker *w,unsigned index,JgrCopyCompletion *out)
{
    if(!w || !out || index>=w->count)return -1;
    lock(w);int ready=index<w->completed;if(ready)*out=w->results[index];unlock(w);return ready;
}
void jgr_copy_worker_pause(JgrCopyWorker *w,int paused)
{if(w){lock(w);w->pause_requested=!!paused;wake_worker(w);unlock(w);}}
int jgr_copy_worker_wait_paused(JgrCopyWorker *w)
{
    if(!w)return -1;
    lock(w);
    while(w->pause_requested && !w->paused && !w->finished)wait_main(w);
    int result=w->finished?w->status:0;unlock(w);return result;
}
void jgr_copy_worker_cancel(JgrCopyWorker *w)
{if(w){lock(w);w->cancel=1;wake_worker(w);unlock(w);}}
int jgr_copy_worker_wait(JgrCopyWorker *w)
{
    if(!w)return -1;
    lock(w);while(!w->finished)wait_main(w);
    int result=w->status;unlock(w);return result;
}
void jgr_copy_worker_destroy(JgrCopyWorker *w)
{
    if(!w)return;
    jgr_copy_worker_cancel(w);
#ifdef __PSP__
    sceKernelWaitThreadEnd(w->thread,NULL);sceKernelDeleteThread(w->thread);
    sceKernelDeleteSema(w->main_event);sceKernelDeleteSema(w->worker_event);sceKernelDeleteSema(w->lock);
#else
    pthread_join(w->thread,NULL);
    pthread_cond_destroy(&w->main_event);pthread_cond_destroy(&w->worker_event);pthread_mutex_destroy(&w->lock);
#endif
    jgr_disc_close(w->disc);free(w->results);free(w->buffer);free(w->output_buffer);free(w);
}
#endif
