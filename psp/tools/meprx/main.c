/* Media Engine initialization, cache synchronization and work dispatch.
   No ForUser calls on the ME or at startup. */
#include <pspkernel.h>
#include <pspsysreg.h>
#include <pspsysevent.h>
#include "../../src/me_protocol.h"
PSP_MODULE_INFO("JGR Media Engine",0x1000,1,1);
extern unsigned char me_stub[],me_stub_end[];
static volatile JgrMeShared *control;
static int watcher=-1;
static void me_dcache_wbinv(void)
{
    for(int i=0;i<8192;i+=64){__builtin_allegrex_cache(0x14,i);__builtin_allegrex_cache(0x14,i);}
}
static void __attribute__((noinline,noreturn)) me_loop(volatile JgrMeShared *m)
{
    unsigned fcsr,idle=0;
    __asm__ volatile("cfc1 %0,$31" : "=r"(fcsr));fcsr&=~0xf80u;
    __asm__ volatile("ctc1 %0,$31" : : "r"(fcsr));
    while(m->init && !m->stop) {
        unsigned lane=jgr_me_work_lane(m);
        if(!lane){for(int i=0;i<64;i++)__asm__ volatile("nop");if(!(++idle&1023))m->spins++;continue;}
        if(lane==1) {
            m->start=0;me_dcache_wbinv();
            m->result=((int (*)(int))m->func)((int)m->param);
            me_dcache_wbinv();JGR_ME_SYNC();m->alive++;m->done=1;
        } else {
            unsigned begin,end;me_dcache_wbinv();
            __asm__ volatile("mfc0 %0,$9" : "=r"(begin));
            int done=((int (*)(int))m->bg_func)((int)m->bg_param);
            __asm__ volatile("mfc0 %0,$9" : "=r"(end));
            me_dcache_wbinv();JGR_ME_SYNC();
            unsigned elapsed=end-begin;m->bg_cycles+=elapsed;
            if(elapsed>m->bg_max_cycles)m->bg_max_cycles=elapsed;
            m->bg_chunks++;if(done)m->bg_state=JGR_ME_BG_DONE;
        }
    }
    for(;;)__asm__ volatile("nop");
}
static void reset_core(void)
{sceSysregMeResetEnable();}
static void sleep_core(void)
{reset_core();sceSysregMeBusClockDisable();}
#include "power.inc"
static void boot_core(volatile JgrMeShared *m)
{
    reset_core();m->start=0;m->done=1;m->result=0;m->init=1;m->bg_state=JGR_ME_BG_CANCELLED;
    volatile unsigned *dst=(volatile unsigned *)0xbfc00040u;
    const unsigned *src=(const unsigned *)me_stub;
    for(unsigned i=0;i<(unsigned)(me_stub_end-me_stub+3)/4;i++)dst[i]=src[i];
    *(volatile unsigned *)0xbfc00600u=(unsigned)me_loop;
    *(volatile unsigned *)0xbfc00604u=(unsigned)m;
    for(int i=0;i<16384;i+=64){__builtin_allegrex_cache(0x14,i);__builtin_allegrex_cache(0x14,i);}
    __asm__ volatile("sync" : : : "memory");
    if(!m->stop && !m->sleeping) {
        sceSysregMeBusClockEnable();sceSysregMeResetDisable();sceSysregVmeResetDisable();
    }
    m->boot_status=1;
}
static int watch(SceSize args,void *argp)
{
    (void)args;(void)argp;
    for(;;) {
        /* A power event may interrupt this thread. Serialize the short
           reset/bootstrap transaction with that event, never its wait. */
        unsigned key=sceKernelCpuSuspendIntr();
        int live=power_watch_step(boot_core);
        sceKernelCpuResumeIntr(key);
        if(!live)break;
        sceKernelDelayThread(1000);
    }
    return 0;
}
int module_start(SceSize args,void *argp)
{
    (void)args;(void)argp;
    /* Avoid firmware-specific module argument handling by finding our
       explicit locator in the 32-MiB app's data image during startup. */
    const unsigned *p=(const unsigned *)0x08804000u,*end=(const unsigned *)0x0a000000u;
    for(;p+6<end;p+=4)if(p[0]==JGR_ME_MAGIC0 && p[1]==JGR_ME_MAGIC1 && p[2]==JGR_ME_MAGIC2 && p[3]==JGR_ME_MAGIC3) {
        unsigned a=p[4],physical=a&~0x40000000u;
        if(p[5]!=sizeof(JgrMeShared) || (a&63) || !(a&0x40000000u) || physical<0x08800000u || physical>0x0a000000u-sizeof(JgrMeShared))continue;
        control=(volatile JgrMeShared *)a;break;
    }
    if(!control)return -1;
    watcher=sceKernelCreateThread("jgr_me_fence",watch,0x18,0x1000,0,NULL);
    if(watcher<0){control->boot_status=watcher;control=NULL;return -2;}
    /* Only this startup thread touches reset until boot completes. Then
       the watchdog exclusively owns suspend/reboot/stop handshakes. */
    int error=power_claim();
    if(error<0){control->boot_status=error;control=NULL;sceKernelDeleteThread(watcher);watcher=-1;return error;}
    unsigned key=sceKernelCpuSuspendIntr();
    core_replaced=1;boot_core(control);
    sceKernelCpuResumeIntr(key);
    if(sceKernelStartThread(watcher,0,NULL)<0) {
        reset_core();control->stopped=1;sceKernelDeleteThread(watcher);watcher=-1;control=NULL;
        /* Retain the module if unregistering its event handler fails. */
        return power_release()<0 ? 0:-3;
    }
    return 0;
}
int module_stop(SceSize args,void *argp)
{
    (void)args;(void)argp;
    if(watcher>=0){sceKernelTerminateDeleteThread(watcher);watcher=-1;}
    reset_core();__asm__ volatile("sync" : : : "memory");
    if(control){control->bg_state=JGR_ME_BG_CANCELLED;control->stopped=1;control=NULL;}
    return power_release();
}
