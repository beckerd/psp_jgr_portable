#ifndef JGR_PORTABLE_BUBBLE_H
#define JGR_PORTABLE_BUBBLE_H
#include "portable_disc.h"
#include <stdio.h>
/* Source PLAYER_EFFECT geometry to the native speech-bubble buffer format. */
int jgr_convert_bubble(JgrDisc *,const JgrDiscFile *,FILE *);
#endif
