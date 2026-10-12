#ifndef JGR_ME_H
#define JGR_ME_H
typedef struct {
    int available,load_error,boot_status,ping_error;
    unsigned ping_us,jobs,timeouts,wait_us,max_wait_us,submit_us,polls;
    unsigned suspends,resumes,resume_errors;
} MeProfile;
void me_init(void);
void me_boot_log(const char *event,int a,int b); /* startup only, never worker/callback */
void me_boot_log_flush(void); /* main-thread startup/failure safe point only */
int me_available(void);
int me_begin(int (*function)(int),int arg);
int me_wait(unsigned timeout_us);
int me_result(void);
/* Main/render thread only. Inputs remain immutable until wait returns.
   Wait: 1 completed, 0 reset-fenced cancellation, -1 still owned by ME.
   Counters are raw ME Count ticks, NOT MHz-derived utilization. */
int me_background_begin(int (*chunk)(int),int arg);
int me_background_wait(unsigned *chunks,unsigned *ticks,unsigned *max_ticks);
/* Stops/reset-fences the core. No worker-owned data may be reused before
   this returns true; a failed stop leaves that storage quarantined. */
int me_stop(void);
void me_note_suspend(void);
void me_note_resume(void);
void me_note_exit(void);
int me_resume_pending(void);
int me_service_resume(void);
int me_fence(void);
/* Reset a failed job and re-ping while retaining the kernel watchdog.
   Failure still requires me_fence before any borrowed memory is reused. */
int me_recover(void);
int me_shutdown(void);
void me_profile_read(MeProfile *out);
#endif
