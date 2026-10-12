#include "gameplay_audio_preload.h"
#include "audio.h"
#include <stdlib.h>
#ifdef __PSP__
#include <pspkernel.h>
#elif defined(JGR_GAMEPLAY_PRELOAD_TEST)
#include <pthread.h>
#endif

#if defined(__PSP__) || defined(JGR_GAMEPLAY_PRELOAD_TEST)
static GameplayAudioPreload result;
static void *reserve;
static int active;
#ifdef __PSP__
static SceUID thread=-1;
#else
static pthread_t thread;
#endif
static void load_banks(void)
{
    result.world_error=result.stage==4 ? 0:audio_load_world(result.stage);
    result.player_error=audio_load_player(result.character);
    /* Required banks have precedence over optional encounter residency.
       A failed attempt is retried after the scene releases its memory. */
    result.enemy_error=result.world_error || result.player_error ? -1:audio_preload_enemy_attacks(result.kinds);
    free(reserve);reserve=NULL;
}
#ifdef __PSP__
static int worker(SceSize size,void *args)
{(void)size;(void)args;load_banks();return 0;}
#else
static void *worker(void *args)
{(void)args;load_banks();return NULL;}
#endif
int gameplay_audio_preload_begin(int stage,int character,uint64_t kinds)
{
    /* Do not replace a live job or its immutable mission selection. */
    if(active || stage<0 || (unsigned)character>=16)return 0;
    /* EVENT can still need small allocations while drawing. This is a
       real allocation, independent of malloc's current arena statistics;
       if it cannot fit, preserve the existing post-scene load path. */
    reserve=malloc(512u*1024u);if(!reserve)return 0;
    result=(GameplayAudioPreload){.stage=stage,.character=character,.kinds=kinds};
#ifdef __PSP__
    /* Below output (0x12) and music (0x18), above rendering (0x20).
       A loader below main can be preempted while holding scene_lock or
       newlib's heap lock, stalling the higher-priority voice callback.
       Match the other I/O readers: disk waits still yield to rendering. */
    thread=sceKernelCreateThread("gameplay_audio",worker,0x1c,16384,0,NULL);
    if(thread<0 || sceKernelStartThread(thread,0,NULL)<0) {
        if(thread>=0)sceKernelDeleteThread(thread);
        thread=-1;free(reserve);reserve=NULL;return 0;
    }
#else
    if(pthread_create(&thread,NULL,worker,NULL)){free(reserve);reserve=NULL;return 0;}
#endif
    active=1;return 1;
}
int gameplay_audio_preload_finish(GameplayAudioPreload *out)
{
    if(!active)return 0;
#ifdef __PSP__
    sceKernelWaitThreadEnd(thread,NULL);sceKernelDeleteThread(thread);thread=-1;
#else
    pthread_join(thread,NULL);
#endif
    if(out)*out=result;
    active=0;return 1;
}
#else
int gameplay_audio_preload_begin(int stage,int character,uint64_t kinds)
{(void)stage;(void)character;(void)kinds;return 0;}
int gameplay_audio_preload_finish(GameplayAudioPreload *out){(void)out;return 0;}
#endif
