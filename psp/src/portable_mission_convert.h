#ifndef JGR_PORTABLE_MISSION_CONVERT_H
#define JGR_PORTABLE_MISSION_CONVERT_H
#include <stdio.h>
#include "portable_disc.h"
enum { JGR_MISSION_CTRL=10,JGR_MISSION_GRAPH,JGR_MISSION_GROUPS,
       JGR_MISSION_OBJECTS,JGR_MISSION_PEDS,JGR_MISSION_POLICE,
       JGR_MISSION_RIVALS,JGR_MISSION_WAVES,JGR_MISSION_CANS };
/* Writes a seekable temporary file. Caller verifies its expected identity
   before publishing. sectors is used only by OBJECTS (1..128). */
int jgr_mission_convert(JgrDisc *disc,const JgrDiscFile *source,
                        unsigned kind,unsigned sectors,FILE *output);
#endif
