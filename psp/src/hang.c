#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspge.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "hang.h"
#include "crash.h"
#include "crash_report.h"
#include "release_report.h"

volatile HangProgress hang_progress;
static int monitor=-1,main_thread=-1;
static unsigned observed,serial,power_epoch,reported;
static char report[2048];
static char report_path[512];
static char stack_path[512];
typedef struct {
    unsigned magic,version,header_bytes,stack_bytes,stack_base,frame,phase,code_anchor;
    char build[68];
} HangStackHeader;
/* No stack-sized allocation: the dead main thread owns the source memory.
   A length-delimited record and footer allow an interrupted write to be
   recognized. This file is written ONLY after a kernel-killed thread. */
static int save_killed_stack(const SceKernelThreadInfo *info,unsigned phase)
{
    uintptr_t base=(uintptr_t)info->stack;
    unsigned bytes=(unsigned)info->stackSize;
    if(!(info->status&PSP_THREAD_KILLED))return 1;
    if((base&15u) || base<0x08800000u || base>=0x0a000000u ||
       bytes<256 || bytes>256u*1024u || bytes>0x0a000000u-base)return -1;
    const void *data=info->stack;
#ifdef JGR_HANG_TEST_STACK_DATA
    data=JGR_HANG_TEST_STACK_DATA(base);
#endif
    HangStackHeader header={0x5348474au,1,sizeof header,bytes,(unsigned)base,
        hang_progress.frame,phase,(unsigned)(uintptr_t)crash_build_id,{0}};
    const char *build=crash_build_id();
    for(unsigned i=0;i+1<sizeof header.build && build[i];i++)header.build[i]=build[i];
    unsigned footer=0x444e454au;
    int fd=sceIoOpen(stack_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
    if(fd<0)return fd;
    int result=jgr_crash_write_all(fd,(const char *)&header,sizeof header,sceIoWrite);
    if(!result)result=jgr_crash_write_all(fd,data,bytes,sceIoWrite);
    if(!result)result=jgr_crash_write_all(fd,(const char *)&footer,sizeof footer,sceIoWrite);
    int closed=sceIoClose(fd);if(!result && closed<0)result=closed;
    if(!result)result=sceIoSync("ms0:",0);
    return result;
}
static const char *phase_name(unsigned phase)
{
    static const char *names[]={"unknown","frame","resume","profile","log I/O",
        "controller","route I/O","player audio I/O","GE fence","VBlank","exit","diagnostic forced delay",
        "graffiti update","world/audio bank binding","camera/view preparation",
        "benchmark capture open","benchmark capture RAM copy","benchmark capture write","benchmark capture close",
        "player world update","player simulation","player contacts","player camera","street prop contacts"};
    return phase<sizeof names/sizeof *names ? names[phase]:
        phase>=HANG_DRAW ? "draw section":phase>=HANG_LOGIC ? "logic section":"unknown";
}
void hang_power(int sleeping)
{hang_progress.sleeping=!!sleeping;hang_progress.power_epoch++;}
void hang_player_context(unsigned tick,int zone,int movement,const float position[3])
{
    hang_progress.player_tick=tick;hang_progress.player_zone=(unsigned)zone;
    hang_progress.player_movement=(unsigned)movement;
    for(unsigned i=0;i<3;i++){unsigned bits;memcpy(&bits,position+i,4);hang_progress.player_position[i]=bits;}
}

static int save_report(unsigned now)
{
    SceKernelThreadInfo info={.size=sizeof info};
    int status=sceKernelReferThreadStatus(main_thread,&info);
    unsigned phase=hang_progress.phase;
    JgrCrashText t={report,sizeof report,0};
    jgr_crash_text(&t,"\nJGR HANG BEGIN v1\nbuild=");jgr_crash_text(&t,crash_build_id());
    jgr_crash_text(&t,"\nNo CPU exception captured. Numeric fields are hexadecimal.\nphase_name=");
    jgr_crash_text(&t,phase_name(phase));jgr_crash_char(&t,'\n');
    jgr_crash_field(&t,"phase=",phase);jgr_crash_field(&t,"frame=",hang_progress.frame);
    if(phase>=HANG_CAPTURE_OPEN && phase<=HANG_CAPTURE_CLOSE) {
        jgr_crash_field(&t,"capture_bytes=",hang_progress.capture_bytes);
        jgr_crash_field(&t,"capture_total=",hang_progress.capture_total);
    }
    jgr_crash_field(&t,"stage=",hang_progress.stage);jgr_crash_field(&t,"state=",hang_progress.state);
    jgr_crash_field(&t,"player_tick=",hang_progress.player_tick);
    jgr_crash_field(&t,"player_zone=",hang_progress.player_zone);
    jgr_crash_field(&t,"player_movement=",hang_progress.player_movement);
    jgr_crash_field(&t,"player_x_f32=",hang_progress.player_position[0]);
    jgr_crash_field(&t,"player_y_f32=",hang_progress.player_position[1]);
    jgr_crash_field(&t,"player_z_f32=",hang_progress.player_position[2]);
    jgr_crash_field(&t,"code_anchor=",(uintptr_t)crash_build_id);
    jgr_crash_field(&t,"idle_us=",now-observed);jgr_crash_field(&t,"power_epoch=",hang_progress.power_epoch);
    jgr_crash_field(&t,"thread=",main_thread);jgr_crash_field(&t,"thread_query=",status);
    if(status>=0) {
        jgr_crash_field(&t,"thread_status=",info.status);jgr_crash_field(&t,"wait_type=",info.waitType);
        if(info.status&PSP_THREAD_KILLED)
            jgr_crash_text(&t,"thread_diagnosis=kernel-killed; status alone does not identify the cause\nstack_dump_file=jgr_hang_stack.bin\n");
        jgr_crash_field(&t,"stack_base=",(uintptr_t)info.stack);
        jgr_crash_field(&t,"stack_bytes=",info.stackSize);
        jgr_crash_field(&t,"thread_exit_status=",info.exitStatus);
        jgr_crash_field(&t,"wait_id=",info.waitId);jgr_crash_field(&t,"priority=",info.currentPriority);
        jgr_crash_field(&t,"run_clock_low=",info.runClocks.low);
        jgr_crash_field(&t,"run_clock_high=",info.runClocks.hi);
    }
    /* Peek only. Never join the main thread's blocked GE wait. */
    jgr_crash_field(&t,"ge_state=",sceGeDrawSync(1));
    jgr_crash_text(&t,"JGR HANG END\n");
    int fd=sceIoOpen(report_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
    int result=fd<0 ? fd:jgr_crash_write_all(fd,report,(unsigned)t.used,sceIoWrite);
    if(fd>=0){int closed=sceIoClose(fd);if(!result && closed<0)result=closed;}
    if(!result)result=sceIoSync("ms0:",0);
    /* Save the short report first, even if the larger stack write fails.
       Never copy a live/hung thread's changing stack. */
    if(!result && status>=0 && (info.status&PSP_THREAD_KILLED)) {
        int dumped=save_killed_stack(&info,phase);
        t=(JgrCrashText){report,sizeof report,0};
        jgr_crash_text(&t,"JGR HANG STACK ");jgr_crash_field(&t,"frame=",hang_progress.frame);
        jgr_crash_field(&t,"stack_dump_result=",(unsigned)dumped);
        fd=sceIoOpen(report_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
        result=fd<0 ? fd:jgr_crash_write_all(fd,report,(unsigned)t.used,sceIoWrite);
        if(fd>=0){int closed=sceIoClose(fd);if(!result && closed<0)result=closed;}
        if(!result)result=sceIoSync("ms0:",0);
    }
    /* Persist the short watchdog report and any stopped-thread stack before
       appending recent context. Never borrow the possibly stalled logger. */
    if(!result) {
        fd=sceIoOpen(report_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
        result=fd<0 ? fd:release_report_write(fd,sceIoWrite);
        if(fd>=0){int closed=sceIoClose(fd);if(!result && closed<0)result=closed;}
        if(!result)result=sceIoSync("ms0:",0);
    }
    return result;
}
static void poll_progress(unsigned now)
{
    unsigned next=hang_progress.serial,epoch=hang_progress.power_epoch;
    if(!hang_progress.armed || hang_progress.sleeping || epoch!=power_epoch || next!=serial) {
        observed=now;serial=next;power_epoch=epoch;reported=0;return;
    }
    if(!reported && now-observed>=8000000u) {
        reported=1;save_report(now); /* once per stall, no normal-play writes */
    }
}
static int monitor_thread(SceSize args,void *argp)
{
    (void)args;(void)argp;
    for(;;){sceKernelDelayThread(500000);poll_progress(sceKernelGetSystemTimeLow());}
    return 0;
}
int hang_init(void)
{
    /* Raw sceIo paths are per-thread; the reporter does not inherit the
       main thread's cwd. Resolve once while healthy, without opening a
       file or using libc from the stalled-game report path. */
    if(!getcwd(report_path,sizeof report_path))return -1;
    size_t n=strlen(report_path);
    if(n+sizeof "/jgr_hang_stack.bin">sizeof stack_path)return -1;
    memcpy(stack_path,report_path,n);
    memcpy(stack_path+n,"/jgr_hang_stack.bin",sizeof "/jgr_hang_stack.bin");
    memcpy(report_path+n,"/jgr_hang.txt",sizeof "/jgr_hang.txt");
    main_thread=sceKernelGetThreadId();observed=sceKernelGetSystemTimeLow();
    monitor=sceKernelCreateThread("jgr_hang_monitor",monitor_thread,0x18,4096,PSP_THREAD_ATTR_USER,0);
    if(monitor<0)return monitor;
    int result=sceKernelStartThread(monitor,0,0);
    if(result<0){sceKernelDeleteThread(monitor);monitor=-1;}
    return result;
}
void hang_shutdown(void)
{hang_progress.armed=0;if(monitor>=0){sceKernelTerminateDeleteThread(monitor);monitor=-1;}}
