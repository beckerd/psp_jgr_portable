"""Setup launcher must fence workers before either executable replacement."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
HEADERS={
'pspkernel.h':r'''
#include <stddef.h>
#define PSP_MODULE_INFO(a,b,c,d)
#define PSP_MAIN_THREAD_ATTR(a)
#define PSP_MAIN_THREAD_STACK_SIZE_KB(a)
#define PSP_HEAP_SIZE_KB(a)
#define THREAD_ATTR_USER 0
#define THREAD_ATTR_VFPU 0
#define PSP_THREAD_ATTR_USER 0
typedef unsigned SceSize;
int sceKernelCreateCallback(const char*,int(*)(int,int,void*),void*);
int sceKernelRegisterExitCallback(int);
int sceKernelCreateThread(const char*,int(*)(SceSize,void*),int,int,int,int);
int sceKernelStartThread(int,int,void*);
int sceKernelDelayThread(unsigned);
int sceKernelDelayThreadCB(unsigned);
int sceKernelExitGame(void);
''',
'pspiofilemgr.h':'int sceIoDevctl(const char*,unsigned,void*,unsigned,void*,unsigned);',
'psploadexec.h':r'''
struct SceKernelLoadExecParam {size_t size,args;void *argp;void *key;};
int sceKernelLoadExec(const char*,struct SceKernelLoadExecParam*);
''',
'systemctrl.h':r'''
struct SceKernelLoadExecVSHParam {
 size_t size,args;void *argp;const char *key;
 unsigned vshmain_args_size;void *vshmain_args;char *configfile;unsigned unk4,unk5;
};
int sctrlKernelLoadExecVSHMs2(const char*,struct SceKernelLoadExecVSHParam*);
int sctrlKernelLoadExecVSHEf2(const char*,struct SceKernelLoadExecVSHParam*);
''',
'pspctrl.h':r'''
#define PSP_CTRL_START 8
#define PSP_CTRL_MODE_ANALOG 1
typedef struct {unsigned Buttons;} SceCtrlData;
int sceCtrlReadBufferPositive(SceCtrlData*,int);
int sceCtrlSetSamplingCycle(int);
int sceCtrlSetSamplingMode(int);
''',
'pspdebug.h':'int pspDebugScreenInit(void);int pspDebugScreenPrintf(const char*,...);',
'psppower.h':r'''
#define PSP_POWER_CB_SUSPENDING 1
#define PSP_POWER_CB_STANDBY 2
#define PSP_POWER_CB_RESUME_COMPLETE 4
int scePowerTick(int);
int scePowerSetClockFrequency(int,int,int);
int scePowerRegisterCallback(int,int);
'''}
DRIVER=r'''
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define main setup_main
#include "portable_launcher.c"
#undef main
static jmp_buf done;
static int scenario,exit_requested,requests,shutdown_calls,fenced,loads,exits,delays,suspends,resumes;
static int crash_stops,crash_retired;
static int ordinary_loads,ms_loads,ef_loads;
void portable_assets_setup_request_exit(void){exit_requested=1;requests++;}
int portable_assets_setup_exiting(void){return exit_requested;}
void portable_assets_setup_tick(void){}
int portable_assets_chdir_application(const char *p){(void)p;return 0;}
int portable_assets_bootstrap(void){if(scenario==1)return -1;if(scenario==2)portable_assets_setup_request_exit();return 0;}
int portable_bundle_prepare(const char *p,PortableBundleResult *r)
{(void)p;(void)r;assert(!"Bootstrap already prepared the runtime");return -1;}
int portable_bundle_path(unsigned role,char *p,size_t n)
{assert(role==PORTABLE_BUNDLE_GAME&&n>80);if(scenario==3)portable_assets_setup_request_exit();
 if(scenario==7)return PORTABLE_BUNDLE_MISSING;
 strcpy(p,scenario==6?"ef0:/PSP/GAME/JGR/data/cache/game.prx":"ms0:/PSP/GAME/JGR/data/cache/game.prx");return 0;}
int jgr_setup_me_shutdown(void){shutdown_calls++;if(shutdown_calls<3)return 0;fenced=1;return 1;}
int crash_shutdown(void){assert(fenced);crash_stops++;if(crash_stops<3)return -1;crash_retired=1;return 0;}
void jgr_setup_me_note_suspend(void){suspends++;}
void jgr_setup_me_note_resume(void){resumes++;}
int sceKernelCreateCallback(const char *s,int(*fn)(int,int,void*),void *p){(void)s;(void)fn;(void)p;return 1;}
int sceKernelRegisterExitCallback(int a){(void)a;return 0;}
int scePowerRegisterCallback(int a,int b){(void)a;(void)b;return 0;}
int sceKernelCreateThread(const char *s,int(*f)(SceSize,void*),int p,int z,int a,int b)
{(void)s;(void)f;(void)p;(void)z;(void)a;(void)b;return scenario==4?-1:1;}
int sceKernelStartThread(int a,int b,void *p){(void)a;(void)b;(void)p;callbacks_ready=1;return 0;}
int sceKernelDelayThread(unsigned us){(void)us;assert(++delays<20);return 0;}
int sceKernelDelayThreadCB(unsigned us){return sceKernelDelayThread(us);}
int sceKernelExitGame(void){assert(fenced&&crash_retired);exits++;longjmp(done,1);}
static void check_arguments(const char *s,size_t bytes,void *args)
{
 assert(fenced&&crash_retired&&!exit_requested);
 const char *a=args;assert(!strcmp(a,s));a+=strlen(a)+1;
 assert(!strcmp(a,"--jgr-root"));a+=strlen(a)+1;
 char root[512];assert(getcwd(root,sizeof root)&&!strcmp(a,root));
 assert(bytes==(size_t)(a-(char*)args)+strlen(a)+1);loads++;
}
int sceKernelLoadExec(const char *s,struct SceKernelLoadExecParam *p)
{
 assert(scenario==5&&p->size==sizeof *p&&!p->key);
 ordinary_loads++;check_arguments(s,p->args,p->argp);return -1;
}
static int cfw_load(const char *s,struct SceKernelLoadExecVSHParam *p)
{
 assert(p->size==sizeof *p&&!strcmp(p->key,"game"));
 assert(!p->vshmain_args_size&&!p->vshmain_args&&!p->configfile&&!p->unk4&&!p->unk5);
 check_arguments(s,p->args,p->argp);return (int)0x80020149u;
}
int sctrlKernelLoadExecVSHMs2(const char *s,struct SceKernelLoadExecVSHParam *p)
{assert(scenario==0&&!strncmp(s,"ms0:/",5));ms_loads++;return cfw_load(s,p);}
int sctrlKernelLoadExecVSHEf2(const char *s,struct SceKernelLoadExecVSHParam *p)
{assert(scenario==6&&!strncmp(s,"ef0:/",5));ef_loads++;return cfw_load(s,p);}
int sceIoDevctl(const char *dev,unsigned command,void *in,unsigned ni,void *out,unsigned no)
{assert(fenced&&crash_retired&&!strcmp(dev,"kemulator:")&&command==3&&!in&&!ni&&!out&&!no);return scenario==5?0:-1;}
int sceCtrlReadBufferPositive(SceCtrlData *p,int n){(void)n;p->Buttons=PSP_CTRL_START;return 1;}
int sceCtrlSetSamplingCycle(int n){(void)n;return 0;}
int sceCtrlSetSamplingMode(int n){(void)n;return 0;}
int pspDebugScreenInit(void){return 0;}
int pspDebugScreenPrintf(const char *s,...){(void)s;return 0;}
int scePowerTick(int n){(void)n;return 0;}
int scePowerSetClockFrequency(int a,int b,int c){(void)a;(void)b;(void)c;return 0;}
int main(int argc,char **argv)
{
 assert(argc==2);scenario=argv[1][0]-'0';
 quit(0,0,NULL);assert(requests==1&&!exits&&!shutdown_calls);exit_requested=requests=0;
 power_event(0,PSP_POWER_CB_SUSPENDING,NULL);power_event(0,PSP_POWER_CB_RESUME_COMPLETE,NULL);
 assert(suspends==1&&resumes==1);
 if(!setjmp(done)){setup_main();assert(0);}
 assert(exits==1&&shutdown_calls>=3&&crash_stops>=3&&delays>=4);
 assert(loads==(scenario==0||scenario==5||scenario==6));
 assert(ms_loads==(scenario==0)&&ordinary_loads==(scenario==5)&&ef_loads==(scenario==6));
 return 0;
}
'''
class Launcher(unittest.TestCase):
    def test_every_handoff_waits_for_physical_fence(self):
        with tempfile.TemporaryDirectory(prefix='jgr-launcher-') as tmp:
            d=Path(tmp)
            for name,content in HEADERS.items():(d/name).write_text(content)
            source=d/'driver.c';source.write_text(DRIVER)
            exe=d/'driver'
            subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-O1','-g',
                '-fsanitize=address,undefined','-DJGR_SETUP_ME=1','-DJGR_SETUP_CRASH=1','-DJGR_DEVICE_SETUP=1','-DJGR_PORTABLE_ASSETS=1',
                '-I'+str(d),'-I'+str(ROOT/'psp/src'),str(source),'-o',str(exe)],check=True)
            for scenario in range(8):
                with self.subTest(scenario=scenario):subprocess.run([str(exe),str(scenario)],cwd=d,check=True)
if __name__=='__main__':unittest.main()
