/* Browser entry for the shared, checksum-verified asset converter.
 * The caller mounts selected files read-only and a bounded output filesystem
 * before calling this function. Original data is never embedded or uploaded. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#include "portable_bundle.h"
#include "portable_install.h"
#include "portable_startup_warning.h"
#ifndef JGR_PORTABLE_IDENTITY_HEADER
#define JGR_PORTABLE_IDENTITY_HEADER "portable_asset_identity.h"
#endif
#include JGR_PORTABLE_IDENTITY_HEADER
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void,jgr_browser_progress,(const char *phase,unsigned current,unsigned total,
                               double completed,double required),{
    if (Module.prepareProgress) Module.prepareProgress({
        phase: UTF8ToString(phase), current: current, total: total,
        completedBytes: completed, requiredBytes: required
    });
});
EM_JS(int,jgr_browser_cancelled,(void),{
    return Module.prepareCancelled && Module.prepareCancelled() ? 1 : 0;
});
EM_JS(double,jgr_browser_available_bytes,(void),{
    if (Module.prepareAvailableBytes == null || Module.prepareAvailableBytes === -1) return -1;
    return Number.isSafeInteger(Module.prepareAvailableBytes) &&
           Module.prepareAvailableBytes >= 0 ? Module.prepareAvailableBytes : -2;
});
#define BROWSER_EXPORT EMSCRIPTEN_KEEPALIVE
#else
/* The entry's transaction tests exercise the same C control flow on the host. */
extern void jgr_browser_progress(const char *,unsigned,unsigned,double,double);
extern int jgr_browser_cancelled(void);
#define BROWSER_EXPORT
#endif

static char browser_error[512];
static int browser_active,cancelled;
static unsigned completed_files;

