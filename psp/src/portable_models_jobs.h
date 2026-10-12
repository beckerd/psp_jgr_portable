#ifndef JGR_PORTABLE_MODELS_JOBS_H
#define JGR_PORTABLE_MODELS_JOBS_H
#include "portable_models.h"
/* Cumulative setup wall times, measured only at model/texture call boundaries.
 * Enabled by JGR_DEVICE_SETUP; -DJGR_MODELS_STATS=0 removes all clocks/counters
 * and leaves a zero-valued API, for overhead comparisons and other builds.
 * finish_us includes texture_us. texture_us includes prepare_us, wait_us and
 * cpu_texture_us; these overlapping counters must not be added together.
 * prepare_us includes ME-path source read/decode/resize. cpu_texture_us includes
 * the complete CPU fallback (read/decode/resize/quantize/write), when used. */
typedef struct {
    uint64_t total_us,geometry_us,finish_us,pack_us,texture_us,prepare_us,wait_us,cpu_texture_us;
    uint64_t scratch_peak;
    unsigned jobs,rows,textures,cache_hits,spills,opens;
} JgrModelsStats;
void jgr_models_stats(JgrModelsStats *);
/* Optional setup-session texture reuse. Begin binds one open, immutable disc;
 * clear releases cached bytes without ending that scope, and end disables it.
 * End before closing the disc or entering the replay phase. Allocation failure
 * simply uses the ordinary converter. At most 512 KiB of pixels are retained
 * in RAM. An optional content cache in the validated temporary directory
 * avoids rebuilding identical textures at different disc locations. Its
 * bounded index and file are private to this invocation; clear drops RAM
 * pixels, while end closes and removes the content cache. */
#define JGR_MODEL_TEXTURE_CACHE_FILE "model-textures.tmp"
#define JGR_MODEL_TEXTURE_CACHE_DISK_BYTES (32u*1024u*1024u)
void jgr_models_cache_begin(JgrDisc *);
void jgr_models_cache_clear(void);
void jgr_models_cache_end(void);
/* Recipe validation performs no filesystem writes and restores plan position. */
int jgr_model_recipe_validate(FILE *plan,uint32_t offset,uint32_t bytes);
/* Bounded private work streams are created inside the caller's validated
 * cache directory, then removed on success/failure. Output is caller-owned. */
int jgr_models_convert(JgrDisc *,FILE *plan,uint32_t offset,uint32_t bytes,
                       const char *temporary_directory,FILE *output);
/* A shared cast may emit its matching PLT1 normal/material sidecar during the
 * same geometry pass. Both streams remain caller-owned; only flags 16384 are
 * accepted when lighting_output is non-NULL. */
int jgr_models_convert_with_lighting(JgrDisc *,FILE *plan,uint32_t offset,uint32_t bytes,
                       const char *temporary_directory,FILE *output,FILE *lighting_output);
/* Last conversion failure; empty after a successful conversion. */
const char *jgr_models_error(void);
#endif
