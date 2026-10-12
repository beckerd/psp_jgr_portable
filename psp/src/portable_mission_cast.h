#ifndef JGR_PORTABLE_MISSION_CAST_H
#define JGR_PORTABLE_MISSION_CAST_H
#include "portable_disc.h"
#include <stdio.h>
/* Emit 60 u64 cast masks, one u64 valid mask and one u32 title model ID.
 * Inputs are the verified generated mission/set banks and title replay. */
int jgr_mission_cast(JgrDisc *,const char *verified_asset_directory,FILE *output);
int jgr_mission_cast_checked(JgrDisc *,FILE *(*verified_open)(void *,const char *),
                              void *user,FILE *output);
#endif
