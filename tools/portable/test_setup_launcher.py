"""Converter module must retire every asynchronous owner before freeing libc."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
HEADERS={
'pspkernel.h':r'''
#ifndef TEST_PSP_KERNEL
#define TEST_PSP_KERNEL
#include <stddef.h>
#define PSP_MODULE_INFO(a,b,c,d)
#define PSP_HEAP_SIZE_KB(a)
#define PSP_HEAP_THRESHOLD_SIZE_KB(a)
#define PSP_THREAD_ATTR_USER 1
#define PSP_THREAD_ATTR_VFPU 2
#define PSP_MEMORY_PARTITION_USER 2
typedef int SceUID;
typedef unsigned SceSize;
typedef long long SceOff;
typedef struct {unsigned size;int mpidstack;unsigned stacksize;int priority;unsigned attribute;} SceKernelSMOption;
int sceKernelCreateCallback(const char*,int(*)(int,int,void*),void*);
int sceKernelRegisterExitCallback(int);
int sceKernelDeleteCallback(int);
int sceKernelCreateThread(const char*,int(*)(SceSize,void*),int,int,int,void*);
int sceKernelStartThread(int,int,void*);
int sceKernelWaitThreadEnd(int,void*);
int sceKernelDeleteThread(int);
int sceKernelDelayThread(unsigned);
int sceKernelDelayThreadCB(unsigned);
unsigned sceKernelTotalFreeMemSize(void);
unsigned sceKernelMaxFreeMemSize(void);
int sceKernelLoadModule(const char*,int,void*);
int sceKernelStartModule(int,unsigned,void*,int*,SceKernelSMOption*);
int sceKernelStopModule(int,unsigned,void*,int*,void*);
int sceKernelUnloadModule(int);
#endif
''',
'pspiofilemgr.h':r'''
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define PSP_O_RDONLY O_RDONLY
#define PSP_O_WRONLY O_WRONLY
#define PSP_O_CREAT O_CREAT
#define PSP_O_TRUNC O_TRUNC
#define PSP_O_APPEND O_APPEND
#define PSP_SEEK_SET SEEK_SET
#define PSP_SEEK_END SEEK_END
#define FIO_S_ISDIR S_ISDIR
typedef struct stat SceIoStat;
int sceIoOpen(const char*,int,int);
int sceIoRead(int,void*,unsigned);
int sceIoWrite(int,const void*,unsigned);
int sceIoClose(int);
SceOff sceIoLseek(int,SceOff,int);
int sceIoGetstat(const char*,SceIoStat*);
int sceIoMkdir(const char*,int);
int sceIoRemove(const char*);
int sceIoRename(const char*,const char*);
int sceIoSync(const char*,int);
''',
'pspctrl.h':r'''
#define PSP_CTRL_MODE_ANALOG 1
int sceCtrlSetSamplingCycle(int);
int sceCtrlSetSamplingMode(int);
''',
'psppower.h':r'''
#define PSP_POWER_CB_SUSPENDING 1
#define PSP_POWER_CB_STANDBY 2
#define PSP_POWER_CB_RESUME_COMPLETE 4
int scePowerTick(int);
int scePowerSetClockFrequency(int,int,int);
int scePowerRegisterCallback(int,int);
int scePowerUnregisterCallback(int);
'''}
DRIVER=r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "portable_launcher.c"
static int scenario,exit_requested,me_stops,crash_stops,waited,deleted,heap_live,init_calls;
static int deleted_callbacks,unregistered,power_ticks,setup_started,setup_joined,in_start;
static int (*setup_entry)(SceSize,void*);
static JgrSetupModuleArgs setup_args;
void __libcglue_init(int argc,char **argv){assert(argc==1&&!strcmp(argv[0],"ms0:/PSP/GAME/JGR/EBOOT.PBP"));heap_live=1;init_calls++;}
void __libcglue_deinit(void){assert(heap_live&&me_stops==3&&crash_stops==3);assert(scenario==3||scenario==4||scenario==5||waited);heap_live=0;}
void portable_assets_setup_request_exit(void){exit_requested=1;}
int portable_assets_setup_exiting(void){return exit_requested;}
void portable_assets_setup_tick(void){}
int portable_assets_chdir_application(const char *p){assert(!strcmp(p,"ms0:/PSP/GAME/JGR"));return scenario==5?-5:0;}
int portable_assets_bootstrap(void){assert(heap_live&&!in_start);if(scenario==2)exit_requested=1;return scenario==1?-8:0;}
int jgr_setup_me_shutdown(void){assert(heap_live&&module_stop(0,0)<0);return ++me_stops>=3;}
int crash_shutdown(void){assert(me_stops==3&&heap_live);return ++crash_stops>=3?0:-1;}
void jgr_setup_me_note_suspend(void){}
void jgr_setup_me_note_resume(void){}
int sceKernelCreateCallback(const char *s,int(*f)(int,int,void*),void *p){(void)s;(void)f;(void)p;return 3;}
int sceKernelRegisterExitCallback(int id){assert(id==3);return 0;}
int sceKernelDeleteCallback(int id){assert(id==3);deleted_callbacks++;return 0;}
int scePowerRegisterCallback(int a,int b){assert(a==-1&&b==3);return 5;}
int scePowerUnregisterCallback(int a){assert(a==5);unregistered++;return 0;}
int sceKernelCreateThread(const char *s,int(*f)(SceSize,void*),int p,int z,int a,void *b)
{(void)p;(void)a;(void)b;
 if(!strcmp(s,"setup main")){assert(in_start&&z==128*1024&&!heap_live);setup_entry=f;return scenario==8?-18:8;}
 assert(!in_start);return scenario==3?-3:7;}
int sceKernelStartThread(int a,int b,void *p){
 if(a==8){assert(in_start&&b==sizeof setup_args);memcpy(&setup_args,p,sizeof setup_args);setup_started++;return scenario==9?-19:0;}
 assert(a==7&&!in_start);callbacks_ready=1;return scenario==4?-4:0;}
int sceKernelWaitThreadEnd(int a,void *b){(void)b;if(a==8){assert(!active&&!heap_live);setup_joined++;return scenario==10?-20:0;}assert(a==7&&!callbacks_running&&me_stops==3&&crash_stops==3);waited++;return 0;}
int sceKernelDeleteThread(int a){if(a==8){assert(!active||in_start);return scenario==11?-21:0;}assert(a==7);deleted++;return 0;}
int sceKernelDelayThread(unsigned us){(void)us;return 0;}
int sceKernelDelayThreadCB(unsigned us){(void)us;callbacks_running=0;return 0;}
int scePowerTick(int n){(void)n;power_ticks++;return 0;}
int scePowerSetClockFrequency(int a,int b,int c){(void)a;(void)b;(void)c;return 0;}
int sceCtrlSetSamplingCycle(int n){(void)n;return 0;}
int sceCtrlSetSamplingMode(int n){(void)n;return 0;}
int main(int argc,char **argv){
 assert(argc==2);scenario=atoi(argv[1]);
 if(scenario==6){callbacks_running=1;callbacks(0,0);assert(callbacks_ready&&deleted_callbacks==2&&unregistered==1&&power_ticks==1);return 0;}
 volatile int result=JGR_SETUP_PENDING;
 JgrSetupModuleArgs args={JGR_SETUP_MODULE_ABI,sizeof args,"ms0:/PSP/GAME/JGR",&result};
 if(scenario==7){args.abi=0;assert(module_start(sizeof args,&args)<0&&!init_calls);return 0;}
 in_start=1;assert(module_start(sizeof args,&args)==0);in_start=0;
 assert(!heap_live&&!init_calls);
 if(scenario==8||scenario==9){assert(result==(scenario==8?-18:-19)&&!active&&module_stop(0,0)==0);return 0;}
 assert(setup_started==1&&active&&result==JGR_SETUP_PENDING&&module_stop(0,0)<0);
 assert(setup_entry(sizeof setup_args,&setup_args)==0);
 if(scenario==10||scenario==11){assert(module_stop(0,0)==(scenario==10?-20:-21)&&setup_thread==8);return 0;}
 assert(!heap_live&&!active&&module_stop(0,0)==0);
 assert(result==(scenario==0?0:scenario==1?-8:scenario==2?PORTABLE_ASSETS_CANCELLED:scenario==3?-3:scenario==4?-4:-5));
 assert(deleted==(scenario!=3&&scenario!=5)&&setup_joined==1);
 return 0;
}
'''
class Launcher(unittest.TestCase):
    def test_module_cleanup_and_cancellation(self):
        with tempfile.TemporaryDirectory(prefix='jgr-converter-') as tmp:
            d=Path(tmp)
            for name,content in HEADERS.items():(d/name).write_text(content)
            (d/'driver.c').write_text(DRIVER)
            exe=d/'driver'
            subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-O1','-g',
                '-fsanitize=address,undefined','-DJGR_SETUP_ME=1','-DJGR_SETUP_CRASH=1','-DJGR_DEVICE_SETUP=1','-DJGR_PORTABLE_ASSETS=1',
                '-I'+str(d),'-I'+str(ROOT/'psp/src'),str(d/'driver.c'),'-o',str(exe)],check=True)
            for scenario in range(12):
                with self.subTest(scenario=scenario):subprocess.run([str(exe),str(scenario)],cwd=d,check=True)
if __name__=='__main__':unittest.main()
