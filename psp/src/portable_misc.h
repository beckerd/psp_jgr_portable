#ifndef JGR_PORTABLE_MISC_H
#define JGR_PORTABLE_MISC_H
#include "portable_disc.h"
#include <stdio.h>
int jgr_misc_known(const char *name);
int jgr_misc_convert(JgrDisc *,const char *name,FILE *output);
#endif
