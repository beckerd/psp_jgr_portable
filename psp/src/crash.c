#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspdebug.h>
#include <pspge.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "crash.h"
#include "crash_report.h"
#include "release_report.h"
#include "crash_build.inc"
#if JGR_BUNDLED_RUNTIME
#include "portable_bundle.h"
#endif
static const char jgr_crash_build[]=
#ifdef JGR_CRASH_REPORT_FIXTURE
    "fixture-"
#endif
    JGR_CRASH_SOURCE_ID;

static JgrCrashShared storage __attribute__((aligned(64)));
static volatile JgrCrashShared *shared;
static struct {
    uint32_t magic[4];volatile JgrCrashShared *shared;uint32_t size;
} locator __attribute__((aligned(16),used))={
    {JGR_CRASH_MAGIC0,JGR_CRASH_MAGIC1,JGR_CRASH_MAGIC2,JGR_CRASH_MAGIC3},
    0,sizeof(JgrCrashShared)
};
static int module=-1,reporter=-1,report_fd=-1;
static char report[4096];
static char report_path[512];
const char *crash_build_id(void){return jgr_crash_build;}

static int report_thread(SceSize args,void *argp)
{
    (void)args;(void)argp;
    /* No timer and no polling. Only the exception helper wakes this thread. */
    while(shared->state!=JGR_CRASH_READY)sceKernelSleepThread();
    /* Stop normal rendering if an audio/stream/worker thread faulted. */
    if(shared->main_thread!=sceKernelGetThreadId())sceKernelSuspendThread(shared->main_thread);
    size_t n=jgr_crash_format(report,sizeof report,shared,jgr_crash_build);
    /* Do not use GU, wait for a display list, query malloc, walk arbitrary
       stack pointers or take the runtime logger/audio locks here. */
    /* Keep EPC/RA visible without scrolling off under the full stack dump. */
    char screen[1024];JgrCrashText t={screen,sizeof screen,0};
    jgr_crash_field(&t,"EPC:      ",shared->regs.epc);
    jgr_crash_field(&t,"RA:       ",shared->regs.gpr[31]);
    jgr_crash_field(&t,"BadVAddr: ",shared->regs.badvaddr);
    jgr_crash_field(&t,"Cause:    ",shared->regs.cause);
    jgr_crash_field(&t,"Status:   ",shared->regs.status);
    jgr_crash_field(&t,"Thread:   ",shared->fault_thread);
    jgr_crash_field(&t,"Anchor:   ",shared->anchor);
    jgr_crash_text(&t,"Build:\n");jgr_crash_text(&t,jgr_crash_build);
    jgr_crash_char(&t,'\n');screen[t.used]=0;
    if(report_fd<0)report_fd=sceIoOpen("jgr_crash.txt",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
    int result=report_fd<0 ? report_fd:
        jgr_crash_write_all(report_fd,report,(unsigned)n,sceIoWrite);
    if(report_fd>=0) {
        int closed=sceIoClose(report_fd);report_fd=-1;
        if(!result && closed<0)result=closed;
    }
    /* A close/sync failure must never be announced as a saved report. */
    if(!result)result=sceIoSync("ms0:",0);
    shared->state=result<0 ? JGR_CRASH_SAVE_FAILED:JGR_CRASH_SAVED;
    /* The original exception report is already closed and durable. Append
       independent release context afterward; a partial second report cannot
       erase the registers or change the original report's saved status. */
    if(!result && report_path[0]) {
        int fd=sceIoOpen(report_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
        if(fd>=0) {
            int extra=release_report_write(fd,sceIoWrite);
            int closed=sceIoClose(fd);if(!extra && closed<0)extra=closed;
            if(!extra)sceIoSync("ms0:",0);
        }
    }
    /* Persist first: a broken graphics driver must not prevent an otherwise
       writable exception report. Showing the screen is best effort. */
    sceGeBreak(1,0);
    pspDebugScreenInit();
    pspDebugScreenSetTextColor(0xffffffffu);
    pspDebugScreenPuts(result<0 ?
        "Crash dump could not be saved. Photograph this screen.\n":
#if JGR_PORTABLE_ASSETS
        "Crash saved: data/log/jgr_crash.txt                    \n");
#else
        "Crash saved: PSP/GAME/JGRVIEW/jgr_crash.txt             \n");
#endif
    pspDebugScreenPuts("Restart the PSP after noting the exception details.\n");
    pspDebugScreenPuts("\n");pspDebugScreenPrintData(screen,(int)t.used);
    /* Keep the fault visible. Deliberately do not resume or auto-reboot. */
    for(;;)sceKernelSleepThread();
    return 0;
}

int crash_init(void)
{
    if(sceIoDevctl("kemulator:",3,0,0,0,0)>=0)return 1;
    /* Reporter threads have independent raw-I/O working directories. */
    if(getcwd(report_path,sizeof report_path)) {
        size_t n=strlen(report_path);
        if(n+sizeof "/jgr_crash.txt"<=sizeof report_path)
            memcpy(report_path+n,"/jgr_crash.txt",sizeof "/jgr_crash.txt");
        else report_path[0]=0;
    } else report_path[0]=0;
    storage.abi=JGR_CRASH_ABI;storage.bytes=sizeof storage;
    storage.installed=JGR_CRASH_PENDING;
    storage.main_thread=sceKernelGetThreadId();
    storage.anchor=(uint32_t)(uintptr_t)crash_init;
    SceKernelModuleInfo info={.size=sizeof info};
    if(sceKernelQueryModuleInfo(sceKernelGetModuleIdByAddress(crash_init),&info)>=0) {
        storage.text_addr=info.text_addr;storage.text_size=info.text_size;
    }
    sceKernelDcacheWritebackInvalidateRange(&storage,sizeof storage);
#ifdef JGR_CRASH_HOST_TEST
    shared=&storage;
#else
    shared=(volatile JgrCrashShared *)((uintptr_t)&storage|0x40000000u);
#endif
    reporter=sceKernelCreateThread("jgr_crash_report",report_thread,0x10,8192,PSP_THREAD_ATTR_USER,0);
    if(reporter<0)return reporter;
    shared->reporter=reporter;
    int result=sceKernelStartThread(reporter,0,0);
    if(result<0){sceKernelDeleteThread(reporter);reporter=-1;return result;}
    /* Reserve the file handle while healthy; append preserves older crashes.
       No report data is written until an exception is reported. */
    report_fd=sceIoOpen("jgr_crash.txt",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
    locator.shared=shared;
    sceKernelDcacheWritebackAll();
#if JGR_BUNDLED_RUNTIME
    char helper[PORTABLE_BUNDLE_PATH_MAX];
    if(portable_bundle_path(PORTABLE_BUNDLE_CRASH,helper,sizeof helper)<0){result=-1;goto fail;}
    module=sceKernelLoadModule(helper,0,0);
#else
    module=sceKernelLoadModule("jgr_crash.prx",0,0);
#endif
    if(module<0){result=module;goto fail;}
    int status=0;
    result=sceKernelStartModule(module,0,0,&status,0);
    if(result<0)goto fail;
    if(status<0){result=status;goto fail;}
    /* Do not claim success merely because LoadModule/StartModule succeeded. */
    result=shared->installed;
    if(result==(int)JGR_CRASH_PENDING)result=-1;
    if(result<0)goto fail;
    return 0;
fail:
    crash_shutdown();
    return result;
}

void crash_memory_sample(unsigned frame,int stage,int state,unsigned heap,
                         unsigned system,unsigned largest)
{
    if(!shared)return;
    shared->sample_frame=frame;shared->sample_stage=stage;shared->sample_state=state;
    shared->heap_free=heap;shared->system_free=system;shared->system_largest=largest;
}

int crash_shutdown(void)
{
    if(module>=0) {
        int status=0;
        int result=sceKernelStopModule(module,0,0,&status,0);
        /* A failed start has no registered handler; a successful one must
           unregister before either its code or reporter can be retired. */
        if((result<0 || status<0) && shared && shared->installed>=0 &&
           shared->installed!=(int)JGR_CRASH_PENDING)return result<0?result:status;
        result=sceKernelUnloadModule(module);
        if(result<0)return result;
        module=-1;
    }
    if(reporter>=0){sceKernelTerminateDeleteThread(reporter);reporter=-1;}
    if(report_fd>=0){sceIoClose(report_fd);report_fd=-1;}
    locator.shared=0;shared=0;
    return 0;
}
