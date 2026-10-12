#ifndef JGR_PORTABLE_ASSETS_H
#define JGR_PORTABLE_ASSETS_H

/* Only portable builds interpose file operations. Ordinary development builds
   retain their existing paths, including test fixture directories. */
#ifndef JGR_PORTABLE_ASSETS
#define JGR_PORTABLE_ASSETS 0
#endif
#ifndef JGR_DEVICE_SETUP
#define JGR_DEVICE_SETUP 0
#endif
#ifndef JGR_SETUP_ME
#define JGR_SETUP_ME 0
#endif
#ifndef JGR_BUNDLED_RUNTIME
#define JGR_BUNDLED_RUNTIME JGR_DEVICE_SETUP
#endif

#define JGR_PORTABLE_PATH_MAX 768
#define JGR_PORTABLE_NAME_MAX 192
enum {
    PORTABLE_ASSETS_OK=0,
    PORTABLE_ASSETS_MISSING_MANIFEST=-1,
    PORTABLE_ASSETS_INVALID_MANIFEST=-2,
    PORTABLE_ASSETS_VERSION=-3,
    PORTABLE_ASSETS_IDENTITY=-4,
    PORTABLE_ASSETS_MISSING_FILE=-5,
    PORTABLE_ASSETS_SIZE=-6,
    PORTABLE_ASSETS_CHECKSUM=-7,
    PORTABLE_ASSETS_IO=-8,
    PORTABLE_ASSETS_PATH=-9,
    PORTABLE_ASSETS_CANCELLED=-10
};
typedef struct {
    int status;
    unsigned files,full_verification,cache_written;
    char file[JGR_PORTABLE_NAME_MAX];
} PortableAssetsResult;
/* Startup only; captures the absolute application directory once. After it
   succeeds, file lookup is bounded string work with no extra disk access. */
int portable_assets_init(PortableAssetsResult *result,
                        void (*progress)(unsigned,unsigned,const char *));
int portable_assets_bootstrap(void);
/* A single pre-GU frame of the user-prepared original warning, without added
   text or delay. Releases pixels immediately; no verification/logging/disc IO. */
void portable_assets_show_startup(void);
/* Setup callback thread: elapsed-time display only, no file I/O. Disabled
   before freeing setup pixels or handing control to the game. */
void portable_assets_setup_tick(void);
/* Setup exit callbacks request cancellation; only the main thread may close
 * workers and replace the executable after their physical fence completes. */
void portable_assets_setup_request_exit(void);
int portable_assets_setup_exiting(void);
/* Restore the original EBOOT directory after an executable handoff. */
int portable_assets_chdir_application(const char *root);
void portable_assets_data_error(void);
const char *portable_assets_error(int status);
int portable_assets_resolve(char out[JGR_PORTABLE_PATH_MAX],const char *name,
                            int writing);

#if JGR_PORTABLE_ASSETS
/* Include real declarations before function-like macros; `struct stat` is not
   rewritten. This header is force-included only for the portable target. */
#include <stdio.h>
#include <sys/stat.h>
FILE *portable_assets_fopen(const char *name,const char *mode);
int portable_assets_stat(const char *name,struct stat *value);
int portable_assets_remove(const char *name);
int portable_assets_rename(const char *from,const char *to);
#ifdef __PSP__
/* Do not inject SDK's u16/u32 typedefs into otherwise portable decoders.
   SceUID and SceMode are int in PSPSDK; the stat tag is public. Object-like
   macros below also leave later SDK function declarations well-formed. */
struct SceIoStat;
int portable_assets_sce_open(const char *name,int flags,int mode);
int portable_assets_sce_stat(const char *name,struct SceIoStat *value);
int portable_assets_sce_remove(const char *name);
int portable_assets_sce_rename(const char *from,const char *to);
#endif
#ifndef JGR_PORTABLE_ASSETS_IMPLEMENTATION
#define fopen(name,mode) portable_assets_fopen((name),(mode))
#define stat(name,value) portable_assets_stat((name),(value))
#define remove(name) portable_assets_remove((name))
#define rename(from,to) portable_assets_rename((from),(to))
#ifdef __PSP__
#define sceIoOpen portable_assets_sce_open
#define sceIoGetstat portable_assets_sce_stat
#define sceIoRemove portable_assets_sce_remove
#define sceIoRename portable_assets_sce_rename
#endif
#endif
#endif
#endif
