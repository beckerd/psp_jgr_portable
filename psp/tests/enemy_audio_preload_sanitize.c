/* Exercise production preload, cache, mixer and deferred retirement using
   the locally generated enemy bank. Only PSP transport and file/heap faults
   are stubbed; no game data is embedded in this test. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned opens;
static int forbid_files,reject_bank;
static size_t allocation_limit;
static FILE *test_fopen(const char *path,const char *mode)
{
    assert(!forbid_files);opens++;
    if(reject_bank && strstr(path,"enemyaudio.bin"))return NULL;
    return fopen(path,mode);
}
static void *test_malloc(size_t n)
{return allocation_limit && n>allocation_limit ? NULL:malloc(n);}
#define fopen test_fopen
#define malloc test_malloc
#include "../src/audio.c"
#undef fopen
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
static const float origin[3]={0};
static unsigned cue(unsigned ref)
{
    unsigned i=0;while(i<ENEMY_AUDIO_CUE_COUNT && ENEMY_AUDIO_CUES[i].reference!=ref)i++;
    assert(i<ENEMY_AUDIO_CUE_COUNT);return i;
}
static void stop_voices(void)
{
    for(unsigned i=0;i<ENEMY_AUDIO_HANDLES;i++)audio_channel_release(enemy_audio_handles+i);
    smsb_stop(&enemy_sequence,0);
}
static void play(unsigned ref)
{
    short out[2048];assert(!audio_enemy_sound((int)ref,origin));
    audio_cb(out,1024,NULL);int heard=0;for(unsigned i=0;i<2048;i++)heard|=out[i]!=0;
    assert(heard);stop_voices();
}
static void test_onishima(void)
{
    audio_free_enemies();assert(!audio_preload_enemy_attacks(UINT64_C(1)<<17));
    AudioEnemyPreloadInfo p;audio_enemy_preload_info(&p);
    assert(p.planned_cues==6 && !p.missing_cues && !p.failures);
    assert(p.pinned_samples==2 && p.pinned_bytes==19224 && p.warm_samples==3);
    assert(p.sequence_bytes==228992 && enemy_audio_bytes==74752);
    for(unsigned i=0;i<ENEMY_AUDIO_HANDLES;i++)assert(!enemy_audio_handles[i].voice.active);
    assert(!enemy_sequence.sequence); /* Preloading itself is silent. */
    unsigned before=opens;forbid_files=1;
    assert(!audio_preload_enemy_attacks(UINT64_C(1)<<17));
    for(unsigned cycle=0;cycle<3;cycle++){
        play(0x110002);play(0x110005);play(0x110005);play(0x110006);
        play(0x110003);play(0x110004);play(0xffff0011u);
    }
    forbid_files=0;assert(opens==before);
    while(enemy_audio_evict(ENEMY_AUDIO_CUE_COUNT));
    assert(enemy_audio_bytes==19224 && enemy_audio_samples[cue(0x110005)] && enemy_audio_samples[cue(0x110006)]);
    forbid_files=1;play(0x110005);play(0x110006);forbid_files=0;
}
static void test_selection_and_budget(void)
{
    audio_free_enemies();unsigned before=opens;
    assert(!audio_preload_enemy_attacks(UINT64_C(1)<<44) && opens==before);
    assert(!enemy_audio_bytes && !enemy_sequence_image);
    assert(!audio_preload_enemy_attacks(UINT64_C(1)<<25));
    assert(enemy_audio_samples[cue(0x90001)] && !enemy_sequence_image && !enemy_audio_pin_bytes);
    audio_free_enemies();assert(!audio_preload_enemy_attacks(UINT64_C(1)<<24));
    assert(enemy_audio_pinned[cue(0x80004)] && enemy_sequence_image);
    audio_free_enemies();assert(!audio_preload_enemy_attacks(UINT64_C(1)<<30));
    assert(enemy_audio_pin_bytes==45479 && !enemy_sequence_image);
    audio_free_enemies();assert(!audio_preload_enemy_attacks(UINT64_C(1)<<32));
    assert(enemy_audio_pinned[cue(0xf0003)] && !enemy_audio_samples[cue(0xd0003)]);
    audio_free_enemies();assert(!audio_preload_enemy_attacks(UINT64_C(1)<<27));
    assert(enemy_audio_samples[cue(0x10006)] && enemy_audio_samples[cue(0x10007)]);
    assert(enemy_audio_pinned[cue(0x1000d)] && !enemy_sequence_image);
    audio_free_enemies();assert(audio_preload_enemy_attacks(UINT64_MAX)<0);
    AudioEnemyPreloadInfo p;audio_enemy_preload_info(&p);
    assert(p.pinned_bytes<=128*1024 && enemy_audio_bytes-p.pinned_bytes<=65536);
    assert(p.failures && p.missing_cues && p.sequence_bytes==228992);
    assert(enemy_audio_pinned[cue(0x110005)] && enemy_audio_pinned[cue(0x110006)]);
    forbid_files=1;play(0x110005);play(0x110006);forbid_files=0;
}
static void test_optional_failures(void)
{
    audio_free_enemies();reject_bank=1;assert(audio_preload_enemy_attacks(UINT64_C(1)<<17)<0);
    AudioEnemyPreloadInfo p;audio_enemy_preload_info(&p);
    assert(p.failures==6 && p.missing_cues==6 && !p.pinned_bytes && !p.sequence_bytes);
    reject_bank=0;play(0x110005); /* Failed warmup preserves ordinary playback. */
    audio_free_enemies();allocation_limit=9500;
    assert(audio_preload_enemy_attacks(UINT64_C(1)<<17)<0);
    audio_enemy_preload_info(&p);
    assert(p.pinned_samples==1 && p.pinned_bytes==1897 && p.missing_cues==5);
    allocation_limit=0;play(0x110005);assert(enemy_audio_pinned[cue(0x110006)]);
    /* A real request can reclaim idle pins under allocation pressure, while
       a currently playing resident must never be evicted. */
    assert(!audio_preload_enemy_attacks(UINT64_C(1)<<17));
    assert(!audio_enemy_sound(0x110005,origin));allocation_limit=9500;
    assert(audio_enemy_sound(0xa0000,origin)<0);
    assert(enemy_audio_samples[cue(0x110005)] && enemy_audio_pinned[cue(0x110005)]);
    assert(!enemy_audio_samples[cue(0x110006)] && !enemy_audio_pinned[cue(0x110006)]);
    allocation_limit=0;stop_voices();play(0xa0000);
}
#if JGR_AUDIO_ME
static int retire_during_job,retirement_checks;
static void during_job(void)
{
    if(!retire_during_job)return;retire_during_job=0;
    unsigned char pcm[4096];memcpy(pcm,mix_job->pcm,sizeof pcm);
    assert(mix_job->input.osb_count && mix_job->input.sequence_count);
    audio_free_enemies();assert(audio_retired && audio_memory_borrowed);
    /* The real worker re-reads its old samples after the main thread frees
       the level. ASan also catches premature reuse of the old sequence. */
    assert(audio_mix_job_execute(mix_job)==transport_reply);
    assert(!memcmp(pcm,mix_job->pcm,sizeof pcm));
    assert(!audio_preload_enemy_attacks(UINT64_C(1)<<17));
    assert(!audio_enemy_sound(0x110005,origin));
    assert(audio_mix_job_execute(mix_job)==transport_reply);
    assert(!memcmp(pcm,mix_job->pcm,sizeof pcm));retirement_checks++;
}
static void test_inflight_retirement(void)
{
    audio_free_enemies();assert(!audio_preload_enemy_attacks(UINT64_C(1)<<17));
    assert(!audio_enemy_sound(0x110005,origin));assert(!audio_enemy_sound((int)0xffff0011u,origin));
    short out[2048];retire_during_job=1;audio_cb(out,1024,NULL);
    assert(retirement_checks==1 && !audio_retired && !audio_memory_borrowed && !audio_mix_pending);
    unsigned live=0;for(unsigned i=0;i<ENEMY_AUDIO_HANDLES;i++)if(enemy_audio_handles[i].voice.active){
        assert(enemy_audio_handles[i].cue->reference==0x110005 && !enemy_audio_handles[i].voice.position);live++;
    }
    assert(live==1 && !enemy_sequence.sequence);
    forbid_files=1;audio_cb(out,1024,NULL);play(0xffff0011u);forbid_files=0;
}
#endif
int main(void)
{
    audio_init(NULL);audio_reverb_enable(0);
    test_onishima();test_selection_and_budget();test_optional_failures();
#if JGR_AUDIO_ME
    test_inflight_retirement();assert(audio_me_full_mixer() && transport_jobs>20);
#endif
    audio_free_enemies();AudioEnemyPreloadInfo p;audio_enemy_preload_info(&p);
    assert(!p.pinned_bytes && !p.pinned_samples && !p.sequence_bytes && !p.planned_cues);
    assert(audio_shutdown());puts("enemy preload: selection, bounded cache, no-I/O playback, failures and retirement passed");return 0;
}
