#ifndef JGR_PORTABLE_WORLD_CONVERT_H
#define JGR_PORTABLE_WORLD_CONVERT_H
#include "portable_disc.h"
#include <stdio.h>
/* Setup-only native world serializers. Output must be seekable. Conversion
 * failures leave an unpublished partial file for the installer to discard. */
int jgr_world_convert_known(const char *name);
int jgr_world_convert(JgrDisc *disc,const char *name,FILE *output);
/* Private setup scratch for native demonstrations/credits. Follows original
 * solid-mesh registration for Shibuya/Benten; it does not replace a catalog
 * collision bank or publish an installed asset. Only stages 1 and 3 apply. */
int jgr_world_native_collision(JgrDisc *disc,unsigned stage,FILE *output);
#endif
