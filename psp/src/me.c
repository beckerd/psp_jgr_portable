/* Media Engine transport for dispatching background computation.
   The CPU handles OS/audio output; ME jobs are pure computation. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "me.h"
#include "me_shared.h"
#include "release_report.h"
#if JGR_BUNDLED_RUNTIME
#include "portable_bundle.h"
#endif
static JgrMeShared shared_mem;
static volatile JgrMeShared *shared;
static struct {
    unsigned magic[4];volatile JgrMeShared *shared;unsigned size;
} locator __attribute__((aligned(16),used))={
    {JGR_ME_MAGIC0,JGR_ME_MAGIC1,JGR_ME_MAGIC2,JGR_ME_MAGIC3},NULL,sizeof(JgrMeShared)
};
static MeProfile profile;
static int module=-1,online;
static volatile int suspended,resume_pending,exiting;
static char boot_log[2048];
static unsigned boot_log_used,boot_log_dropped;
static int boot_log_replace;
void me_boot_log(const char *event,int a,int b)
{
    char line[128];int n=snprintf(line,sizeof line,"%s a=%d b=%d\n",event,a,b);
    if(n<=0)return;
    unsigned bytes=n<(int)sizeof line ? (unsigned)n:sizeof line-1;
    line[bytes-1]='\n';
    /* The crash reporter can capture progress even before the disk batch is
       flushed. Its history producer only copies into bounded RAM storage. */
    release_report_log(line,bytes);
    /* Reserve space to report overflow; never turn an append into disk I/O. */
    if(boot_log_used>sizeof boot_log-64 || bytes>sizeof boot_log-64-boot_log_used){boot_log_dropped++;return;}
    memcpy(boot_log+boot_log_used,line,bytes);boot_log_used+=bytes;
}
void me_boot_log_flush(void)
{
    if(boot_log_dropped) {
        int n=snprintf(boot_log+boot_log_used,sizeof boot_log-boot_log_used,
                       "BOOT_LOG_DROPPED entries=%u\n",boot_log_dropped);
        if(n>0 && (unsigned)n<sizeof boot_log-boot_log_used){boot_log_used+=(unsigned)n;boot_log_dropped=0;}
    }
    if(!boot_log_used)return;
    int fd=sceIoOpen("jgr_me_log.txt",PSP_O_WRONLY|PSP_O_CREAT|
                     (boot_log_replace ? PSP_O_TRUNC:PSP_O_APPEND),0777);
    if(fd<0)return;
    boot_log_replace=0;
    unsigned written=0;
    while(written<boot_log_used) {
        int n=sceIoWrite(fd,boot_log+written,boot_log_used-written);
        if(n<=0 || (unsigned)n>boot_log_used-written)break;
        written+=(unsigned)n;
    }
    sceIoClose(fd);
    if(written){boot_log_used-=written;memmove(boot_log,boot_log+written,boot_log_used);}
}
static int ping(int x){return x^(int)JGR_ME_PING;}
int me_available(void){return online && !suspended && !exiting && shared && !shared->stop;}
int me_begin(int (*function)(int),int arg)
{
    if(!me_available() || !shared->done)return -1;
    unsigned start=sceKernelGetSystemTimeLow();
    /* Caller publishes only isolated input/output buffers. Shared control
       stores are uncached; ordinary RAM inputs reach ME before start. */
    sceKernelDcacheWritebackAll();
    shared->done=0;shared->func=(unsigned)function;shared->param=(unsigned)arg;shared->result=0;
    JGR_ME_SYNC();shared->start=1;
    profile.submit_us+=sceKernelGetSystemTimeLow()-start;profile.jobs++;return 0;
}
int me_wait(unsigned timeout_us)
{
    unsigned start=sceKernelGetSystemTimeLow();int timed_out=0;
    while(!shared->done && !shared->stopped && !suspended && !exiting) {
        if(sceKernelGetSystemTimeLow()-start>=timeout_us){profile.timeouts++;timed_out=1;break;}
        /* Yield the audio thread so game logic can use the main CPU. */
        profile.polls++;sceKernelDelayThread(200);
    }
    unsigned elapsed=sceKernelGetSystemTimeLow()-start;
    profile.wait_us+=elapsed;if(elapsed>profile.max_wait_us)profile.max_wait_us=elapsed;
    return !timed_out && shared->done && !suspended && !exiting && !shared->stopped;
}
int me_result(void){return shared ? shared->result:0;}
int me_background_begin(int (*chunk)(int),int arg)
{
    if(!me_available() || shared->bg_state==JGR_ME_BG_PENDING)return 0;
    sceKernelDcacheWritebackAll();
    shared->bg_func=(unsigned)chunk;shared->bg_param=(unsigned)arg;
    shared->bg_chunks=shared->bg_cycles=shared->bg_max_cycles=0;
    JGR_ME_SYNC();shared->bg_state=JGR_ME_BG_PENDING;return 1;
}
int me_background_wait(unsigned *chunks,unsigned *ticks,unsigned *max_ticks)
{
    if(!shared)return 0;
    unsigned begin=sceKernelGetSystemTimeLow();
    while(shared->bg_state==JGR_ME_BG_PENDING && !shared->stopped) {
        if(suspended || exiting) {if(!me_fence())return -1;break;}
        if(sceKernelGetSystemTimeLow()-begin>100000u) {
            if(!me_stop())return -1;
            break;
        }
        sceKernelDelayThread(50);
    }
    *chunks=shared->bg_chunks;*ticks=shared->bg_cycles;*max_ticks=shared->bg_max_cycles;
    return shared->bg_state==JGR_ME_BG_DONE && !shared->stopped;
}
void me_note_suspend(void)
{
    suspended=1;profile.suspends++;
    if(shared && module>=0 && !shared->stop) {
        shared->sleeping=1;shared->pause_req++;
        JGR_ME_SYNC();
    }
}
void me_note_resume(void)
{
    if(shared && module>=0 && !shared->stop && !exiting) {
        shared->sleeping=0;resume_pending=1;
        JGR_ME_SYNC();
    }
}
void me_note_exit(void)
{
    exiting=1;resume_pending=0;
    if(shared && module>=0){shared->stop=1;JGR_ME_SYNC();}
}
int me_resume_pending(void){return resume_pending && !exiting;}
int me_recover(void)
{
    if(!shared || module<0 || suspended || exiting || shared->stop || shared->sleeping)return 0;
    online=0;
    unsigned pause=++shared->pause_req;JGR_ME_SYNC();
    unsigned start=sceKernelGetSystemTimeLow();
    while(shared->pause_done!=pause) {
        if(suspended || exiting || shared->stop || shared->sleeping ||
           shared->pause_req!=pause || sceKernelGetSystemTimeLow()-start>100000u)return 0;
        sceKernelDelayThread(200);
    }
    /* The old job is now physically reset. Restart only if power/exit
       callbacks have not superseded this private recovery request. */
    if(suspended || exiting || shared->stop || shared->sleeping || shared->pause_req!=pause)return 0;
    unsigned request=++shared->reboot_req;JGR_ME_SYNC();start=sceKernelGetSystemTimeLow();
    while(shared->reboot_done!=request) {
        if(suspended || exiting || shared->stop || shared->sleeping ||
           shared->pause_req!=pause || sceKernelGetSystemTimeLow()-start>100000u)return 0;
        sceKernelDelayThread(200);
    }
    if(suspended || exiting || shared->stop || shared->sleeping || shared->pause_req!=pause)return 0;
    online=1;
    if(me_begin(ping,0x5678) || !me_wait(18000u) || me_result()!=(int)(0x5678^JGR_ME_PING)) {
        online=0;return 0;
    }
    return 1;
}
/* Suspend keeps the watchdog alive for an explicit reboot handshake. All
   other failure/exit paths permanently retire its user-memory pointer. */
