#ifndef JGR_PORTABLE_HARDWARE_H
#define JGR_PORTABLE_HARDWARE_H
#include "portable_disc.h"
#include <stdio.h>
/* Generic SH-4 hardware data, separately attributed GPL-2.0-or-later input.
 * The returned immutable table has 32768 IEEE binary32 bit patterns. */
const uint32_t *jgr_hardware_fsca(void);
int jgr_hardware_known(const char *name);
int jgr_hardware_convert(JgrDisc *disc,const char *name,FILE *out);
#endif
