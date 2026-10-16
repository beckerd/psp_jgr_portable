/* Temporary user module. Setup runs on its own thread so module_start can
 * return before setup loads helpers. Retire every worker and callback, then
 * release libc and join the setup thread before the host unloads this PRX. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#include "setup_module.h"
#include "portable_setup_me.h"
#include "crash.h"
#include <pspkernel.h>
#include <psppower.h>
#include <pspctrl.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("JGR Converter",0,1,0);
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(512);
extern void __libcglue_init(int,char **);
extern void __libcglue_deinit(void);
/* Setup uses PSP threads directly; it has no pthread clients. */
void __libpthreadglue_init(void){}
/* A fatal libc abort cannot safely return control or unload live workers.
 * Leave it to the exception reporter instead of exiting the host game. */
void __attribute__((noreturn)) _exit(int status)
{(void)status;__builtin_trap();}
static volatile int active,callbacks_running,callbacks_ready;
static SceUID setup_thread=-1;
static int exit_callback=-1,power_callback=-1,power_slot=-1;
static int quit(int a,int b,void *user)
{(void)a;(void)b;(void)user;portable_assets_setup_request_exit();return 0;}
static int power_event(int a,int flags,void *user)
{
    (void)a;(void)user;
    if(flags&(PSP_POWER_CB_SUSPENDING|PSP_POWER_CB_STANDBY))jgr_setup_me_note_suspend();
    if(flags&PSP_POWER_CB_RESUME_COMPLETE)jgr_setup_me_note_resume();
    return 0;
}
static int callbacks(SceSize size,void *user)
{
    (void)size;(void)user;
    exit_callback=sceKernelCreateCallback("setup exit",quit,NULL);
    if(exit_callback>=0)sceKernelRegisterExitCallback(exit_callback);
    power_callback=sceKernelCreateCallback("setup power",power_event,NULL);
    if(power_callback>=0)power_slot=scePowerRegisterCallback(-1,power_callback);
    if(power_slot<0)jgr_setup_me_note_suspend();
    callbacks_ready=1;
    while(callbacks_running){scePowerTick(0);portable_assets_setup_tick();sceKernelDelayThreadCB(50000);}
    if(power_slot>=0)scePowerUnregisterCallback(power_slot);
    if(power_callback>=0)sceKernelDeleteCallback(power_callback);
    /* Deleting the exit callback unregisters this module's callback. */
    if(exit_callback>=0)sceKernelDeleteCallback(exit_callback);
    return 0;
}
static int run_setup(SceSize size,void *argument)
{
    (void)size;
    JgrSetupModuleArgs *args=argument;
    char executable[JGR_SETUP_ROOT_BYTES+16];
    size_t n=strlen(args->root);
    memcpy(executable,args->root,n);memcpy(executable+n,"/EBOOT.PBP",sizeof "/EBOOT.PBP");
    char *argv[]={executable,NULL};
    __libcglue_init(1,argv);
    int status=portable_assets_chdir_application(args->root),thread=-1;
    if(!status){
        scePowerSetClockFrequency(333,333,166);
        sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
        callbacks_running=1;
        thread=sceKernelCreateThread("setup callbacks",callbacks,0x11,4096,PSP_THREAD_ATTR_USER,NULL);
        status=thread<0?thread:sceKernelStartThread(thread,0,NULL);
        if(status<0){if(thread>=0)sceKernelDeleteThread(thread);thread=-1;}
        else {
            while(!callbacks_ready)sceKernelDelayThread(1000);
            status=portable_assets_bootstrap();
            if(portable_assets_setup_exiting())status=PORTABLE_ASSETS_CANCELLED;
        }
    }
    /* Never unload code while the ME, exception handler or callback thread
     * can still enter it. A failed fence retains the module and its heap. */
    while(!jgr_setup_me_shutdown()){scePowerTick(0);sceKernelDelayThread(50000);}
    while(crash_shutdown()<0){scePowerTick(0);sceKernelDelayThread(50000);}
    callbacks_running=0;
    if(thread>=0){sceKernelWaitThreadEnd(thread,NULL);sceKernelDeleteThread(thread);}
    fflush(NULL);
    __libcglue_deinit();
    active=0;*args->result=status;
    return 0;
}
int module_start(SceSize size,void *argument)
{
    JgrSetupModuleArgs *args=argument;
    if(active || setup_thread>=0 || size!=sizeof *args || !args || args->abi!=JGR_SETUP_MODULE_ABI ||
       args->bytes!=sizeof *args || !args->result ||
       !memchr(args->root,0,sizeof args->root))return -1;
    active=1;
    setup_thread=sceKernelCreateThread("setup main",run_setup,0x20,128*1024,
                                      PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU,NULL);
    int status=setup_thread<0?setup_thread:sceKernelStartThread(setup_thread,size,args);
    if(status<0){
        if(setup_thread>=0)sceKernelDeleteThread(setup_thread);
        setup_thread=-1;active=0;*args->result=status;
    }
    return 0;
}
int module_stop(SceSize size,void *argument)
{
    (void)size;(void)argument;
    if(active)return -1;
    if(setup_thread>=0){
        int status=sceKernelWaitThreadEnd(setup_thread,NULL);
        if(status<0)return status;
        status=sceKernelDeleteThread(setup_thread);
        if(status<0)return status;
        setup_thread=-1;
    }
    return 0;
}
