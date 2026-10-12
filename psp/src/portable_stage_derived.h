#ifndef JGR_PORTABLE_STAGE_DERIVED_H
#define JGR_PORTABLE_STAGE_DERIVED_H
#include "portable_stage.h"
#include <stdio.h>
/* Setup-only sidecars. JGS/JRS are newly generated seekable asset streams;
 * authored lower mip pixels are read from the user's verified disc. */
int jgr_stage_mips(JgrDisc *,FILE *jgs,unsigned stage,FILE *output);
/* Optional bounded metadata cache, valid only for one immutable disc session.
 * The installer attaches/detaches the callback with stage_texture_capture.
 * No pixels or source payload survive the callback; misses use full matching. */
void jgr_stage_mips_cache_begin(JgrDisc *);
void jgr_stage_mips_cache_end(void);
void jgr_stage_mips_capture(JgrDisc *,const JgrStageTextureSource *,
                            const unsigned char *rgba,FILE *encoded,void *);
#ifdef JGR_STAGE_MIPS_TESTING
typedef struct {unsigned captured,sources,identity_hits,base_encodes,aliases,lower_candidates,eager_fallbacks;} JgrStageMipsStats;
void jgr_stage_mips_stats(JgrStageMipsStats *);
void jgr_stage_mips_stats_reset(void);
void jgr_stage_mips_test_limits(unsigned identities,unsigned aliases);
#endif
int jgr_stage_visibility(FILE *jgs,FILE *jrs,unsigned stage,FILE *output);
/* Rebuild small pedestrian occlusion metadata from generated visual geometry.
 * Exact output ABI sizes bound all caller buffers; no source geometry is kept. */
int jgr_stage_ped_proxy(FILE *city,FILE *objects,FILE *placements,
                       void *roofs,unsigned roof_bytes,void *bodies,unsigned body_bytes,
                       void *banks,unsigned bank_bytes);
/* Independent halves allow capturing city metadata before the object bank
 * exists. Each bank output is one 28-byte record from the combined format. */
int jgr_stage_ped_city_proxy(FILE *city,FILE *placements,void *roofs,unsigned roof_bytes,
                            void *bank,unsigned bank_bytes);
int jgr_stage_ped_object_proxy(FILE *objects,void *bodies,unsigned body_bytes,
                              void *bank,unsigned bank_bytes);
#endif
