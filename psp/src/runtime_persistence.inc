/* Called only during startup, pause, loading, or orderly exit. */
static int runtime_log_rotation;
static int runtime_log_open_session(void)
{
    const int missing=(int)0x80010002u; /* SCE_ERROR_ERRNO_ENOENT */
    int fd=sceIoOpen("jgr_log.txt",PSP_O_RDONLY,0777);
    runtime_log_rotation=0;
    if(fd>=0) {
        int result=sceIoClose(fd);
        if(result>=0) {
            result=sceIoRemove("jgr_log_prev.txt");
            if(result==missing)result=0;
        }
        if(result>=0)result=sceIoRename("jgr_log.txt","jgr_log_prev.txt");
        runtime_log_rotation=result;
        /* Failed rotation must never truncate the only surviving session. */
        if(result<0)return sceIoOpen("jgr_log.txt",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
    } else if(fd!=missing) {
        runtime_log_rotation=fd;
        return sceIoOpen("jgr_log.txt",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
    }
    return sceIoOpen("jgr_log.txt",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
}

static int runtime_log_safe_checkpoint(const char *reason)
{
    int deferred=runtime_log_deferred;
    unsigned phase=hang_mark(HANG_LOG_IO);
    runtime_log_checkpoint(reason);
    int result=logfd<0 ? logfd:runtime_log_queue.used ? -1:0;
    if(logfd>=0) {
        /* Repeated safe snapshots are intentional; history remains in RAM
           for a subsequent fault. Write directly, never through the logger. */
        if(!runtime_log_queue.used)result=release_report_write(logfd,sceIoWrite);
        int closed=sceIoClose(logfd);
        if(!result && closed<0)result=closed;
    }
    logfd=sceIoOpen("jgr_log.txt",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
    if(!result && logfd<0)result=logfd;
    /* Restore even after a short write, close failure, or missing device. */
    runtime_log_deferred=deferred;
    hang_mark(phase);
    if(result<0)release_report_event(RELEASE_EVENT_SAMPLE_ERROR,1,(unsigned)result,0);
    return result;
}
