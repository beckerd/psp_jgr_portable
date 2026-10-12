#ifndef JGR_PORTABLE_GRAFFITI_CONVERT_H
#define JGR_PORTABLE_GRAFFITI_CONVERT_H
#include "portable_disc.h"
#include <stdio.h>
int jgr_graffiti_convert(JgrDisc *,const JgrDiscFile *,unsigned operation,
                         unsigned selector,FILE *);
#endif