static int fail(int code,const char *message,const char *detail)
{
    snprintf(browser_error,sizeof browser_error,"%s%s%s",message,
             detail && *detail?": ":"",detail && *detail?detail:"");
    return code;
}
static int check_cancel(void)
{
    if(jgr_browser_cancelled())cancelled=1;
    return cancelled;
}
static void bundle_progress(unsigned current,unsigned total,const char *name,void *user)
{
    (void)name;(void)user;
    jgr_browser_progress("bundle",current,total,0,0);(void)check_cancel();
}
static int install_progress(const JgrInstallResult *result,void *user)
{
    static const char *const phases[]={"plan","cleanup","scan","space","disc",
                                     "convert","verify","publish","verify"};
    (void)user;
    const char *phase=result->phase<sizeof phases/sizeof *phases?phases[result->phase]:"convert";
    jgr_browser_progress(phase,result->phase_current,result->phase_total,
                         (double)result->completed_bytes,(double)result->required_bytes);
    return !check_cancel();
}
static void verify_progress(unsigned current,unsigned total,const char *name)
{
    (void)name;jgr_browser_progress("verify",current,total,0,0);
    (void)check_cancel();
}
static uint32_t word(const unsigned char *p)
{return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int recipe_matches(const char *path)
{
    unsigned char header[40];FILE *file=fopen(path,"rb");if(!file)return 0;
    int valid=fread(header,1,sizeof header,file)==sizeof header &&
        !memcmp(header,"JGRINS1\0",8) && word(header+8)==4 && word(header+12)==440 &&
        word(header+16)==JGR_ASSET_FILE_COUNT && word(header+28)==JGR_ASSET_MANIFEST_CRC32 &&
        word(header+32)==JGR_ASSET_LAYOUT_VERSION;
    if(fclose(file))valid=0;
    return valid;
}
static int invalidate_receipt(void)
{
    if(remove("data/cache/assets.checked") && errno!=ENOENT)return -1;
    if(remove("data/cache/assets.checked.tmp") && errno!=ENOENT)return -1;
    return 0;
}
static int prepare_bundle_recipe(const char *root,char recipe[PORTABLE_BUNDLE_PATH_MAX])
{
    PortableBundleResult bundle;
    if(portable_bundle_prepare_progress(root,&bundle,bundle_progress,NULL)<0)
        return fail(-101,"Could not read the application bundle",portable_bundle_error(bundle.status));
    if(portable_bundle_path(PORTABLE_BUNDLE_RECIPE,recipe,PORTABLE_BUNDLE_PATH_MAX)<0 || !recipe_matches(recipe))
        return fail(-102,"EBOOT.PBP does not match this asset preparer",
                    "Use the EBOOT and Prepare Assets files from the same release");
    return 0;
}
static int prepare_startup_warning(const char *root,const char *gdi)
{
    if(check_cancel())return JGR_INSTALL_CANCELLED;
    int valid=portable_startup_warning_valid(root);
    if(check_cancel())return JGR_INSTALL_CANCELLED;
    if(valid)return 0; /* No source-disc open on reuse. */
    JgrDisc *disc=NULL;char detail[256]={0};
    int opened=jgr_disc_open(&disc,gdi,detail,sizeof detail);
    if(opened<0){
        if(disc)jgr_disc_close(disc);
        if(check_cancel())return JGR_INSTALL_CANCELLED;
        return fail(-107,"Could not prepare the startup screen",detail);
    }
    if(check_cancel()){jgr_disc_close(disc);return JGR_INSTALL_CANCELLED;}
    int result=portable_startup_warning_prepare(disc,root);
    jgr_disc_close(disc);
    if(check_cancel())return JGR_INSTALL_CANCELLED;
    return result ? fail(-107,"Could not prepare the startup screen",NULL):0;
}

BROWSER_EXPORT const char *jgr_browser_error(void){return browser_error;}
BROWSER_EXPORT unsigned jgr_browser_asset_count(void){return completed_files;}
/* A separately usable optional repair: validates this EBOOT/preparer pair,
 * but never starts core conversion or invalidates its completion receipt. */
BROWSER_EXPORT int jgr_browser_prepare_startup_warning(const char *root,const char *gdi)
{
    if(browser_active)return fail(-100,"Asset preparation is already running",NULL);
    browser_error[0]=0;cancelled=0;
    if(!root || root[0]!='/' || !gdi || gdi[0]!='/')
        return fail(-100,"Select a game folder and a disc image before preparing assets",NULL);
    char previous[JGR_PORTABLE_PATH_MAX],recipe[PORTABLE_BUNDLE_PATH_MAX];
    if(!getcwd(previous,sizeof previous) || chdir(root))
        return fail(-100,"Cannot open the selected game folder",NULL);
    browser_active=1;int status;
    if(check_cancel())status=JGR_INSTALL_CANCELLED;
    else {
        status=prepare_bundle_recipe(root,recipe);
        if(!status)status=prepare_startup_warning(root,gdi);
    }
    if(status==JGR_INSTALL_CANCELLED)fail(status,"Asset preparation cancelled",NULL);
    if(chdir(previous) && !status)status=fail(-106,"Could not finish the preparation session",NULL);
    browser_active=0;return status;
}
BROWSER_EXPORT int jgr_browser_prepare(const char *root,const char *gdi)
{
    if(browser_active)return fail(-100,"Asset preparation is already running",NULL);
    browser_error[0]=0;cancelled=0;completed_files=0;
    if(!root || root[0]!='/' || !gdi || gdi[0]!='/')
        return fail(-100,"Select a game folder and a disc image before preparing assets",NULL);
    char previous[JGR_PORTABLE_PATH_MAX],recipe[PORTABLE_BUNDLE_PATH_MAX];
    if(!getcwd(previous,sizeof previous) || chdir(root))
        return fail(-100,"Cannot open the selected game folder",NULL);
    browser_active=1;int status=-100,invalidated=0;
    if(check_cancel()){status=JGR_INSTALL_CANCELLED;goto done;}
    status=prepare_bundle_recipe(root,recipe);if(status)goto done;
    if(check_cancel()){status=JGR_INSTALL_CANCELLED;goto done;}
    if(invalidate_receipt()){
        status=fail(-103,"Could not clear the previous installation receipt",NULL);goto done;
    }
    invalidated=1;
    JgrInstallResult installation;
    status=jgr_install_run(root,recipe,gdi,0,install_progress,NULL,&installation);
    if(status){
        if(status!=JGR_INSTALL_CANCELLED){
            char detail[416];
            snprintf(detail,sizeof detail,"%s%s%s%s",installation.detail,
                     *installation.file?" (":"",installation.file,*installation.file?")":"");
            fail(status,jgr_install_error(status),detail);
        }
        goto done;
    }
    if(check_cancel()){status=JGR_INSTALL_CANCELLED;goto done;}
    /* Rehash on the computer before publishing the PSP completion receipt.
     * A successful converter alone is not permission to emit that marker. */
    PortableAssetsResult assets;
    status=portable_assets_init(&assets,verify_progress);
    if(status){fail(-104,"Prepared assets did not pass verification",portable_assets_error(status));status=-104;goto done;}
    if(check_cancel()){status=JGR_INSTALL_CANCELLED;goto done;}
    if(!assets.cache_written || assets.files!=JGR_ASSET_FILE_COUNT){
        status=fail(-105,"Could not publish the completed installation receipt",NULL);goto done;
    }
    completed_files=assets.files;status=0;invalidated=0;
    /* Core verification is now committed. The installer may already have
     * prepared this optional image with its open disc. A resumed complete
     * installation needs only this supplemental step; neither its failure
     * nor cancellation may revoke the verified core receipt. */
    status=prepare_startup_warning(root,gdi);
    if(status!=JGR_INSTALL_CANCELLED){status=0;browser_error[0]=0;}
done:
    if(status && invalidated)(void)invalidate_receipt();
    if(status==JGR_INSTALL_CANCELLED)fail(status,"Asset preparation cancelled",NULL);
    if(chdir(previous) && !status)status=fail(-106,"Could not finish the preparation session",NULL);
    browser_active=0;
    if(!status)jgr_browser_progress("done",completed_files,completed_files,0,0);
    return status;
}
