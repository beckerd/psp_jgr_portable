#ifndef JGR_CRASH_SHARED_H
#define JGR_CRASH_SHARED_H
#include <stddef.h>
#include <stdint.h>

/* Fixed-width ABI between the user game and the kernel exception helper. */
#define JGR_CRASH_MAGIC0 0x4352474au
#define JGR_CRASH_MAGIC1 0x48534152u
#define JGR_CRASH_MAGIC2 0x31504143u
#define JGR_CRASH_MAGIC3 0x91a7e50bu
#define JGR_CRASH_ABI 1u
#define JGR_CRASH_PENDING 0x7fffffffu
#define JGR_CRASH_STACK_WORDS 64u
#define JGR_CRASH_READY 1u
#define JGR_CRASH_SAVED 2u
#define JGR_CRASH_SAVE_FAILED 3u

/* These offsets are also used in tools/crashprx/exception.S. */
typedef struct {
    uint32_t gpr[32];
    uint32_t status,lo,hi,badvaddr,cause,epc;
    uint32_t fpr[32],fsr,fir;
} JgrCrashRegs;
typedef char JgrCrashRegsLayout[(offsetof(JgrCrashRegs,status)==128 &&
    offsetof(JgrCrashRegs,epc)==148 && offsetof(JgrCrashRegs,fpr)==152 &&
    sizeof(JgrCrashRegs)==288) ? 1:-1];

typedef struct {
    uint32_t abi,bytes;
    int32_t installed,reporter,main_thread;
    uint32_t state;
    int32_t fault_thread;
    uint32_t anchor,text_addr,text_size;
    /* Last ordinary profiler sample, NOT a heap walk in the fault handler. */
    uint32_t sample_frame,sample_stage,sample_state;
    uint32_t heap_free,system_free,system_largest;
    JgrCrashRegs regs;
    uint32_t stack_count,stack[JGR_CRASH_STACK_WORDS];
} JgrCrashShared;

static inline int jgr_crash_user_range(uint32_t address,uint32_t bytes)
{
    return address>=0x08800000u && address<0x0a000000u &&
           bytes<=0x0a000000u-address;
}
static inline unsigned jgr_crash_stack_words(uint32_t sp)
{
    if((sp&3u) || !jgr_crash_user_range(sp,4))return 0;
    unsigned words=(0x0a000000u-sp)/4;
    return words<JGR_CRASH_STACK_WORDS ? words:JGR_CRASH_STACK_WORDS;
}
#endif
