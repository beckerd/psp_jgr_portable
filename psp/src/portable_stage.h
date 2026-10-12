#ifndef JGR_PORTABLE_STAGE_H
#define JGR_PORTABLE_STAGE_H
#include "portable_disc.h"
#include <stdio.h>
enum {JGR_STAGE_PHASE_INPUT=1,JGR_STAGE_PHASE_GEOMETRY,JGR_STAGE_PHASE_TEXTURE,
      JGR_STAGE_PHASE_ASSEMBLY,JGR_STAGE_PHASE_POSTPROCESS};
enum {JGR_STAGE_JOB_OBJECTS,JGR_STAGE_JOB_BASE,JGR_STAGE_JOB_RENDER,
      JGR_STAGE_JOB_PROP_BANK,JGR_STAGE_JOB_LIGHT_BANK,JGR_STAGE_JOB_CITY,
      JGR_STAGE_JOB_PACK,JGR_STAGE_JOB_PROPS};
/* Jobs count outer calls; builds/counts describe intermediate conversion work,
 * not distinct published assets. Times are exclusive, including nested jobs.
 * A snapshot includes the currently running phase without changing its clock. */
typedef struct {
 uint64_t total_us,input_us,geometry_us,texture_us,assembly_us,postprocess_us,current_us;
 uint64_t objects,batches,vertices,triangles,textures,output_bytes;
 unsigned jobs,completed,failed,builds,active,stage,kind,phase;
} JgrStageStats;
void jgr_stage_stats(JgrStageStats *);
void jgr_stage_stats_reset(void);
enum {JGR_STAGE_FAILURE_FINISH=1,JGR_STAGE_FAILURE_OWNERSHIP,
      JGR_STAGE_FAILURE_KEY_COPY,JGR_STAGE_FAILURE_SECTION_CLOSE,
      JGR_STAGE_FAILURE_SCRATCH_END,JGR_STAGE_FAILURE_RAW_FLUSH,
      JGR_STAGE_FAILURE_KEY_FLUSH,JGR_STAGE_FAILURE_PACK,
      JGR_STAGE_FAILURE_OUTPUT_FLUSH,JGR_STAGE_FAILURE_OWNERSHIP_HASH,
      JGR_STAGE_FAILURE_KEY_WRITE,JGR_STAGE_FAILURE_KEY_UPDATE};
/* First failed city boundary in the current outer conversion. Capturing this
 * does no logging or timing; offsets are -1 when no live stream is available. */
typedef struct {
 unsigned stage,step,section;
 int error;
 int64_t source_offset,output_offset;
 int source_error,output_error;
 unsigned heap_free,kernel_free,kernel_max_free;
} JgrStageFailure;
void jgr_stage_failure(JgrStageFailure *);
/* Internal coarse phase scopes shared by city postprocessing helpers. */
typedef struct {unsigned active,stage,kind,phase;} JgrStageStatsScope;
void jgr_stage_stats_begin(JgrStageStatsScope *,unsigned stage,unsigned kind);
void jgr_stage_stats_phase(unsigned phase);
void jgr_stage_stats_end(const JgrStageStatsScope *,int result);
/* Native first-launch static geometry conversion. Source images and all
 * texture mappings are read from the supported disc, never compiled payloads. */
int jgr_stage_objects(JgrDisc *,unsigned stage,const char *cache_directory,FILE *);
int jgr_stage_surface(JgrDisc *,FILE *);
/* Near geometry intermediate; final city sidecars/postprocessing are separate. */
int jgr_stage_base(JgrDisc *,unsigned stage,const char *cache_directory,FILE *);
typedef struct {
 uint32_t root,texlist,mesh; /* mesh is used only when root==0 */
 double matrix[16];
 unsigned first_object,object_count,texture0; /* compact slot 0, or 0xffff */
} JgrStageProp;
/* Explicit original prop roots/texlists and caller-computed transforms.
 * The generated bank owns consecutive ranges returned per placement. */
int jgr_stage_prop_bank(JgrDisc *,unsigned stage,const char *temporary_directory,
                       JgrStageProp *,unsigned count,FILE *output);
/* Source-material, unlit variant used for the original grinding lamps. */
int jgr_stage_light_bank(JgrDisc *,unsigned stage,const char *temporary_directory,
                        JgrStageProp *,unsigned count,FILE *output);
/* Near/far construction before geometry compaction and prop instancing. */
int jgr_stage_render(JgrDisc *,unsigned stage,const char *temporary_directory,
                     FILE *geometry,FILE *ownership);
int jgr_stage_city_geometry(JgrDisc *,unsigned stage,const char *temporary_directory,
                            FILE *geometry,FILE *ownership);
/* Optional setup-session observer. Geometry is the completed packed city
 * before prop instancing; tunnel indices retain original source strip flags.
 * The borrowed stream/indices are valid only during the callback. Capture
 * failures must remain optional and may not alter the supplied stream. */
typedef void (*JgrStageCityCapture)(JgrDisc *,unsigned stage,FILE *geometry,
                                   const uint16_t *tunnel_indices,unsigned count,void *);
void jgr_stage_city_capture(JgrDisc *,JgrStageCityCapture,void *);
/* Optional setup-session observation of a completed indexed city texture.
 * Sources identify immutable disc ranges; pixels/stream are borrowed only
 * during the callback. The observer must not change or retain either. */
typedef struct {
 JgrDiscFile file;uint32_t gbix;
 unsigned source_width,source_height,width,height;
 uint32_t clut,pixels;
} JgrStageTextureSource;
typedef void (*JgrStageTextureCapture)(JgrDisc *,const JgrStageTextureSource *,
                                       const unsigned char *rgba,FILE *encoded,void *);
void jgr_stage_texture_capture(JgrDisc *,JgrStageTextureCapture,void *);
int jgr_stage_tunnel_materials(JgrDisc *,const char *temporary_directory,
                                FILE *verified_geometry,FILE *raw_indices);
#endif
