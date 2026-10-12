#ifndef JGR_AUDIO_MEMORY_H
#define JGR_AUDIO_MEMORY_H
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
/* Audio-owned allocations carry an intrusive retirement link. Retiring a
   bank during an ME job needs no allocation and cannot overflow a queue.
   This covers split samples AND the EventAudio container holding cursors. */
typedef union AudioAllocation {
    struct {union AudioAllocation *next;} link;
    unsigned char alignment[16];
} AudioAllocation;
static AudioAllocation *audio_retired;
static int audio_memory_borrowed;
static unsigned audio_memory_guard(void)
{
#ifdef __PSP__
    return sceKernelCpuSuspendIntr();
#else
    return 0;
#endif
}
static void audio_memory_unguard(unsigned key)
{
#ifdef __PSP__
    sceKernelCpuResumeIntr(key);
#else
    (void)key;
#endif
}
static void *audio_malloc(size_t size)
{
    if(size>SIZE_MAX-sizeof(AudioAllocation))return NULL;
    AudioAllocation *p=malloc(sizeof *p+size);return p ? p+1:NULL;
}
static void *audio_calloc(size_t count,size_t size)
{
    if(size && count>SIZE_MAX/size)return NULL;
    size*=count;void *p=audio_malloc(size);if(p)memset(p,0,size);return p;
}
static void audio_free(void *ptr)
{
    if(!ptr)return;
    AudioAllocation *p=(AudioAllocation *)ptr-1;
    unsigned key=audio_memory_guard();
    if(audio_memory_borrowed){p->link.next=audio_retired;audio_retired=p;p=NULL;}
    audio_memory_unguard(key);free(p);
}
static void audio_memory_release(void)
{
    unsigned key=audio_memory_guard();audio_memory_borrowed=0;
    AudioAllocation *p=audio_retired;audio_retired=NULL;audio_memory_unguard(key);
    while(p){AudioAllocation *next=p->link.next;free(p);p=next;}
}
#endif
