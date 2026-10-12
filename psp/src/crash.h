#ifndef JGR_CRASH_H
#define JGR_CRASH_H
/* Zero means installed on hardware; positive means deliberately skipped
   (emulator); negative means NOT installed. Always log this result. */
int crash_init(void);
int crash_shutdown(void);
const char *crash_build_id(void);
void crash_memory_sample(unsigned frame,int stage,int state,unsigned heap,
                         unsigned system,unsigned largest);
#endif
