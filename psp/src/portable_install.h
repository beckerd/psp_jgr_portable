#ifndef JGR_PORTABLE_INSTALL_H
#define JGR_PORTABLE_INSTALL_H
#include <stdint.h>
#include <stddef.h>

enum {
    JGR_INSTALL_OK=0, JGR_INSTALL_IO=-1, JGR_INSTALL_PLAN=-2,
    JGR_INSTALL_DISC=-3, JGR_INSTALL_CHECKSUM=-4, JGR_INSTALL_SPACE=-5,
    JGR_INSTALL_INCOMPLETE=-6, JGR_INSTALL_CANCELLED=-7
};
enum { JGR_INSTALL_COPY=1, JGR_INSTALL_TEX2D=2, JGR_INSTALL_LIST2D=3 };
enum {
    JGR_INSTALL_PHASE_PLAN=0, JGR_INSTALL_PHASE_CLEANUP,
    JGR_INSTALL_PHASE_SCAN, JGR_INSTALL_PHASE_SPACE, JGR_INSTALL_PHASE_DISC,
    JGR_INSTALL_PHASE_CONVERT, JGR_INSTALL_PHASE_VERIFY,
    JGR_INSTALL_PHASE_PUBLISH, JGR_INSTALL_PHASE_DONE
};
/* Setup-only cumulative wall times. Read includes raw-sector validation and
 * unpacking; write includes the final flush/close. No timing runs in gameplay. */
typedef struct {
    uint64_t bytes,read_us,write_us,hash_us,callback_us,publish_us;
    unsigned files,chunks,bulk_chunks,max_chunk;
} JgrInstallCopyStats;
typedef struct {
    uint64_t drain_us;
    unsigned queued,published,overlapped_jobs;
} JgrInstallPipelineStats;
/* Converter wall time includes its internal phases; do not add these totals
 * to the stage/replay counters. Flush, verification and publication are the
 * separate installer work after conversion. Setup-only, file boundaries. */
typedef struct {
    uint64_t convert_us,flush_us,verify_us,publish_us,bytes;
    unsigned started,completed,failed;
} JgrInstallWorkGroup;
typedef struct {JgrInstallWorkGroup city,replay;} JgrInstallWorkStats;
void jgr_install_work_stats(JgrInstallWorkStats *);
typedef struct {
    int status;
    unsigned total,available,written,reused,missing;
    uint64_t completed_bytes,required_bytes;
    char file[192],detail[192];
    unsigned phase,phase_current,phase_total;
    JgrInstallCopyStats copy;
    JgrInstallPipelineStats pipeline;
} JgrInstallResult;
/* Return nonzero to continue. Callback runs between bounded chunks. */
typedef int (*JgrInstallProgress)(const JgrInstallResult *,void *);
/* Invoked only after the installed-pack check fails. All generated outputs are
   verified before a manifest is published. A valid installed pack needs no
   disc access. allow_incomplete is for developer converter tests only; a
   partial run always returns INCOMPLETE and never publishes a manifest. */
int jgr_install_run(const char *root,const char *plan_path,const char *gdi_path,
                    int allow_incomplete,JgrInstallProgress progress,void *user,
                    JgrInstallResult *result);
const char *jgr_install_error(int status);
#endif
