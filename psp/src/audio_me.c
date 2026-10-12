#include "diagnostics.h"
#include <pspkernel.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include "audio_me.h"
#include "me.h"
#include "reverb_job.h"
ReverbStorage audio_reverb_storage;
static ReverbJob job;
static AudioMixJob *mix_job;
static Smsb *mix_sequences;
static unsigned mix_sequence_bytes;
AudioMixJob *audio_me_mix_buffer(void){return mix_job;}
static unsigned bank,serial;
static int mode,mix_enabled;
int audio_me_full_mixer(void){return mode>0 && mix_enabled;}
static AudioMeProfile profile;
static void *quarantine_scratch;
static int verify_worker(void);
static int verify_mix_worker(void);
static int audio_me_mix_limit(unsigned count,int reset,unsigned timeout);
/* Release startup still executes real jobs on both banks, compares PCM/state
   with the CPU, and takes the same timeout/fence paths. Long-tail coverage and
   the complete pause/mono/reset matrix belong to diagnostic builds. */
#if JGR_DIAGNOSTICS
enum { VERIFY_REVERB_BLOCKS=24,VERIFY_MIX_BLOCKS=8,VERIFY_FRAMES=1024 };
#else
enum { VERIFY_REVERB_BLOCKS=2,VERIFY_MIX_BLOCKS=2,VERIFY_FRAMES=64 };
#endif
void audio_me_init(void)
{
    me_init();mode=me_available();bank=serial=0;mix_enabled=0;
#ifdef JGR_AUDIO_MIX_CPU_TEST
    /* Isolated integration fixture only: run real transactions on Allegrex
       when PPSSPP has no ME. Never claim these timings as ME performance. */
    mode=1;me_boot_log("CPU_WORKER_TEST",1,0);
#endif
    if(mode) {
        packed_reverb_reset(&audio_reverb_storage.bank[0]);
        sceKernelDcacheWritebackInvalidateRange(&audio_reverb_storage,sizeof audio_reverb_storage);
        me_boot_log("VERIFY",JGR_DIAGNOSTICS,VERIFY_FRAMES);
        unsigned started=JGR_PROFILE_CLOCK();
        profile.verify_status=verify_worker();profile.verify_us=JGR_PROFILE_CLOCK()-started;
        me_boot_log("VERIFIED",profile.verify_status,profile.verify_samples);
        if(profile.verify_status!=1)audio_me_force_cpu();
        if(mode>0) {
            mix_job=memalign(64,sizeof *mix_job);
            if(!mix_job){me_boot_log("MIX_ALLOC_FAILED",sizeof *mix_job,0);audio_me_force_cpu();}
            else {
                memset(mix_job,0,sizeof *mix_job);
                if(!audio_me_reserve_mix(1)){me_boot_log("MIX_ALLOC_FAILED",0,0);audio_me_force_cpu();goto finished;}
                mix_enabled=1;
                unsigned stamp=JGR_PROFILE_CLOCK();
                profile.mix_verify_status=verify_mix_worker();profile.mix_verify_us=JGR_PROFILE_CLOCK()-stamp;
                me_boot_log("MIX_VERIFIED",profile.mix_verify_status,profile.mix_verify_samples);
                me_boot_log("MIX_BUFFER_BYTES",sizeof *mix_job,0);
                me_boot_log("MIX_REFERENCE_US",profile.mix_reference_us,0);
                me_boot_log("MIX_FAILURE",profile.mix_fail_kind,profile.mix_fail_result);
                me_boot_log("MIX_WAIT_US",profile.mix_fail_wait_us,profile.mix_fail_timeout_us);
                if(profile.mix_verify_status!=1) {
                    if(mix_enabled && mode>0)profile.fallbacks++;
                    mix_enabled=0;
                }
                if(mode>=0){
                    unsigned cap=mix_job->sequence_capacity;memset(mix_job,0,sizeof *mix_job);
                    mix_job->sequence_capacity=cap;mix_job->input.sequences=mix_sequences;mix_job->output.sequences=mix_sequences+cap;
                }
            }
        }
        /* Startup work was not audible playback. Keep its verification
           status/duration, retire its transport costs before callbacks. */
        MeProfile ignored;me_profile_read(&ignored);profile.jobs=profile.samples=0;
    }
finished:
    /* Startup events are a single bounded write, never callback-time I/O. */
    me_boot_log_flush();
}
int audio_me_owns_state(void){return mode!=0;}
int audio_me_quarantined(void){return mode<0;}
int audio_me_force_cpu(void)
{
    if(!mode)return 1;
    if(!me_fence()){mode=-1;profile.quarantined=1;return 0;}
    sceKernelDcacheInvalidateRange(&audio_reverb_storage,sizeof audio_reverb_storage);
    packed_reverb_recover(&audio_reverb_storage,bank);
    free(quarantine_scratch);quarantine_scratch=NULL;
    mode=0;profile.quarantined=0;profile.fallbacks++;return 1;
}
void audio_me_service_resume(void)
{
    if(!me_resume_pending())return;
    if(!audio_me_force_cpu())return;
    if(profile.verify_status==1 && me_service_resume()) {
        packed_reverb_from_cpu(&audio_reverb_storage);bank=0;
        sceKernelDcacheWritebackInvalidateRange(&audio_reverb_storage,sizeof audio_reverb_storage);mode=1;
    }
}
int audio_me_process(const int *sends,int (*wet)[2],unsigned count,int reset)
{
    if(!mode)return 0;
    if(mode<0 || count>REVERB_JOB_FRAMES
#ifndef JGR_AUDIO_MIX_CPU_TEST
       || !me_available()
#endif
    ) {
        return audio_me_force_cpu() ? 0:-1;
    }
    /* Caller supplies private, cache-line-isolated callback arrays. Flush
       and drop old output lines, including the BSS loader's cached zeros. */
    if(count)sceKernelDcacheWritebackInvalidateRange(wet,(count*sizeof *wet+63u)&~63u);
    job=(ReverbJob){&audio_reverb_storage,sends,wet,bank,count,!!reset,++serial};
#ifdef JGR_AUDIO_MIX_CPU_TEST
    if(reverb_job_execute(&job)!=(int)(0x13579bdfu^serial^count))return -1;
#else
    if(me_begin(reverb_job_worker,(int)&job) || !me_wait(18000u) || me_result()!=(int)(0x13579bdfu^serial^count)) {
        /* The committed bank was never written by this job. Reset-fence
           before CPU expansion/replay: no concurrent CPU/ME state writes. */
        return audio_me_force_cpu() ? 0:-1;
    }
#endif
    bank^=1;profile.jobs++;profile.samples+=count;
    if(count)sceKernelDcacheInvalidateRange(wet,(count*sizeof *wet+63u)&~63u);
    return 1;
}
void audio_me_release_idle_mix(void)
{
    if(mode || me_resume_pending())return; /* mode 0 implies a confirmed fence */
    free(mix_sequences);mix_sequences=NULL;mix_sequence_bytes=0;
    free(mix_job);mix_job=NULL;
}
AudioReverbBuffer *audio_me_reverb_buffer(void)
{
    /* Reuse the full transaction allocation after its final commit. Never
       keep another 20 KiB resident solely for the fallback path. */
    _Static_assert(sizeof(AudioReverbBuffer)<=sizeof(AudioMixJob),"fallback workspace");
    if(mode<=0 || !mix_job)return NULL;
    mix_enabled=0;free(mix_sequences);mix_sequences=NULL;mix_sequence_bytes=0;
    return (AudioReverbBuffer *)(void *)mix_job;
}
int audio_me_reserve_mix(unsigned count)
{
    if(!mix_job || count>AUDIO_JOB_SEQUENCES || mode<0)return 0;
    if(!count)count=1;
    if(mix_job->sequence_capacity==count)return 1;
    /* No worker is in flight here. Live game cursors already hold the last
       committed result, so obsolete snapshot capacity can be freed BEFORE
       allocation. Never hold both arenas during a fragmented city handoff. */
    free(mix_sequences);mix_sequences=NULL;mix_sequence_bytes=0;
    mix_job->sequence_capacity=0;mix_job->input.sequences=mix_job->output.sequences=NULL;
    unsigned bytes=(2*count*sizeof(Smsb)+63u)&~63u;
    mix_sequences=memalign(64,bytes);if(!mix_sequences)return 0;
    memset(mix_sequences,0,bytes);mix_sequence_bytes=bytes;
    mix_job->sequence_capacity=count;mix_job->input.sequences=mix_sequences;mix_job->output.sequences=mix_sequences+count;return 1;
}
static int audio_me_mix_limit(unsigned count,int reset,unsigned timeout)
{
    if(!mix_job || count>REVERB_JOB_FRAMES)return -1;
    if(mode<0 && !audio_me_force_cpu())return -1;
    mix_job->storage=&audio_reverb_storage;mix_job->count=count;
    mix_job->bank=bank;mix_job->reset=!!reset;mix_job->serial=++serial;
    if(mode>0) {
        /* The entire descriptor is private to the callback until fenced.
           Drop stale output cache lines before letting ME publish them. */
        sceKernelDcacheWritebackInvalidateRange(mix_job,sizeof *mix_job);
        sceKernelDcacheWritebackInvalidateRange(mix_sequences,mix_sequence_bytes);
#ifdef JGR_AUDIO_MIX_CPU_TEST
        if(audio_mix_job_execute(mix_job)==(int)(0x4d49584au^serial^count)) {
#else
        unsigned started=JGR_PROFILE_CLOCK();
        int submitted=me_begin(audio_mix_job_worker,(int)mix_job);
        int waited=submitted ? 0:me_wait(timeout);
        int reply=me_result();
        if(!submitted && waited && reply==(int)(0x4d49584au^serial^count)) {
#endif
            bank^=1;profile.jobs++;profile.samples+=count;
            sceKernelDcacheInvalidateRange(mix_job,sizeof *mix_job);
            sceKernelDcacheInvalidateRange(mix_sequences,mix_sequence_bytes);return 1;
        }
#ifndef JGR_AUDIO_MIX_CPU_TEST
        profile.mix_fail_kind=submitted ? 1:(!waited ? 2:3);
        profile.mix_fail_result=reply;profile.mix_fail_wait_us=JGR_PROFILE_CLOCK()-started;
        profile.mix_fail_timeout_us=timeout;
#endif
        /* A failed full callback must not throw away verified ME reverb.
           Reset/ping first, then recover only the last committed DSP bank.
           If reset cannot be confirmed, retain the existing quarantine. */
        if(me_available())mix_enabled=0; /* suspend/exit is not a mixer defect */
        if(me_recover()) {
            sceKernelDcacheInvalidateRange(&audio_reverb_storage,sizeof audio_reverb_storage);
            packed_reverb_recover(&audio_reverb_storage,bank);profile.mix_recoveries++;profile.fallbacks++;
        } else if(!audio_me_force_cpu())return -1;
        /* Input was never written by ME. Invalidate partially written
           output only after reset, then replay this SAME buffer once. */
        sceKernelDcacheInvalidateRange(mix_job,sizeof *mix_job);
        sceKernelDcacheInvalidateRange(mix_sequences,mix_sequence_bytes);
    }
    packed_reverb_from_cpu(&audio_reverb_storage);bank=0;mix_job->bank=0;
    if(audio_mix_job_execute(mix_job)!=(int)(0x4d49584au^serial^count))return -1;
    packed_reverb_recover(&audio_reverb_storage,1);
    if(mode>0) {
        packed_reverb_from_cpu(&audio_reverb_storage);bank=0;
        sceKernelDcacheWritebackInvalidateRange(&audio_reverb_storage,sizeof audio_reverb_storage);
    }
    return 0;
}
int audio_me_mix(unsigned count,int reset){return audio_me_mix_limit(count,reset,18000u);}
void audio_me_collect(AudioMeProfile *out)
{
    MeProfile transport;me_profile_read(&transport);
    *out=profile;out->enabled=mode>0 && me_available();
    out->workspace_bytes=mix_job ? sizeof *mix_job+mix_sequence_bytes:0;
    out->sequence_capacity=mix_job && mix_enabled ? mix_job->sequence_capacity:0;
    out->full_callback=out->enabled && mix_enabled;
    out->load_error=transport.load_error;out->boot_status=transport.boot_status;
    out->ping_error=transport.ping_error;out->ping_us=transport.ping_us;
    out->suspends=transport.suspends;out->resumes=transport.resumes;out->resume_errors=transport.resume_errors;
    out->timeouts=transport.timeouts;out->wait_us=transport.wait_us;
    out->max_wait_us=transport.max_wait_us;out->submit_us=transport.submit_us;out->polls=transport.polls;
    profile.jobs=profile.samples=profile.fallbacks=0;
}
static int verify_worker(void)
{
    unsigned state_bytes=(sizeof(SourceReverb)+63u)&~63u;
    unsigned char *scratch=memalign(64,state_bytes+VERIFY_FRAMES*12u);
    if(!scratch)return -1;
    SourceReverb *reference=(SourceReverb *)scratch;
    int *sends=(int *)(scratch+state_bytes),(*wet)[2]=(int (*)[2])(sends+VERIFY_FRAMES);
    source_reverb_reset(reference);unsigned rng=1;int result=1;
    /* Diagnostics cross the longest 18,294-sample tap. The short release
       check sends nonzero input immediately and verifies both bank commits. */
    for(unsigned block=0;block<VERIFY_REVERB_BLOCKS && result==1;block++) {
        for(unsigned i=0;i<VERIFY_FRAMES;i++) {
            rng=rng*1664525u+1013904223u;
            sends[i]=JGR_DIAGNOSTICS && (block<2 || block>=12) ? 0:(int16_t)(rng>>16);
        }
        int ran=audio_me_process(sends,wet,VERIFY_FRAMES,block==0);
        if(ran!=1){result=-2;if(ran<0){quarantine_scratch=scratch;scratch=NULL;}break;}
        for(unsigned i=0;i<VERIFY_FRAMES;i++) {
            int expected[2];source_reverb_next(reference,sends[i],expected);
            if(expected[0]!=wet[i][0] || expected[1]!=wet[i][1]){result=-3;break;}
            profile.verify_samples++;
        }
    }
    if(result==1) {
        sceKernelDcacheInvalidateRange(&audio_reverb_storage,sizeof audio_reverb_storage);
        const PackedReverb *p=&audio_reverb_storage.bank[bank];
        if(p->cursor!=reference->cursor || p->started!=reference->started ||
           memcmp(p->temp,reference->temp,sizeof p->temp) || memcmp(p->mem,reference->mem,sizeof p->mem))result=-4;
        for(unsigned i=0;i<=SOURCE_REVERB_MASK && result==1;i++)
            if(packed_reverb_read(p,i)!=source_reverb_read(reference,i))result=-5;
    }
    free(scratch);return result;
}
#include "audio_job_fixture.h"
static int verify_mix_worker(void)
{
    AudioMixJob *reference=memalign(64,sizeof *reference);
    ReverbStorage *state=memalign(64,sizeof *state);
    Smsb *reference_sequences=memalign(64,2*sizeof(Smsb));
    if(!reference || !state || !reference_sequences){free(reference);free(state);free(reference_sequences);return -1;}
    audio_job_fixture(mix_job);memset(state,0,sizeof *state);packed_reverb_reset(&state->bank[0]);unsigned ref_bank=0;int result=1;
    /* Decode only the bounded sanity block in release; keep the original
       full-block cold-start stress case in diagnostics. */
    mix_job->music_decode.blocks=VERIFY_FRAMES/32;
    for(unsigned i=0;i<VERIFY_MIX_BLOCKS && result==1;i++) {
        mix_job->input.mono=i&1;mix_job->input.paused=i==3;
        mix_job->input.enabled=i!=5;
        *reference=*mix_job;reference->storage=state;reference->bank=ref_bank;
        reference->input.sequences=reference_sequences;reference->output.sequences=reference_sequences+1;
        *reference_sequences=*mix_job->input.sequences;
        reference->reset=i==0 || i==6;reference->count=VERIFY_FRAMES;reference->serial=serial+1;
        unsigned started=JGR_PROFILE_CLOCK();
        audio_mix_job_execute(reference);ref_bank^=1;
        profile.mix_reference_us+=JGR_PROFILE_CLOCK()-started;
        /* This deliberately combines three ADX speakers, effects, a
           sequencer, music decode and a cold DSP reset. It runs before
           playback: don't apply the live callback's 18 ms deadline. */
        if(audio_me_mix_limit(VERIFY_FRAMES,i==0 || i==6,100000u)!=1){result=-2;break;}
        if(memcmp(reference->pcm,mix_job->pcm,sizeof mix_job->pcm) ||
           memcmp(&reference->output,&mix_job->output,offsetof(AudioJobState,sequences)) ||
           memcmp(&reference->output.movie,&mix_job->output.movie,sizeof(AudioJobState)-offsetof(AudioJobState,movie)) ||
           memcmp(reference->output.sequences,mix_job->output.sequences,sizeof(Smsb)) ||
           memcmp(&reference->music_decode,&mix_job->music_decode,sizeof mix_job->music_decode)) {
            result=-3;profile.mix_fail_kind=4;profile.mix_fail_result=result;
        }
        sceKernelDcacheInvalidateRange(&audio_reverb_storage,sizeof audio_reverb_storage);
        if(memcmp(&state->bank[ref_bank],&audio_reverb_storage.bank[bank],offsetof(PackedReverb,started)+sizeof(unsigned))) {
            result=-4;profile.mix_fail_kind=5;profile.mix_fail_result=result;
        }
        profile.mix_verify_samples+=VERIFY_FRAMES;
        Smsb *input=mix_job->input.sequences;mix_job->input=mix_job->output;mix_job->input.sequences=input;*input=*mix_job->output.sequences;
    }
    free(reference);free(state);free(reference_sequences);return result;
}
int audio_me_shutdown(void)
{
    if(me_shutdown()){free(mix_sequences);mix_sequences=NULL;mix_sequence_bytes=0;free(mix_job);mix_job=NULL;free(quarantine_scratch);quarantine_scratch=NULL;mode=0;return 1;}
    /* An unacknowledged reset must not release worker-owned scratch. */
    return 0;
}
