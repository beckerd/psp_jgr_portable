#ifndef JGR_GE_WRITEBACK_H
#define JGR_GE_WRITEBACK_H
/* One live DIRECT list; begin follows sceGuStart, end follows sceGuFinish.
   The existing GE fence owns both its command and transient vertex memory. */
void ge_writeback_begin(void *arena);
void ge_writeback_end(void);
void ge_writeback_profile(unsigned out[3]); /* publications, writeback us, active frames */
void ge_writeback_totals(unsigned out[3]);
#endif
