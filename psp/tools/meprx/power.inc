/* A custom ME cannot answer the firmware SceMeRpc handler's suspend RPC.
   In particular, event 0x4005 sends command 399 and waits indefinitely for
   the Sony ME firmware. Claim its events BEFORE replacing that firmware.
   See uofw/src/kd/me_wrapper/me_wrapper.c, eventHandler/sub_16389.

   Our handler never waits, allocates, reads files, or takes a thread lock.
   It physically fences the ME even when the watchdog cannot be scheduled.
   The user callback requests the reboot only after RESUME_COMPLETE. */
static PspSysEventHandler *firmware_power;
static int power_registered,core_replaced;
static volatile int power_blocked;
static int power_event(int event,char *name,void *param,int *result)
{
    (void)name;(void)param;(void)result;
    if(event==0x4005) {
        power_blocked=1;
        if(control) {
            control->sleeping=1;
            if(control->pause_req==control->pause_done)control->pause_req++;
        }
        sleep_core();JGR_ME_SYNC();
        if(control) {
            control->bg_state=JGR_ME_BG_CANCELLED;
            control->pause_done=control->pause_req;
        }
    } else if(event==0x10005) {
        /* Resume phase zero: do not bootstrap while the OS restores RAM
           and devices. No Sony RPC or ME firmware restore is needed. */
        sleep_core();JGR_ME_SYNC();
    } else if(event==0x100000)power_blocked=0;
    return 0;
}
static PspSysEventHandler power_handler={
    .size=sizeof(PspSysEventHandler),.name="JgrMePower",
    .type_mask=0x01ffff00,.handler=power_event
};
static int power_watch_step(void (*boot)(volatile JgrMeShared *))
{
    return power_blocked ? control!=0:
        jgr_me_kernel_step(&control,reset_core,boot);
}
static int is_firmware_power(const char *name)
{
    const char *wanted="SceMeRpc";
    if(!name)return 0;
    while(*wanted && *name==*wanted){name++;wanted++;}
    return !*name && !*wanted;
}
static int power_claim(void)
{
    for(PspSysEventHandler *p=sceKernelReferSysEventHandler();p;p=p->next)
        if(is_firmware_power(p->name)){firmware_power=p;break;}
    /* An unknown handler configuration must retain CPU audio; never boot
       a custom core without owning the firmware's power handoff. */
    if(!firmware_power)return -4;
    int error=sceKernelUnregisterSysEventHandler(firmware_power);
    if(error<0){firmware_power=0;return error;}
    error=sceKernelRegisterSysEventHandler(&power_handler);
    if(error<0) {
        sceKernelRegisterSysEventHandler(firmware_power);firmware_power=0;
        return error;
    }
    power_registered=1;return 0;
}
static int power_release(void)
{
    if(power_registered) {
        int error=sceKernelUnregisterSysEventHandler(&power_handler);
        if(error<0)return error; /* do not unload a registered code pointer */
        power_registered=0;
    }
    /* Once replaced, Sony's ME code/state no longer exists. Re-registering
       SceMeRpc against a reset core would recreate the sleep deadlock even
       after CPU fallback. Leave it detached for the rest of this title;
       ExitGame/loadexec reloads the firmware modules for the XMB. */
    if(firmware_power && !core_replaced) {
        int error=sceKernelRegisterSysEventHandler(firmware_power);
        if(error<0)return error;
    }
    firmware_power=0;return 0;
}
