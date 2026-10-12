/* Real host threads/semaphores for the production radio worker. Other audio
   tests retain their simpler PSP stubs; this header is private to this test. */
#ifndef JGR_MESSAGE_ASYNC_PSP_TEST_H
#define JGR_MESSAGE_ASYNC_PSP_TEST_H
#define JGR_AUDIO_TEST_PSPKERNEL_H
#define JGR_TEST_PSPKERNEL_H
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#include <fcntl.h>
#define PSP_O_RDONLY O_RDONLY
#define sceIoOpen open
#define sceIoRead read
#define sceIoClose close
typedef int SceUID;
typedef unsigned SceSize;
static _Thread_local int test_scene_held,test_radio_thread;
static int test_thread_failure;
typedef struct {pthread_mutex_t mutex;pthread_cond_t condition;int count,max,scene,alive;} TestAudioSema;
static TestAudioSema test_audio_semas[8];
static unsigned test_audio_sema_next;
static pthread_t test_audio_thread;
static int (*test_audio_entry)(SceSize,void *);
static int test_thread_created,test_thread_started;
static unsigned sceKernelGetSystemTimeLow(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned)((uint64_t)t.tv_sec*1000000+t.tv_nsec/1000);}
static int sceKernelDelayThread(unsigned us)
{struct timespec t={us/1000000,(us%1000000)*1000};nanosleep(&t,NULL);return 0;}
static int sceKernelCreateSema(const char *name,int attr,int count,int max,void *option)
{
    (void)attr;(void)option;unsigned id=++test_audio_sema_next;assert(id<8);
    TestAudioSema *s=test_audio_semas+id;pthread_mutex_init(&s->mutex,NULL);pthread_cond_init(&s->condition,NULL);
    s->count=count;s->max=max;s->scene=!strcmp(name,"scene_sound");s->alive=1;return (int)id;
}
static int sceKernelWaitSema(int id,int count,unsigned *timeout)
{
    assert(id>0 && id<8);TestAudioSema *s=test_audio_semas+id;assert(s->alive);
    pthread_mutex_lock(&s->mutex);int error=0;struct timespec end;
    if(timeout){clock_gettime(CLOCK_REALTIME,&end);end.tv_nsec+=(long)*timeout*1000;end.tv_sec+=end.tv_nsec/1000000000;end.tv_nsec%=1000000000;}
    while(s->count<count && !error)error=timeout ? pthread_cond_timedwait(&s->condition,&s->mutex,&end):pthread_cond_wait(&s->condition,&s->mutex);
    if(!error){s->count-=count;if(s->scene){assert(!test_scene_held);test_scene_held=1;}}
    pthread_mutex_unlock(&s->mutex);return error ? -1:0;
}
static int sceKernelSignalSema(int id,int count)
{
    TestAudioSema *s=test_audio_semas+id;assert(s->alive);pthread_mutex_lock(&s->mutex);
    if(s->scene){assert(test_scene_held);test_scene_held=0;}
    int error=s->count+count>s->max;
    if(!error){s->count+=count;pthread_cond_broadcast(&s->condition);}
    pthread_mutex_unlock(&s->mutex);return error ? -1:0;
}
static int sceKernelDeleteSema(int id)
{TestAudioSema *s=test_audio_semas+id;assert(s->alive);s->alive=0;pthread_cond_destroy(&s->condition);pthread_mutex_destroy(&s->mutex);return 0;}
static void *test_audio_run(void *unused)
{(void)unused;test_radio_thread=1;test_audio_entry(0,NULL);return NULL;}
static int sceKernelCreateThread(const char *name,int (*entry)(SceSize,void *),int priority,unsigned stack,unsigned attr,void *option)
{
    assert(!strcmp(name,"radio_reader") && priority==0x1c && stack==8192);(void)attr;(void)option;
    if(test_thread_failure)return -1;assert(!test_thread_created);test_thread_created=1;test_audio_entry=entry;return 100;
}
static int sceKernelStartThread(int id,unsigned size,void *args)
{assert(id==100 && !size && !args);int error=pthread_create(&test_audio_thread,NULL,test_audio_run,NULL);test_thread_started=!error;return error ? -1:0;}
static int sceKernelWaitThreadEnd(int id,unsigned *timeout)
{assert(id==100 && !timeout && test_thread_started);pthread_join(test_audio_thread,NULL);test_thread_started=0;return 0;}
static int sceKernelDeleteThread(int id)
{assert(id==100 && !test_thread_started);test_thread_created=0;return 0;}
static void sceKernelDcacheWritebackInvalidateRange(void *p,unsigned n){(void)p;(void)n;}
static void sceKernelDcacheInvalidateRange(void *p,unsigned n){(void)p;(void)n;}
#endif
