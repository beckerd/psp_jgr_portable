#ifndef JGR_PORTABLE_ROSTER_CONVERT_H
#define JGR_PORTABLE_ROSTER_CONVERT_H
#include <stdio.h>
/* Inputs are already verified generated chars_compact.jgc and playerlight.bin.
 * Repacking preserves every model, LOD, animation and vertex lighting map. */
int jgr_roster_convert(FILE *characters,FILE *lighting,
                       const char *temporary_directory,FILE *output);
#ifdef JGR_ROSTER_TESTING
void jgr_roster_test_source_disabled(unsigned disabled);
void jgr_roster_test_source_stats(unsigned *loaded,unsigned *reclaimed);
#endif
#endif
