/* Setup-only single-worker transport. The kernel helper is shared with the
 * game executable, but that executable and its ME clients are not resident. */
#include "portable_setup_me.h"
#include <string.h>
#if defined(__PSP__) || defined(JGR_SETUP_ME_HOST_TEST)
#include <pspkernel.h>
#include <pspiofilemgr.h>
#ifdef JGR_SETUP_ME_HOST_TEST
#define JGR_ME_HOST_TEST 1
#endif
#include "me_shared.h"

static JgrMeShared setup_control;
static volatile JgrMeShared *control;
typedef struct {
    unsigned magic[4];volatile JgrMeShared *shared;unsigned size;
} __attribute__((aligned(64))) SetupLocator;
static SetupLocator setup_locator __attribute__((used))={
    {JGR_ME_MAGIC0,JGR_ME_MAGIC1,JGR_ME_MAGIC2,JGR_ME_MAGIC3},NULL,sizeof(JgrMeShared)
};
static int module=-1,module_stopped,attempted,online,active;
static volatile int disabled,suspended,exiting;
static JgrSetupMeRange borrowed[JGR_SETUP_ME_MAX_RANGES];
static unsigned borrowed_count;
static JgrSetupMeStats statistics;

static unsigned now(void){return sceKernelGetSystemTimeLow();}
static int ping(intptr_t value){return (int)value^(int)JGR_ME_PING;}
int jgr_setup_me_available(void)
{return online && !disabled && !suspended && !exiting && control && !control->stop && !control->sleeping && !control->stopped;}
static int valid_ranges(const JgrSetupMeRange *ranges,unsigned count)
{
    if(!ranges || !count || count>JGR_SETUP_ME_MAX_RANGES)return 0;
    for(unsigned i=0;i<count;i++){
        uintptr_t at=(uintptr_t)ranges[i].address;size_t bytes=ranges[i].bytes;
        if(!at || !bytes || (at&63) || (bytes&63) || bytes>INT32_MAX || at>UINTPTR_MAX-bytes ||
           !ranges[i].flags || (ranges[i].flags&~(JGR_SETUP_ME_READ|JGR_SETUP_ME_WRITE)))return 0;
#ifndef JGR_SETUP_ME_HOST_TEST
        /* Reject aliases and kernel/VRAM ranges. Rounding arbitrary pointers
         * would permit invalidating an adjacent CPU-owned allocation. */
        if(at<0x08800000u || at+bytes>0x0a000000u)return 0;
#endif
        for(unsigned j=0;j<i;j++){
            uintptr_t other=(uintptr_t)ranges[j].address;
            if(at<other+ranges[j].bytes && other<at+bytes)return 0;
        }
    }
    return 1;
}
static void release_ranges(int completed)
{
    unsigned start=now();
    /* ME flushes its own entire 8-KiB cache before publishing done. A reset
     * can discard partial work; cancelled output must never be consumed. */
    for(unsigned i=0;i<borrowed_count;i++)if(borrowed[i].flags&JGR_SETUP_ME_WRITE)
        sceKernelDcacheInvalidateRange(borrowed[i].address,(unsigned)borrowed[i].bytes);
    JGR_ME_SYNC();statistics.cache_us+=(unsigned)(now()-start);
    borrowed_count=0;active=0;
    if(completed)statistics.completed++;else statistics.cancelled++;
}
int jgr_setup_me_submit(JgrSetupMeWorker worker,intptr_t argument,
                       const JgrSetupMeRange *ranges,unsigned count)
{
    if(!jgr_setup_me_available() || active || !control->done || !worker || !valid_ranges(ranges,count))return 0;
    unsigned start=now();
    for(unsigned i=0;i<count;i++){
        borrowed[i]=ranges[i];
        /* Even write-only arenas can contain dirty CPU cache lines from
         * initialization. Publish/invalidate them before ME owns the range. */
        sceKernelDcacheWritebackInvalidateRange(ranges[i].address,(unsigned)ranges[i].bytes);
    }
    statistics.cache_us+=(unsigned)(now()-start);
    if(!jgr_setup_me_available())return 0;
    borrowed_count=count;active=1;
    control->done=0;control->func=(unsigned)(uintptr_t)worker;
    control->param=(unsigned)(uintptr_t)argument;control->result=0;
    JGR_ME_SYNC();control->start=1;statistics.jobs++;return 1;
}
/* module_stop resets the core synchronously, even if the watchdog is stuck.
 * A failed unregister/unload must still prevent executable replacement. */
