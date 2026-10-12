#ifndef JGR_PORTABLE_TEXTURE_JOB_H
#define JGR_PORTABLE_TEXTURE_JOB_H
#include <stddef.h>
#include <stdint.h>
/* A single aligned allocation owns immutable prepared pixels, encoded output,
 * and a bounded private allocator. Only one worker may execute at a time.
 * The caller must fence transport completion before inspecting or destroying
 * a submitted job. No worker calls the OS, filesystem, or shared allocator. */
typedef struct JgrTextureJob JgrTextureJob;
size_t jgr_texture_job_bytes(unsigned width,unsigned height);
JgrTextureJob *jgr_texture_job_create(const unsigned char *prepared,
    unsigned width,unsigned height,unsigned flags);
void *jgr_texture_job_range(JgrTextureJob *,size_t *bytes);
int jgr_texture_job_worker(intptr_t argument);
int jgr_texture_job_result(const JgrTextureJob *,const unsigned char **output,unsigned *flags);
/* CPU retry/verification are legal only before submission or after its fence. */
int jgr_texture_job_retry(JgrTextureJob *);
int jgr_texture_job_verify(const JgrTextureJob *);
void jgr_texture_job_destroy(JgrTextureJob *);
#endif
