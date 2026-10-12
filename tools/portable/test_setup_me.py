"""Synthetic ownership/cache/failure tests for the isolated setup ME client."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
KERNEL = r'''
#include <stddef.h>
typedef int SceUID;
unsigned sceKernelGetSystemTimeLow(void);
void sceKernelDcacheWritebackAll(void);
void sceKernelDcacheWritebackInvalidateRange(void *,unsigned);
void sceKernelDcacheInvalidateRange(void *,unsigned);
int sceKernelDelayThread(unsigned);
int sceKernelLoadModule(const char *,int,void *);
int sceKernelStartModule(int,int,void *,int *,void *);
int sceKernelStopModule(int,int,void *,int *,void *);
int sceKernelUnloadModule(int);
'''
IO = r'''
#define PSP_O_RDONLY 1
int sceIoOpen(const char *,int,int);
int sceIoClose(int);
int sceIoDevctl(const char *,int,void *,int,void *,int);
'''
DRIVER = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define JGR_SETUP_ME_HOST_TEST 1
#include "portable_setup_me.c"
#include "me_protocol.h"
static volatile JgrMeShared *owner;
static unsigned ticks,resets,runs,loads,stops,unloads,all_cache,range_cache,invalidations;
static int emulate,off_file,load_fails,start_fails,job_hangs,watch_hangs,stop_fails,unload_fails,stop_status_fails;
static unsigned char input[128] __attribute__((aligned(64)));
static unsigned char output[128] __attribute__((aligned(64)));
static void reset_core(void){resets++;}
static void boot_core(volatile JgrMeShared *m)
{reset_core();m->start=0;m->done=1;m->boot_status=1;}
static void step(void)
{
    if(!watch_hangs)jgr_me_kernel_step(&owner,reset_core,boot_core);
    if(owner && jgr_me_work_lane(owner)==1 && !job_hangs){
        owner->start=0;owner->result=(int)(owner->param^JGR_ME_PING);owner->done=1;runs++;
    }
}
unsigned sceKernelGetSystemTimeLow(void){return ticks;}
void sceKernelDcacheWritebackAll(void){all_cache++;}
void sceKernelDcacheWritebackInvalidateRange(void *p,unsigned bytes)
{assert(!((uintptr_t)p&63)&&bytes&&!(bytes&63));range_cache++;}
void sceKernelDcacheInvalidateRange(void *p,unsigned bytes)
{
    assert(!((uintptr_t)p&63)&&bytes&&!(bytes&63));
    assert(control && (control->done || control->stopped || module_stopped));invalidations++;
}
int sceKernelDelayThread(unsigned us){ticks+=us;step();return 0;}
int sceKernelLoadModule(const char *path,int a,void *p)
{(void)a;(void)p;assert(!strcmp(path,"verified/helper.prx"));loads++;return load_fails?-8:5;}
int sceKernelStartModule(int m,int a,void *p,int *status,void *q)
{
    (void)m;(void)a;(void)p;(void)q;*status=0;
    if(start_fails){control->boot_status=-4;return -9;}
    owner=control;boot_core(owner);return 0;
}
int sceKernelStopModule(int m,int a,void *p,int *status,void *q)
{
    (void)m;(void)a;(void)p;(void)q;stops++;*status=0;
    if(stop_fails)return -10;
    reset_core();if(owner){owner->stopped=1;owner=NULL;}
    if(stop_status_fails)*status=-11;
    return 0;
}
int sceKernelUnloadModule(int m){(void)m;unloads++;return unload_fails?-12:0;}
int sceIoOpen(const char *p,int flags,int mode)
{(void)mode;assert(flags==PSP_O_RDONLY&&!strcmp(p,"nome"));return off_file?3:-1;}
int sceIoClose(int fd){(void)fd;return 0;}
int sceIoDevctl(const char *p,int a,void *b,int c,void *d,int e)
{(void)a;(void)b;(void)c;(void)d;(void)e;assert(!strcmp(p,"kemulator:"));return emulate?0:-1;}
static int worker(intptr_t argument){return (int)argument;}
static void init(void){assert(jgr_setup_me_init("verified/helper.prx"));assert(loads==1&&runs==1&&all_cache==1);}
static void submit(void)
{
    JgrSetupMeRange ranges[]={{input,sizeof input,JGR_SETUP_ME_READ},{output,sizeof output,JGR_SETUP_ME_READ|JGR_SETUP_ME_WRITE}};
    assert(jgr_setup_me_submit(worker,321,ranges,2));assert(active&&borrowed_count==2);
    assert(!jgr_setup_me_submit(worker,123,ranges,2));
}
static void normal(void)
{
    init();unsigned before=range_cache;submit();assert(range_cache==before+2&&all_cache==1);
    int value=17;assert(jgr_setup_me_poll(&value)==JGR_SETUP_ME_PENDING&&value==17&&!invalidations);
    assert(jgr_setup_me_wait(1000,&value)==JGR_SETUP_ME_COMPLETE&&value==(321^JGR_ME_PING));
    assert(invalidations==1&&!active&&!borrowed_count&&all_cache==1);
    assert(jgr_setup_me_poll(&value)==JGR_SETUP_ME_CANCELLED);
    JgrSetupMeStats stats;jgr_setup_me_stats(&stats);assert(stats.jobs==1&&stats.completed==1&&stats.cancelled==0);
    assert(jgr_setup_me_shutdown()&&!owner&&unloads==1&&!jgr_setup_me_available());
    assert(!jgr_setup_me_init("verified/helper.prx")&&loads==1);
    unsigned old=resets;memset(&setup_control,0xa5,sizeof setup_control);step();assert(resets==old);
}
static void bounds(void)
{
    init();unsigned before=range_cache;
    JgrSetupMeRange r={input,64,JGR_SETUP_ME_READ};
    assert(!jgr_setup_me_submit(NULL,0,&r,1));assert(!jgr_setup_me_submit(worker,0,NULL,1));
    assert(!jgr_setup_me_submit(worker,0,&r,0));assert(!jgr_setup_me_submit(worker,0,&r,9));
    r.address=input+1;assert(!jgr_setup_me_submit(worker,0,&r,1));r.address=input;
    r.bytes=63;assert(!jgr_setup_me_submit(worker,0,&r,1));r.bytes=0;assert(!jgr_setup_me_submit(worker,0,&r,1));
    r.bytes=64;r.flags=0;assert(!jgr_setup_me_submit(worker,0,&r,1));r.flags=4;assert(!jgr_setup_me_submit(worker,0,&r,1));
    r.flags=1;r.address=(void *)(UINTPTR_MAX-63);assert(!jgr_setup_me_submit(worker,0,&r,1));
    JgrSetupMeRange overlap[]={{input,128,1},{input+64,64,2}};assert(!jgr_setup_me_submit(worker,0,overlap,2));
    assert(range_cache==before&&!active&&!invalidations);assert(jgr_setup_me_shutdown());
}
static void suspend_exit(void)
{
    init();submit();unsigned before=runs;jgr_setup_me_note_suspend();assert(active&&!invalidations&&runs==before);
    int value=55;assert(jgr_setup_me_poll(&value)==JGR_SETUP_ME_CANCELLED&&value==55);
    assert(!owner&&!active&&invalidations==1&&runs==before);
    jgr_setup_me_note_resume();assert(!jgr_setup_me_available());jgr_setup_me_note_exit();assert(jgr_setup_me_shutdown());
}
static void timeout(void)
{
    init();submit();job_hangs=1;int value=19;
    assert(jgr_setup_me_wait(300,&value)==JGR_SETUP_ME_CANCELLED&&value==19);
    assert(!active&&!owner&&invalidations==1&&statistics.timeouts==1);assert(jgr_setup_me_shutdown());
}
static void unfenced(void)
{
    init();submit();watch_hangs=job_hangs=stop_fails=1;int value=71;
    assert(jgr_setup_me_wait(300,&value)==JGR_SETUP_ME_PENDING&&value==71);
    assert(active&&borrowed_count==2&&!invalidations&&owner&&!unloads);
    jgr_setup_me_note_exit();assert(!jgr_setup_me_shutdown());assert(active&&!invalidations&&!unloads);
    stop_fails=0;assert(jgr_setup_me_shutdown());assert(!active&&!owner&&invalidations==1&&unloads==1);
}
static void unload_failure(void)
{
    init();submit();unload_fails=1;assert(!jgr_setup_me_shutdown());
    assert(!active&&!owner&&invalidations==1&&module_stopped&&stops==1);
    unsigned old=stops;unload_fails=0;assert(jgr_setup_me_shutdown()&&stops==old);
}
static void stop_status_failure(void)
{
    init();submit();watch_hangs=job_hangs=stop_status_fails=1;
    assert(!jgr_setup_me_shutdown());assert(!active&&!owner&&invalidations==1&&!unloads);
    stop_status_fails=0;assert(jgr_setup_me_shutdown()&&unloads==1);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"normal"))normal();else if(!strcmp(argv[1],"bounds"))bounds();
    else if(!strcmp(argv[1],"suspend"))suspend_exit();else if(!strcmp(argv[1],"timeout"))timeout();
    else if(!strcmp(argv[1],"unfenced"))unfenced();else if(!strcmp(argv[1],"unload"))unload_failure();
    else if(!strcmp(argv[1],"stop_status"))stop_status_failure();
    else if(!strcmp(argv[1],"emulator")){emulate=1;assert(!jgr_setup_me_init("verified/helper.prx")&&!loads&&jgr_setup_me_shutdown());}
    else if(!strcmp(argv[1],"disabled")){off_file=1;assert(!jgr_setup_me_init("verified/helper.prx")&&!loads&&jgr_setup_me_shutdown());}
    else if(!strcmp(argv[1],"load")){load_fails=1;assert(!jgr_setup_me_init("verified/helper.prx")&&loads==1&&!unloads&&jgr_setup_me_shutdown());}
    else if(!strcmp(argv[1],"start")){start_fails=1;assert(!jgr_setup_me_init("verified/helper.prx")&&loads==1&&unloads==1&&jgr_setup_me_shutdown());}
    else if(!strcmp(argv[1],"ping")){job_hangs=1;assert(!jgr_setup_me_init("verified/helper.prx")&&!owner&&unloads==1&&jgr_setup_me_shutdown());}
    else if(!strcmp(argv[1],"pre_exit")){jgr_setup_me_note_exit();assert(!jgr_setup_me_init("verified/helper.prx")&&!loads&&jgr_setup_me_shutdown());}
    else return 2;
    return 0;
}
'''


class SetupMe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('cc'):
            raise unittest.SkipTest('Host compiler unavailable')
        cls.tmp = tempfile.TemporaryDirectory(prefix='jgr-setup-me-')
        cls.work = Path(cls.tmp.name)
        (cls.work/'pspkernel.h').write_text(KERNEL)
        (cls.work/'pspiofilemgr.h').write_text(IO)
        (cls.work/'check.c').write_text(DRIVER)
        cls.exe = cls.work/'check'
        subprocess.run(['cc', '-std=gnu99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I'+str(cls.work), '-I'+str(ROOT/'psp/src'),
                        str(cls.work/'check.c'), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_ownership_cache_ranges_and_lifecycle_failures(self):
        for case in ('normal', 'bounds', 'suspend', 'timeout', 'unfenced', 'unload',
                     'stop_status', 'emulator', 'disabled', 'load', 'start', 'ping', 'pre_exit'):
            with self.subTest(case=case):
                subprocess.run([self.exe, case], check=True)

    def test_host_fallback_never_invokes_worker(self):
        source = self.work/'fallback.c'
        source.write_text('''#include <assert.h>
#include "portable_setup_me.h"
static int worker(intptr_t x){(void)x;assert(0);return 0;}
int main(void){int result=33;assert(!jgr_setup_me_init("unused"));
 assert(!jgr_setup_me_submit(worker,0,0,0));assert(jgr_setup_me_poll(&result)==-1&&result==33);
 assert(jgr_setup_me_wait(1,&result)==-1&&result==33);assert(jgr_setup_me_shutdown());return 0;}
''')
        exe = self.work/'fallback'
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'psp/src'),
                        str(source), str(ROOT/'psp/src/portable_setup_me.c'), '-o', str(exe)], check=True)
        subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
