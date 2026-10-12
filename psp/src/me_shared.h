#ifndef JGR_ME_SHARED_H
#define JGR_ME_SHARED_H
/* Shared Media Engine mailbox with an uncached-only, isolated
   control line. The kernel helper stops polling user memory before exit. */
typedef struct {
    volatile unsigned start,done,func,param;
    volatile int result;
    volatile unsigned init,alive,spins;
    volatile int boot_status;
    volatile unsigned stop,stopped;
    volatile unsigned pause_req,pause_done,reboot_req,reboot_done,sleeping;
    /* Separate cache line/mailbox: render preparation never overwrites an
       audio request or completion. One bounded chunk, then check audio. */
    volatile unsigned bg_state,bg_func,bg_param,bg_chunks;
    volatile unsigned bg_cycles,bg_max_cycles;
} __attribute__((aligned(64))) JgrMeShared;
#define JGR_ME_BG_IDLE 0u
#define JGR_ME_BG_PENDING 1u
#define JGR_ME_BG_DONE 2u
#define JGR_ME_BG_CANCELLED 3u
#define JGR_ME_MAGIC0 0x4a47524du
#define JGR_ME_MAGIC1 0x45534852u
#define JGR_ME_MAGIC2 0x554e4341u
#define JGR_ME_MAGIC3 0x43484531u
#define JGR_ME_PING 0x536e6421u
#ifdef JGR_ME_HOST_TEST
#define JGR_ME_SYNC() __asm__ volatile("" : : : "memory")
#else
#define JGR_ME_SYNC() __asm__ volatile("sync" : : : "memory")
#endif
#endif
