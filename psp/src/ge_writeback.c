#include "diagnostics.h"
#include <stdint.h>
#include <pspkernel.h>
#include "ge_writeback.h"

/* PSPSDK guInternal.h's list prefix, also checked against the installed
   sceGuStart/CheckList disassembly. No context state/layout is duplicated.
   Both pointers must use the same alias for CheckList's subtraction. */
#include "ge_list.h"
static int active;
static int allocating;
static unsigned publications,writeback_us,frames;

void ge_writeback_begin(void *arena)
{
#ifndef JGR_UNCACHED_GE_LIST
    /* Start writes a small prefix through the SDK's uncached alias. The
       previous list is fenced and its final writeback is complete. Discard
       stale cached copies of ONLY that prefix before switching aliases. */
    uintptr_t start=(uintptr_t)arena&~(uintptr_t)0x40000000u;
    uintptr_t current=(uintptr_t)gu_list->current&~(uintptr_t)0x40000000u;
    if(current>start)sceKernelDcacheInvalidateRange((void *)start,(current-start+63)&~63u);
    gu_list->start=(unsigned *)start;gu_list->current=(unsigned *)current;
    active=1;frames++;
#else
    (void)arena;
#endif
}
void ge_writeback_end(void){active=0;}
void ge_writeback_profile(unsigned out[3])
{out[0]=publications;out[1]=writeback_us;out[2]=frames;publications=writeback_us=frames=0;}
void ge_writeback_totals(unsigned out[3])
{out[0]=publications;out[1]=writeback_us;out[2]=frames;}

int __real_sceGeListUpdateStallAddr(int id,void *address);
void *__real_sceGuGetMemory(int size);
void *__wrap_sceGuGetMemory(int size)
{
    /* Allocation contains a jump, not a draw. Let the next draw/Finish
       publish it together with the vertices the caller is about to fill.
       Avoid a whole-cache writeback and a kernel call for every tiny
       clipped triangle allocation. Nested CALL lists keep SDK behavior. */
    allocating++;
    void *p=__real_sceGuGetMemory(size);
    allocating--;
    return p;
}
int __wrap_sceGeListUpdateStallAddr(int id,void *address)
{
    if(active && allocating)return 0;
    if(active) {
        unsigned started=JGR_PROFILE_CLOCK();
        /* GetMemory publishes its jump BEFORE the caller fills vertices.
           A monotonically advancing range would miss those later writes.
           Flush the fixed-size CPU data cache at EVERY publication instead:
           commands, older allocations, and their final vertices all become
           visible before GE can read the newly published draw command.
           Clean lines are retained; no per-draw cache invalidation. */
        sceKernelDcacheWritebackAll();
        writeback_us+=JGR_PROFILE_CLOCK()-started;publications++;
    }
    return __real_sceGeListUpdateStallAddr(id,address);
}
