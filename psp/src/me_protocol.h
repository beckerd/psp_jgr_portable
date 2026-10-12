#ifndef JGR_ME_PROTOCOL_H
#define JGR_ME_PROTOCOL_H
#include "me_shared.h"
/* Kernel watchdog step, separate from physical reset/bootstrap so the exact
   ownership and power handshakes can be fault-injected in host tests. */
static inline int jgr_me_kernel_step(volatile JgrMeShared **owner,
        void (*reset)(void),void (*boot)(volatile JgrMeShared *))
{
    volatile JgrMeShared *m=*owner;
    if(!m)return 0;
    if(m->stop) {
        reset();JGR_ME_SYNC();m->bg_state=JGR_ME_BG_CANCELLED;*owner=0;m->stopped=1;return 0;
    }
    if(m->pause_req!=m->pause_done) {
        unsigned request=m->pause_req;
        reset();JGR_ME_SYNC();m->bg_state=JGR_ME_BG_CANCELLED;m->pause_done=request;
    }
    if(!m->stop && !m->sleeping && m->reboot_req!=m->reboot_done) {
        unsigned request=m->reboot_req;
        boot(m);JGR_ME_SYNC();m->reboot_done=request;
    }
    return 1;
}
/* Shared by the real worker and scheduler fault-injection tests. Audio
   wins at every chunk boundary. Invoke returns nonzero when a background
   batch is complete; no OS service or allocation is legal inside a chunk. */
static inline unsigned jgr_me_work_lane(const volatile JgrMeShared *m)
{
    if(m->stop || m->sleeping || m->pause_req!=m->pause_done)return 0;
    if(m->start)return 1;
    return m->bg_state==JGR_ME_BG_PENDING ? 2:0;
}
#endif