static int retire_module(void)
{
    if(module<0)return 1;
    if(!module_stopped){
        int status=0,result=sceKernelStopModule(module,0,NULL,&status,NULL);
        if(result<0 || status<0)return 0;
        module_stopped=1;
    }
    if(sceKernelUnloadModule(module)<0)return 0;
    module=-1;return 1;
}
int jgr_setup_me_cancel(void)
{
    disabled=1;online=0;
    int fenced=module<0 || module_stopped;
    if(!fenced && control){
        control->stop=1;JGR_ME_SYNC();unsigned start=now();
        while(!control->stopped && (unsigned)(now()-start)<200000u)sceKernelDelayThread(100);
        fenced=control->stopped!=0;
    }
    if(!fenced){
        /* A failed start can leave the helper partially initialized. Never
         * infer a fence solely from boot_status or a user-side timeout. */
        (void)retire_module();
        fenced=module<0 || module_stopped || (control && control->stopped);
    }
    if(fenced && active)release_ranges(0);
    return fenced;
}
int jgr_setup_me_poll(int *result)
{
    if(!active)return JGR_SETUP_ME_CANCELLED;
    if(disabled || suspended || exiting || !control || control->stop || control->sleeping || control->stopped ||
       control->pause_req!=control->pause_done)
        return jgr_setup_me_cancel()?JGR_SETUP_ME_CANCELLED:JGR_SETUP_ME_PENDING;
    if(!control->done)return JGR_SETUP_ME_PENDING;
    JGR_ME_SYNC();int value=control->result;release_ranges(1);
    if(result)*result=value;
    return JGR_SETUP_ME_COMPLETE;
}
int jgr_setup_me_wait(unsigned timeout_us,int *result)
{
    unsigned start=now();int state;
    while((state=jgr_setup_me_poll(result))==JGR_SETUP_ME_PENDING){
        if((unsigned)(now()-start)>=timeout_us){
            statistics.timeouts++;
            state=jgr_setup_me_cancel()?JGR_SETUP_ME_CANCELLED:JGR_SETUP_ME_PENDING;break;
        }
        sceKernelDelayThread(100);
    }
    statistics.wait_us+=(unsigned)(now()-start);return state;
}
int jgr_setup_me_shutdown(void)
{
    int fenced=jgr_setup_me_cancel();
    int retired=retire_module();
    if(retired && active){release_ranges(0);fenced=1;}
    if(retired){setup_locator.shared=NULL;sceKernelDcacheWritebackInvalidateRange(&setup_locator,sizeof setup_locator);}
    return fenced && retired;
}
void jgr_setup_me_note_suspend(void)
{
    suspended=1;disabled=1;
    if(control && module>=0){control->sleeping=1;control->stop=1;JGR_ME_SYNC();}
}
void jgr_setup_me_note_resume(void)
{suspended=0;/* This setup run remains on CPU after any suspend. */}
void jgr_setup_me_note_exit(void)
{
    exiting=1;disabled=1;
    if(control && module>=0){control->stop=1;JGR_ME_SYNC();}
}
int jgr_setup_me_init(const char *helper)
{
    if(attempted)return jgr_setup_me_available();
    attempted=1;
    if(!helper || !*helper || disabled || suspended || exiting)return 0;
    if(sceIoDevctl("kemulator:",3,NULL,0,NULL,0)>=0){statistics.load_error=1;return 0;}
    SceUID off=sceIoOpen("nome",PSP_O_RDONLY,0);
    if(off>=0){sceIoClose(off);statistics.load_error=2;return 0;}
    memset(&setup_control,0,sizeof setup_control);
    sceKernelDcacheWritebackInvalidateRange(&setup_control,sizeof setup_control);
#ifdef JGR_SETUP_ME_HOST_TEST
    control=&setup_control;
#else
    control=(volatile JgrMeShared *)((uintptr_t)&setup_control|0x40000000u);
#endif
    control->done=1;control->init=1;setup_locator.shared=control;
    /* One startup publication covers immutable worker code/constants and
     * the locator. Per-job publication uses only the declared ranges. */
    sceKernelDcacheWritebackAll();
    module=sceKernelLoadModule(helper,0,NULL);
    if(module<0){statistics.load_error=module;return 0;}
    int status=0,result=sceKernelStartModule(module,0,NULL,&status,NULL);
    statistics.boot_status=control->boot_status;
    if(result<0 || status<0 || control->boot_status!=1){
        statistics.load_error=result<0?result:status<0?status:-1;
        /* This helper sets boot_status only after replacing/starting ME.
         * Startup rejection before that point has no worker or registered
         * callback left alive; unload the never-started module directly. */
        if(control->boot_status!=1 && sceKernelUnloadModule(module)>=0)module=-1;
        (void)jgr_setup_me_shutdown();return 0;
    }
    /* Ping has no borrowed data. It tests instruction execution and the
     * foreground mailbox before any caller may surrender its storage. */
    control->done=0;control->func=(unsigned)(uintptr_t)ping;control->param=0x1357;
    JGR_ME_SYNC();control->start=1;unsigned start=now();
    while(!control->done && !disabled && !control->stopped && (unsigned)(now()-start)<200000u)sceKernelDelayThread(100);
    if(!control->done || control->result!=(int)(0x1357^JGR_ME_PING) || disabled || control->stopped){
        statistics.load_error=-2;(void)jgr_setup_me_shutdown();return 0;
    }
    online=1;return jgr_setup_me_available();
}
void jgr_setup_me_stats(JgrSetupMeStats *out)
{if(out){*out=statistics;out->available=jgr_setup_me_available();out->active=active;out->disabled=disabled;}}
#else
int jgr_setup_me_init(const char *helper){(void)helper;return 0;}
int jgr_setup_me_available(void){return 0;}
int jgr_setup_me_submit(JgrSetupMeWorker worker,intptr_t argument,const JgrSetupMeRange *ranges,unsigned count)
{(void)worker;(void)argument;(void)ranges;(void)count;return 0;}
int jgr_setup_me_poll(int *result){(void)result;return JGR_SETUP_ME_CANCELLED;}
int jgr_setup_me_wait(unsigned timeout,int *result){(void)timeout;(void)result;return JGR_SETUP_ME_CANCELLED;}
int jgr_setup_me_cancel(void){return 1;}
int jgr_setup_me_shutdown(void){return 1;}
void jgr_setup_me_note_suspend(void){}
void jgr_setup_me_note_resume(void){}
void jgr_setup_me_note_exit(void){}
void jgr_setup_me_stats(JgrSetupMeStats *out){if(out)memset(out,0,sizeof *out);}
#endif
