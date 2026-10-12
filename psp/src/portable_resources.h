#ifndef JGR_PORTABLE_RESOURCES_H
#define JGR_PORTABLE_RESOURCES_H
#include "portable_disc.h"
#include <stdio.h>
enum { JGR_RESOURCE_FONT=40,JGR_RESOURCE_RESULT_CAMERA,JGR_RESOURCE_SPRAY_CAMERA,
       JGR_RESOURCE_SPOTS,JGR_RESOURCE_VOICES,JGR_RESOURCE_PRACTICE_LIST,JGR_RESOURCE_STRINGS,
       JGR_RESOURCE_GRIND_MAP };
int jgr_resource_convert(JgrDisc *,const JgrDiscFile *,unsigned operation,
                         const unsigned arguments[4],FILE *);
#endif