int me_fence(void)
{
    if(!suspended || exiting)return me_stop();
    if(!shared || module<0 || shared->boot_status!=1)return 1;
    unsigned start=sceKernelGetSystemTimeLow();
    while(shared->pause_done!=shared->pause_req && !shared->stopped) {
        if(sceKernelGetSystemTimeLow()-start>200000u)return 0;
        sceKernelDelayThread(200);
    }
    return 1;
}
int me_service_resume(void)
{
    if(!me_resume_pending())return 0;
    resume_pending=0;
    if(!me_fence() || !shared || shared->stop || shared->sleeping)goto fail;
    unsigned pause=shared->pause_req,request=shared->reboot_req+1;
    shared->reboot_req=request;JGR_ME_SYNC();
    unsigned start=sceKernelGetSystemTimeLow();
    while(shared->reboot_done!=request) {
        if(exiting || shared->sleeping || sceKernelGetSystemTimeLow()-start>100000u)goto fail;
        sceKernelDelayThread(200);
    }
    if(exiting || shared->sleeping || pause!=shared->pause_req)goto fail;
    suspended=0;online=1;
    if(me_begin(ping,0x4321) || !me_wait(18000) || me_result()!=(int)(0x4321^JGR_ME_PING))goto fail;
    profile.resumes++;return 1;
fail:
    profile.resume_errors++;me_stop();return 0;
}
int me_stop(void)
{
    online=0;
    if(!shared || module<0 || shared->boot_status!=1)return 1;
    shared->stop=1;JGR_ME_SYNC();
    unsigned start=sceKernelGetSystemTimeLow();
    while(!shared->stopped) {
        if(sceKernelGetSystemTimeLow()-start>200000u)return 0;
        sceKernelDelayThread(200);
    }
    return 1;
}
void me_init(void)
{
    boot_log_used=boot_log_dropped=0;boot_log_replace=1;
    /* Drop the BSS loader's cached zeros before using an uncached alias. */
    sceKernelDcacheWritebackInvalidateRange(&shared_mem,sizeof shared_mem);
#ifdef JGR_ME_HOST_TEST
    shared=&shared_mem;
#else
    shared=(volatile JgrMeShared *)((uintptr_t)&shared_mem|0x40000000u);
#endif
    shared->done=1;shared->init=1;
    locator.shared=shared;profile=(MeProfile){0};
    if(sceIoDevctl("kemulator:",3,NULL,0,NULL,0)>=0){profile.load_error=1;return;}
    SceUID disabled=sceIoOpen("nome",PSP_O_RDONLY,0);
    if(disabled>=0){sceIoClose(disabled);profile.load_error=2;return;}
    if(exiting || suspended){profile.load_error=3;return;}
    me_boot_log("LOAD",0,0);
    sceKernelDcacheWritebackAll();
#if JGR_BUNDLED_RUNTIME
    char helper[PORTABLE_BUNDLE_PATH_MAX];
    if(portable_bundle_path(PORTABLE_BUNDLE_ME,helper,sizeof helper)<0){profile.load_error=-1;me_boot_log("PATH_FAILED",-1,0);me_boot_log_flush();return;}
    module=sceKernelLoadModule(helper,0,NULL);
#else
    module=sceKernelLoadModule("jgr_me.prx",0,NULL);
#endif
    me_boot_log("LOADED",module,0);
    if(module<0){profile.load_error=module;me_boot_log_flush();return;}
    int status=0;int result=sceKernelStartModule(module,0,NULL,&status,NULL);
    me_boot_log("STARTED",result,status);
    if(result<0){profile.load_error=result;me_boot_log_flush();me_shutdown();return;}
    profile.boot_status=shared->boot_status;
    if(shared->boot_status!=1){profile.ping_error=-1;me_boot_log("BOOT_FAILED",profile.boot_status,0);me_boot_log_flush();return;}
    online=1;unsigned start=sceKernelGetSystemTimeLow();
    if(me_begin(ping,0x1234) || !me_wait(200000) || me_result()!=(int)(0x1234^JGR_ME_PING)) {
        profile.ping_error=-2;me_boot_log("PING_FAILED",me_result(),shared->done);me_boot_log_flush();me_stop();return;
    }
    profile.ping_us=sceKernelGetSystemTimeLow()-start;me_boot_log("PING_OK",profile.ping_us,0);
    profile.jobs=profile.wait_us=profile.max_wait_us=profile.submit_us=profile.polls=0;
}
int me_shutdown(void)
{
    int fenced=me_stop();
    if(module>=0) {
        int status=0;
        /* module_stop synchronously resets ME and terminates its watcher,
           even if the uncached stop acknowledgement failed. */
        if(sceKernelStopModule(module,0,NULL,&status,NULL)>=0) {
            fenced=1;sceKernelUnloadModule(module);module=-1;
        }
    }
    return fenced;
}
void me_profile_read(MeProfile *out)
{
    *out=profile;out->available=me_available();
    profile.jobs=profile.timeouts=profile.wait_us=profile.max_wait_us=profile.submit_us=profile.polls=0;
}
