#ifndef JGR_PORTABLE_PEOPLE_CONVERT_H
#define JGR_PORTABLE_PEOPLE_CONVERT_H
#include "portable_disc.h"
#include <stdio.h>
/* Builds authored signed strips, with every corner matched to the verified
 * resident JGC geometry. Source positions, UVs and materials stay disc reads. */
int jgr_people_native(JgrDisc *,unsigned city,FILE *verified_compact_bank,FILE *);
#endif
