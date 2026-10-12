/* Thread/lifetime faults use synthetic loaders; no game samples are needed. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#ifdef __PSP__
#include <pspkernel.h>
#endif

static pthread_t main_thread;
static pthread_mutex_t gate_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond=PTHREAD_COND_INITIALIZER;
static int blocked,entered,release_read,fail_alloc,fail_thread,errors[3];
static unsigned calls[3],order[3],order_count,reserves;
static int wanted_stage,wanted_character;
static uint64_t wanted_kinds;

static void *reserve_malloc(size_t bytes)
{
    assert(bytes==512u*1024u && !reserves);
    if(fail_alloc)return NULL;
    void *p=malloc(bytes);assert(p);reserves++;return p;
}
static void reserve_free(void *p)
{assert(p && reserves==1);reserves--;free(p);}
#ifndef __PSP__
static int create_thread(pthread_t *thread,const pthread_attr_t *attr,
                         void *(*entry)(void *),void *arg)
{return fail_thread ? EAGAIN:pthread_create(thread,attr,entry,arg);}
#else
/* Exercise the production PSP branch with real host threads. In particular
   the loader must outrank rendering while remaining below music/output. */
static pthread_t psp_thread;
static int (*psp_entry)(SceSize,void *);
static int created,started,fail_start;
static void *psp_run(void *unused){(void)unused;psp_entry(0,NULL);return NULL;}
int sceKernelCreateThread(const char *name,int (*entry)(SceSize,void *),int priority,
                          unsigned stack,unsigned attr,void *option)
{
    assert(!strcmp(name,"gameplay_audio") && priority>0x18 && priority<0x20);
    assert(stack==16384 && !attr && !option && !created);
    if(fail_thread)return -1;
    psp_entry=entry;created=1;return 101;
}
int sceKernelStartThread(int id,unsigned bytes,void *args)
{
    assert(id==101 && created && !started && !bytes && !args);
    if(fail_start)return -1;
    int error=pthread_create(&psp_thread,NULL,psp_run,NULL);started=!error;return error ? -1:0;
}
int sceKernelWaitThreadEnd(int id,unsigned *timeout)
{assert(id==101 && started && !timeout);pthread_join(psp_thread,NULL);started=0;return 0;}
int sceKernelDeleteThread(int id){assert(id==101 && created && !started);created=0;return 0;}
#endif
#define JGR_GAMEPLAY_PRELOAD_TEST 1
#define malloc reserve_malloc
#define free reserve_free
#ifndef __PSP__
#define pthread_create create_thread
#endif
#include "gameplay_audio_preload.c"
#undef malloc
#undef free
#undef pthread_create

static int load(unsigned bank)
{
    assert(!pthread_equal(pthread_self(),main_thread));
    assert(reserves==1);calls[bank]++;order[order_count++]=bank;
    pthread_mutex_lock(&gate_mutex);
    if(blocked && !entered) {
        entered=1;pthread_cond_broadcast(&gate_cond);
        while(!release_read)pthread_cond_wait(&gate_cond,&gate_mutex);
    }
    pthread_mutex_unlock(&gate_mutex);
    return errors[bank];
}
int audio_load_world(int stage){assert(stage==wanted_stage);return load(0);}
int audio_load_player(int character){assert(character==wanted_character);return load(1);}
int audio_preload_enemy_attacks(uint64_t kinds){assert(kinds==wanted_kinds);return load(2);}

static void reset(int stage,int character,uint64_t kinds)
{
    assert(!gameplay_audio_preload_finish(NULL) && !reserves);
    wanted_stage=stage;wanted_character=character;wanted_kinds=kinds;
    blocked=entered=release_read=fail_alloc=fail_thread=0;
    memset(errors,0,sizeof errors);memset(calls,0,sizeof calls);order_count=0;
}
int main(void)
{
    main_thread=pthread_self();
    reset(1,0,UINT64_C(0x100000000001));blocked=1;
    assert(gameplay_audio_preload_begin(wanted_stage,wanted_character,wanted_kinds));
    pthread_mutex_lock(&gate_mutex);
    while(!entered)pthread_cond_wait(&gate_cond,&gate_mutex);
    /* The caller has returned and can render while a disk read is pending.
       A second request cannot overwrite the current character/mission. */
    for(unsigned frame=0;frame<100;frame++)
        assert(!gameplay_audio_preload_begin(3,2,UINT64_MAX));
    release_read=1;pthread_cond_broadcast(&gate_cond);pthread_mutex_unlock(&gate_mutex);
    GameplayAudioPreload info;
    assert(gameplay_audio_preload_finish(&info));assert(!reserves);
    assert(info.stage==1 && info.character==0 && info.kinds==wanted_kinds);
    assert(!info.world_error && !info.player_error && !info.enemy_error);
    assert(order_count==3 && order[0]==0 && order[1]==1 && order[2]==2);
    assert(!gameplay_audio_preload_finish(&info));

    reset(4,15,0);assert(gameplay_audio_preload_begin(4,15,0));
    assert(gameplay_audio_preload_finish(&info));
    assert(!calls[0] && calls[1]==1 && calls[2]==1 && !info.world_error);

    /* Low memory and thread creation failure do no loading and leak no
       reserve. The main thread can use its normal synchronous path. */
    reset(2,3,7);fail_alloc=1;assert(!gameplay_audio_preload_begin(2,3,7));
    assert(!gameplay_audio_preload_finish(NULL) && !reserves && !order_count);
    fail_alloc=0;fail_thread=1;assert(!gameplay_audio_preload_begin(2,3,7));
    assert(!gameplay_audio_preload_finish(NULL) && !reserves && !order_count);
    fail_thread=0;
#ifdef __PSP__
    fail_start=1;assert(!gameplay_audio_preload_begin(2,3,7));
    assert(!created && !started && !reserves);fail_start=0;
#endif
    assert(!gameplay_audio_preload_begin(2,16,7));
    assert(!gameplay_audio_preload_begin(-1,0,7));

    /* Mandatory failures suppress optional warming; preserve exact errors
       so the caller can retry after scene memory is released. */
    for(unsigned failed=0;failed<3;failed++) {
        reset(3,6,UINT64_MAX);errors[failed]=-7-(int)failed;
        assert(gameplay_audio_preload_begin(3,6,UINT64_MAX));
        assert(gameplay_audio_preload_finish(&info));
        assert(info.world_error==errors[0] && info.player_error==errors[1]);
        assert(info.enemy_error==(failed<2 ? -1:errors[2]));
        assert(calls[2]==(failed==2));assert(!reserves);
    }
    /* Retirement/shutdown joins before the caller frees any banks. A new
       mission can then start without inheriting errors or the old request. */
    for(unsigned i=0;i<30;i++) {
        reset(5,i%16,UINT64_C(1)<<(i%64));
        assert(gameplay_audio_preload_begin(wanted_stage,wanted_character,wanted_kinds));
        assert(gameplay_audio_preload_finish(NULL));assert(!reserves && order_count==3);
    }
    puts("background reads, memory fallback, errors, retirement and reuse passed");
    return 0;
}
