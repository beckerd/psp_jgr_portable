/* Kernel-only exception capture. File I/O and the screen run in a separate
   sleeping user thread, never on the damaged thread's original stack. */
#include <pspkernel.h>
#include <pspexception.h>
#include "../../src/crash_shared.h"

PSP_MODULE_INFO("JGR Exception Capture",0x1000,1,0);
extern void jgr_crash_exception(void);
/* Exported by PSPSDK's libpspkernel, absent from its public header. */
extern int sceKernelReleaseDefaultExceptionHandler(void (*handler)(void));
JgrCrashRegs jgr_crash_regs;
unsigned char jgr_crash_stack[4096] __attribute__((aligned(16)));
volatile unsigned jgr_crash_latched;
static volatile JgrCrashShared *shared;
static int installed;

void __attribute__((noreturn)) jgr_crash_trap(void)
{
    /* exception.S saved the registers before entering C, changed to our
       private stack, restored this module's GP and cleared the K1 mask. */
    if(shared) {
        const uint32_t *src=(const uint32_t *)&jgr_crash_regs;
        volatile uint32_t *dst=(volatile uint32_t *)&shared->regs;
        for(unsigned i=0;i<sizeof(jgr_crash_regs)/4;i++)dst[i]=src[i];
        shared->fault_thread=sceKernelGetThreadId();
        uint32_t sp=jgr_crash_regs.gpr[29];
        unsigned words=jgr_crash_stack_words(sp);
        for(unsigned i=0;i<words;i++)shared->stack[i]=((volatile uint32_t *)sp)[i];
        shared->stack_count=words;
        __asm__ volatile("sync" ::: "memory");
        shared->state=JGR_CRASH_READY;
        __asm__ volatile("sync" ::: "memory");
        sceKernelWakeupThread(shared->reporter);
    }
    /* Never resume the instruction that faulted. No automatic reboot. */
    for(;;)sceKernelSleepThread();
}

int module_start(SceSize args,void *argp)
{
    (void)args;(void)argp;
    /* Explicit, versioned locator as used by our existing ME helper. ARK
       module argument plumbing is not reliable on all supported setups. */
    const uint32_t *p=(const uint32_t *)0x08804000u,*end=(const uint32_t *)0x0a000000u;
    for(;p+6<end;p+=4) {
        if(p[0]!=JGR_CRASH_MAGIC0 || p[1]!=JGR_CRASH_MAGIC1 ||
           p[2]!=JGR_CRASH_MAGIC2 || p[3]!=JGR_CRASH_MAGIC3)continue;
        uint32_t a=p[4];
        if(!(a&0x40000000u) || (a&63u) || p[5]!=sizeof(JgrCrashShared) ||
           !jgr_crash_user_range(a&~0x40000000u,sizeof(JgrCrashShared)))continue;
        volatile JgrCrashShared *s=(volatile JgrCrashShared *)a;
        if(s->abi!=JGR_CRASH_ABI || s->bytes!=sizeof(*s) ||
           s->installed!=(int)JGR_CRASH_PENDING || s->reporter<0 || s->state)continue;
        shared=s;break;
    }
    if(!shared)return -1;
    int result=sceKernelRegisterDefaultExceptionHandler((void *)jgr_crash_exception);
    shared->installed=result;
    installed=result>=0;
    return result;
}

int module_stop(SceSize args,void *argp)
{
    (void)args;(void)argp;
    /* Do not unload code while a faulting thread is parked inside it. */
    if(jgr_crash_latched)return -1;
    if(installed) {
        int result=sceKernelReleaseDefaultExceptionHandler(jgr_crash_exception);
        if(result<0)return result;
        installed=0;
    }
    if(shared)shared->installed=-1;
    shared=0;
    return 0;
}
