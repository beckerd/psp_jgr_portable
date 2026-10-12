#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "message_async_psp.h"
static pthread_mutex_t gate_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_condition=PTHREAD_COND_INITIALIZER;
static int gate_reference=-1,gate_used,gate_entered,gate_release;
static atomic_uint file_opens,file_closes,file_payload_reads;
static atomic_int fail_read=-1,fail_open=-1;
static atomic_uint heap_limit;
static _Thread_local int reading_reference=-1;
static FILE *radio_fopen(const char *name,const char *mode)
{
    int reference;
    if(sscanf(name,"messagevoice%d.adx",&reference)==1){
        assert(test_radio_thread && !test_scene_held);reading_reference=reference;
        if(atomic_load(&fail_open)==reference)return NULL;
        atomic_fetch_add(&file_opens,1);
    }
    return fopen(name,mode);
}
static size_t radio_fread(void *p,size_t size,size_t count,FILE *f)
{
    if(reading_reference>=0){
        assert(test_radio_thread && !test_scene_held);
        if(size==18){
            atomic_fetch_add(&file_payload_reads,1);
            pthread_mutex_lock(&gate_lock);
            if(reading_reference==gate_reference && !gate_used){
                gate_used=gate_entered=1;pthread_cond_broadcast(&gate_condition);
                while(!gate_release)pthread_cond_wait(&gate_condition,&gate_lock);
            }
            pthread_mutex_unlock(&gate_lock);
            if(atomic_load(&fail_read)==reading_reference)return 0;
        }
    }
    return fread(p,size,count,f);
}
static int radio_fclose(FILE *f)
{if(reading_reference>=0){assert(test_radio_thread && !test_scene_held);atomic_fetch_add(&file_closes,1);reading_reference=-1;}return fclose(f);}
static void *radio_malloc(size_t size)
{unsigned limit=atomic_load(&heap_limit);return limit && size>limit ? NULL:malloc(size);}
#define fopen radio_fopen
#define fread radio_fread
#define fclose radio_fclose
#define malloc radio_malloc
#include "../src/audio.c"
#undef fopen
#undef fread
#undef fclose
#undef malloc
#if JGR_AUDIO_ME
static void during_job(void);
#define AUDIO_ME_TEST_HOOK during_job
#include "audio_me_transport_test.inc"
#endif
void stream_init(const char *p){(void)p;}
void stream_shutdown(void){}
int stream_active(void){return 0;}
int stream_fetch(short *l,short *r){*l=*r=0;return 0;}
static void put_be32(unsigned char *p,unsigned v)
{p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static void fixture(unsigned reference,unsigned blocks,int malformed)
{
    char name[40];snprintf(name,sizeof name,"messagevoice%03u.adx",reference);FILE *f=fopen(name,"wb");assert(f);
    unsigned char h[36]={0x80,0,0,32,3,18,4,1,0,0,0xac,0x44,0,0,0,0,1,0xf4,3,0};
    put_be32(h+12,malformed ? 0xffffff:blocks*32);assert(fwrite(h,1,36,f)==36);
    unsigned char block[18]={0};block[1]=1;for(unsigned i=2;i<18;i++)block[i]=(unsigned char)(0x11+reference);
    for(unsigned i=0;i<blocks;i++)assert(fwrite(block,18,1,f)==1);assert(!fclose(f));
}
static void gate(int reference)
{pthread_mutex_lock(&gate_lock);gate_reference=reference;gate_used=gate_entered=gate_release=0;pthread_mutex_unlock(&gate_lock);}
static void release_gate(void)
{pthread_mutex_lock(&gate_lock);gate_release=1;pthread_cond_broadcast(&gate_condition);pthread_mutex_unlock(&gate_lock);}
static void wait_gate(void)
{
    for(unsigned i=0;i<5000;i++){
        pthread_mutex_lock(&gate_lock);int entered=gate_entered;pthread_mutex_unlock(&gate_lock);
        if(entered)return;sceKernelDelayThread(1000);
    }assert(!"worker did not reach read gate");
}
static void wait_ready(int reference)
{
    for(unsigned i=0;i<5000;i++){
        lock_scene();int ready=message_reference==reference && message_voice.ready;int error=message_load_error;unlock_scene();
        assert(!error);if(ready)return;sceKernelDelayThread(1000);
    }assert(!"worker did not prepare requested message");
}
static void wait_closed(void)
{
    for(unsigned i=0;i<5000;i++){if(atomic_load(&file_opens)==atomic_load(&file_closes))return;sceKernelDelayThread(1000);}
    assert(!"cancelled worker retained file");
}
static void wait_error(int expected)
{
    for(unsigned i=0;i<5000;i++){
        lock_scene();int error=message_load_error;unlock_scene();
        if(error){assert(error==expected);return;}sceKernelDelayThread(1000);
    }assert(!"worker did not publish failure");
}
static void assert_silent(void)
{lock_scene();assert(!message_playing.active && !message_image && !message_playing_image);unlock_scene();}
static void test_alert_and_late_start(void)
{
    assert(!audio_message_request(1));wait_ready(1);audio_message_collect();
    assert(!message_playing.active);assert(!audio_message_voice(1));
    short out[2048];audio_cb(out,1024,NULL);assert(message_playing.position==1024);
    gate(0);assert(!audio_message_request(0));wait_gate();
    unsigned before=message_playing.position;audio_cb(out,1024,NULL);assert(message_playing.position>before);
    assert(!audio_message_voice(0));assert(!message_playing.active); /* Alert boundary, reader remains blocked. */
    audio_message_collect();assert(!message_playing.active);
    release_gate();wait_ready(0);assert(!message_playing.active);audio_message_collect();
    assert(message_playing.active && !message_playing.position && message_playing.samples==1600*32);
    /* Real decoding agrees sample-for-sample with one contiguous source. */
    FILE *f=fopen("messagevoice000.adx","rb");assert(f);unsigned n=36+1600*18;
    unsigned char *data=malloc(n);assert(data && fread(data,1,n,f)==n);fclose(f);
    MovieVoice expected;assert(movie_voice_bind(&expected,data,n));expected.active=1;
    while(expected.active){assert(message_next()==movie_voice_next(&expected));
        if(!(expected.position%2048))audio_message_collect();}
    free(data);audio_free_message();assert_silent();
}
static void test_replacement_and_free(void)
{
    gate(0);assert(!audio_message_request(0));wait_gate();assert(!audio_message_voice(0));
    assert(!audio_message_request(1));assert(audio_message_voice(0)<0);release_gate();wait_ready(1);
    assert(!message_playing.active && !audio_message_voice(1));assert(message_playing.samples==128*32);
    audio_free_message();assert_silent();
    gate(0);assert(!audio_message_request(0));wait_gate();assert(!audio_message_voice(0));
    audio_free_message();assert_silent();release_gate();wait_closed();audio_message_collect();assert_silent();
}
static void test_countdown_ownership(void)
{
    countdown_audio.cue[2]=(MovieVoice){.ready=1,.samples=32};
    gate(0);assert(!audio_message_request(0));wait_gate();
    assert(!audio_countdown_voice(181));release_gate();wait_ready(0);
    assert(countdown_audio.playing.active && !message_playing.active);
    assert(!audio_message_voice(0));assert(message_playing.active && !countdown_audio.playing.active);
    audio_free_message();gate(0);assert(!audio_message_request(0));wait_gate();
    assert(!audio_message_voice(0));assert(!audio_countdown_voice(181));release_gate();wait_closed();
    audio_message_collect();assert(countdown_audio.playing.active);assert_silent();
    assert(!audio_message_voice(-1));assert(!countdown_audio.playing.active);
}
static void test_failures(void)
{
    atomic_store(&fail_read,0);assert(!audio_message_request(0));wait_error(-5);
    assert(audio_message_voice(0)<0);assert_silent();atomic_store(&fail_read,-1);
    assert(!audio_message_request(2));wait_error(-5);assert_silent();
    atomic_store(&fail_open,0);assert(!audio_message_request(0));wait_error(-2);assert_silent();atomic_store(&fail_open,-1);
    atomic_store(&heap_limit,4096);assert(!audio_message_request(0));wait_error(-4);assert_silent();atomic_store(&heap_limit,0);
    gate(0);assert(!audio_message_request(0));wait_gate();assert(!audio_message_voice(0));
    atomic_store(&fail_read,0);release_gate();wait_error(-5);audio_message_collect();
    unsigned position;int active,error;audio_message_info(&position,&active,&error);assert(!active && error);
    atomic_store(&fail_read,-1);audio_free_message();
}
static void test_resume(void)
{
    unsigned opened=atomic_load(&file_opens);gate(0);assert(!audio_message_request(0));wait_gate();
    assert(!audio_message_voice(0));audio_note_suspend();audio_note_resume();release_gate();wait_ready(0);
    assert(atomic_load(&file_opens)==opened+2);audio_message_collect();assert(message_playing.active);
    audio_free_message();
}
#if JGR_AUDIO_ME
static int retirement_phase;
static void during_job(void)
{
    if(!retirement_phase)return;retirement_phase=0;
    unsigned char pcm[4096];memcpy(pcm,mix_job->pcm,sizeof pcm);
    gate(0);audio_free_message();assert(audio_memory_borrowed && audio_retired);
    assert(!audio_message_request(0));wait_gate();assert(!audio_message_voice(0));
    assert(audio_mix_job_execute(mix_job)==transport_reply);assert(!memcmp(pcm,mix_job->pcm,sizeof pcm));
    /* Cancel private loader buffers while borrowed published ones remain
       pinned; the worker must not mutate the ME retirement list. */
    audio_free_message();release_gate();wait_closed();
    assert(audio_mix_job_execute(mix_job)==transport_reply);assert(!memcmp(pcm,mix_job->pcm,sizeof pcm));
}
static void test_me_retirement(void)
{
    assert(!audio_message_request(1));wait_ready(1);assert(!audio_message_voice(1));
    short out[2048];retirement_phase=1;audio_cb(out,1024,NULL);
    assert(!retirement_phase && !audio_retired && !audio_memory_borrowed);assert_silent();
}
#endif
int main(void)
{
    fixture(0,1600,0);fixture(1,128,0);fixture(2,4,1);
    test_thread_failure=1;audio_init(NULL);audio_reverb_enable(0);
    assert(audio_message_request(0)==-6 && !atomic_load(&file_opens));test_thread_failure=0;
    test_alert_and_late_start();test_replacement_and_free();test_countdown_ownership();test_failures();test_resume();
#if JGR_AUDIO_ME
    test_me_retirement();
#endif
    gate(0);assert(!audio_message_request(0));wait_gate();assert(!audio_shutdown());
    release_gate();int done=0;for(unsigned i=0;i<5000 && !done;i++){done=audio_shutdown();if(!done)sceKernelDelayThread(1000);}
    assert(done && !test_thread_created && atomic_load(&file_opens)==atomic_load(&file_closes));
    assert(atomic_load(&file_payload_reads)>15);
    puts("radio async: no main-thread I/O; alert, replacement, cancellation, ownership, errors, resume and shutdown passed");return 0;
}
