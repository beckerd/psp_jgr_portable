/* Setup-only executable. The native game is loaded after all converters and
 * their memory have been discarded by executable replacement. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#undef sceIoOpen
#undef sceIoGetstat
#undef sceIoRemove
#undef sceIoRename
#include "portable_bundle.h"
#if JGR_SETUP_CRASH
#include "crash.h"
#endif
#if JGR_SETUP_ME
#include "portable_setup_me.h"
#endif
#include <pspkernel.h>
#include <psploadexec.h>
#include <pspiofilemgr.h>
#include <systemctrl.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <psppower.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

PSP_MODULE_INFO("JGR Portable Setup",0,1,0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(-256);

static volatile int callbacks_ready;
static int quit(int argument1,int argument2,void *user)
{(void)argument1;(void)argument2;(void)user;portable_assets_setup_request_exit();return 0;}
#if JGR_SETUP_ME
static int power_event(int unknown,int flags,void *user)
{
    (void)unknown;(void)user;
    if(flags&(PSP_POWER_CB_SUSPENDING|PSP_POWER_CB_STANDBY))jgr_setup_me_note_suspend();
    if(flags&PSP_POWER_CB_RESUME_COMPLETE)jgr_setup_me_note_resume();
    return 0;
}
#endif
static int callbacks(SceSize size,void *user)
{
    (void)size;(void)user;
    int id=sceKernelCreateCallback("setup exit",quit,NULL);
    if(id>=0)sceKernelRegisterExitCallback(id);
#if JGR_SETUP_ME
    int power=sceKernelCreateCallback("setup power",power_event,NULL);
    /* If power notification cannot be installed, never start ME work. */
    if(power<0 || scePowerRegisterCallback(-1,power)<0)jgr_setup_me_note_suspend();
#endif
    callbacks_ready=1;
    /* Individual conversion jobs can run for minutes without a UI update. */
    for(;;){scePowerTick(0);portable_assets_setup_tick();sceKernelDelayThreadCB(1000000);}
    return 0;
}
static void stop_helpers(void)
{
#if JGR_SETUP_ME
    /* A failed physical fence must retain the executable and borrowed job
     * allocations. Retry here; neither LoadExec nor ExitGame is safe yet. */
    while(!jgr_setup_me_shutdown()){scePowerTick(0);sceKernelDelayThread(50000);}
#endif
#if JGR_SETUP_CRASH
    /* The exception helper holds pointers into this executable. It must
       unregister before either LoadExec or ExitGame can retire those pages. */
    while(crash_shutdown()<0){scePowerTick(0);sceKernelDelayThread(50000);}
#endif
}
static int return_to_xmb(void)
{
    stop_helpers();
    SceCtrlData pad;
    while(!portable_assets_setup_exiting()){
        sceCtrlReadBufferPositive(&pad,1);if(pad.Buttons&PSP_CTRL_START)break;sceKernelDelayThread(50000);
    }
    sceKernelExitGame();return 1;
}
static void handoff_error(int status,const char *message)
{
    FILE *log=fopen("data/log/setup.log","a");if(log){fprintf(log,"HANDOFF status=%d detail=%s\n",status,message);fclose(log);}
    pspDebugScreenInit();pspDebugScreenPrintf("JGR Portable\n\n%s\nError: %08x\n\nPress START to return to the XMB.\n",message,(unsigned)status);
}
static int start_game(const char *game,void *arguments,size_t argument_bytes)
{
    /* PPSSPP implements the ordinary API without the hardware boot-source
       restriction. On the PSP, use the CFW user bridge for homebrew launched
       from Memory Stick (or the Go's internal storage). Plain LoadExec rejects
       this boot context with SCE_KERNEL_ERROR_ILLEGAL_PERM_CALL. */
    if(sceIoDevctl("kemulator:",3,NULL,0,NULL,0)>=0){
        struct SceKernelLoadExecParam parameter={sizeof parameter,argument_bytes,arguments,NULL};
        return sceKernelLoadExec(game,&parameter);
    }
    struct SceKernelLoadExecVSHParam parameter;
    memset(&parameter,0,sizeof parameter);
    parameter.size=sizeof parameter;
    parameter.args=argument_bytes;
    parameter.argp=arguments;
    parameter.key="game";
    if(!strncmp(game,"ef0:/",5))return sctrlKernelLoadExecVSHEf2(game,&parameter);
    return sctrlKernelLoadExecVSHMs2(game,&parameter);
}
int main(void)
{
    scePowerSetClockFrequency(333,333,166);
    sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    int thread=sceKernelCreateThread("setup callbacks",callbacks,0x11,4096,PSP_THREAD_ATTR_USER,0);
    int thread_status=thread<0?thread:sceKernelStartThread(thread,0,NULL);
    if(thread_status<0){handoff_error(thread_status,"Could not start setup power protection.");return return_to_xmb();}
    while(!callbacks_ready)sceKernelDelayThread(1000);
    char root[512],game[PORTABLE_BUNDLE_PATH_MAX],arguments[1400];
    if(!getcwd(root,sizeof root) || portable_assets_chdir_application(root)<0){handoff_error(PORTABLE_ASSETS_PATH,"Cannot locate the application folder.");return return_to_xmb();}
    if(root[strlen(root)-1]==':')strcat(root,"/");
    if(portable_assets_bootstrap()<0)return return_to_xmb();
    if(portable_assets_setup_exiting())return return_to_xmb();
    /* Bootstrap has already selected the current EBOOT's prepared runtime.
       Rechecking the whole bundle here repeats Memory Stick reads and CRCs. */
    int status=portable_bundle_path(PORTABLE_BUNDLE_GAME,game,sizeof game);
    if(status<0){handoff_error(status,"The bundled game executable is unavailable.");return return_to_xmb();}
    size_t a=strlen(game)+1,b=sizeof "--jgr-root",c=strlen(root)+1;
    if(a+b+c>sizeof arguments){handoff_error(PORTABLE_ASSETS_PATH,"The application path is too long.");return return_to_xmb();}
    memcpy(arguments,game,a);memcpy(arguments+a,"--jgr-root",b);memcpy(arguments+a+b,root,c);
    stop_helpers();
    if(portable_assets_setup_exiting())return return_to_xmb();
    scePowerTick(0);
    status=start_game(game,arguments,a+b+c);
    handoff_error(status,"Could not start the game executable.");return return_to_xmb();
}
