#ifndef JGR_PORTABLE_STAGE_PROPS_H
#define JGR_PORTABLE_STAGE_PROPS_H
#include "portable_disc.h"
#include <stdio.h>
/* First-launch postprocessing of native near/far city geometry. Reads original
 * placement/mesh data from disc; inputs are generated JGS/JRS, never recipes
 * containing vertex payloads. All streams must be seekable and distinct. */
int jgr_stage_props(JgrDisc *,unsigned stage,const char *temporary_directory,
                    FILE *input_geometry,FILE *input_ownership,
                    FILE *output_geometry,FILE *output_ownership);
/* Complete native city plus prop layer. Outputs must be opened w+b. */
int jgr_stage_city(JgrDisc *,unsigned stage,const char *temporary_directory,
                    FILE *output_geometry,FILE *output_ownership);
#endif
