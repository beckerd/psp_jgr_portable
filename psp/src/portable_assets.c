/* Cached asset verification and first-launch device-setup orchestration. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#include "portable_crc32.h"
/* The force-included header precedes the implementation guard above. */
#undef fopen
#undef stat
#undef remove
#undef rename
#undef sceIoOpen
#undef sceIoGetstat
#undef sceIoRemove
#undef sceIoRename

#if JGR_PORTABLE_ASSETS
#ifndef JGR_PORTABLE_IDENTITY_HEADER
#define JGR_PORTABLE_IDENTITY_HEADER "portable_asset_identity.h"
#endif
#include JGR_PORTABLE_IDENTITY_HEADER
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#if JGR_BUNDLED_RUNTIME
#include "portable_bundle.h"
#endif
#if JGR_DEVICE_SETUP
#include "portable_install.h"
#include "portable_models_jobs.h"
#include "portable_stage.h"
#include "portable_replay_session.h"
#endif
#if JGR_DEVICE_SETUP && JGR_SETUP_ME
#include "portable_setup_me.h"
#endif
#if JGR_DEVICE_SETUP && JGR_SETUP_CRASH
#include "crash.h"
#endif
#ifdef __PSP__
#include <pspiofilemgr.h>
#include <pspdebug.h>
#include <pspthreadman.h>
#include <psppower.h>
#endif

#define MANIFEST_NAME "data/assets/assets.manifest"
#define SEAL_NAME "data/cache/assets.checked"
#define SEAL_TEMP "data/cache/assets.checked.tmp"
#define MAX_MANIFEST_BYTES (4u*1024u*1024u)
#define MAX_ASSET_COUNT 20000u
static char app_root[512];
static unsigned app_root_length;
#ifdef __PSP__
static char save_root[JGR_PORTABLE_PATH_MAX];
static unsigned save_root_length;
static void save_root_init(void)
{
    save_root_length=0;
    if((!strncmp(app_root,"ms0:",4) || !strncmp(app_root,"ef0:",4)) &&
       (!app_root[4] || app_root[4]=='/')) {
        snprintf(save_root,sizeof save_root,"%.4s/PSP/SAVEDATA/JGR_PORTABLE",app_root);
        save_root_length=(unsigned)strlen(save_root);
    }
}
static int save_directory_prepare(void);
#endif
static int assets_ready;
static volatile int setup_exit_requested;
void portable_assets_setup_request_exit(void)
{
    setup_exit_requested=1;
#if JGR_DEVICE_SETUP && JGR_SETUP_ME
    jgr_setup_me_note_exit();
#endif
}
int portable_assets_setup_exiting(void){return setup_exit_requested;}
static void setup_power_tick(void)
{
#ifdef __PSP__
    static unsigned last;unsigned now=sceKernelGetSystemTimeLow();
    if(!last || now-last>=1000000u){scePowerTick(0);last=now;}
#endif
}
int portable_assets_chdir_application(const char *root)
{
    char path[JGR_PORTABLE_PATH_MAX];size_t n=root?strlen(root):0;
    if(!n || n+2>sizeof path || !(root[0]=='/' || !strncmp(root,"ms0:/",5) ||
       !strncmp(root,"ef0:/",5) || !strncmp(root,"umd0:/",6) || !strcmp(root,"ms0:") ||
       !strcmp(root,"ef0:") || !strcmp(root,"umd0:")))return PORTABLE_ASSETS_PATH;
    memcpy(path,root,n+1);if(path[n-1]==':'){path[n++]='/';path[n]=0;}
    return chdir(path)?PORTABLE_ASSETS_PATH:0;
}

/* Full file CRCs run only during installation validation; gameplay opens
   never hash or stat assets. */
static uint32_t crc_update(uint32_t crc,const void *data,size_t n)
{return jgr_crc32_add(crc,data,n);}
static int safe_relative(const char *name)
{
    if(!name || !*name || strlen(name)>=JGR_PORTABLE_NAME_MAX)return 0;
    const char *component=name;
    for(const char *p=name;;p++) {
        unsigned char c=(unsigned char)*p;
        if(!c || c=='/') {
            size_t n=(size_t)(p-component);
            if(!n || (n==1 && component[0]=='.') ||
               (n==2 && component[0]=='.' && component[1]=='.'))return 0;
            if(!c)return 1;
            component=p+1;
        } else if(!((c>='a' && c<='z') || (c>='A' && c<='Z') ||
                    (c>='0' && c<='9') || c=='_' || c=='-' || c=='.'))return 0;
    }
}
static int app_path(char out[JGR_PORTABLE_PATH_MAX],const char *relative)
{
    size_t n=strlen(relative);
    if(!app_root_length || app_root_length+1+n>=JGR_PORTABLE_PATH_MAX)return -1;
    memcpy(out,app_root,app_root_length);out[app_root_length]='/';
    memcpy(out+app_root_length+1,relative,n+1);return 0;
}
static int prefixed_path(char out[JGR_PORTABLE_PATH_MAX],const char *prefix,
                         const char *name)
{
    size_t p=strlen(prefix),n=strlen(name),start=app_root_length+1;
    if(!app_root_length || start+p+n>=JGR_PORTABLE_PATH_MAX)return -1;
    memcpy(out,app_root,app_root_length);out[app_root_length]='/';
    memcpy(out+start,prefix,p);memcpy(out+start+p,name,n+1);return 0;
}
enum { PATH_ASSET,PATH_LOG,PATH_CACHE,PATH_SAVE };
static int category(const char *name)
{
    if(!strncmp(name,"jgr_story.sav",13) &&
       (!name[13] || !strcmp(name+13,".tmp") || !strcmp(name+13,".bak")))return PATH_SAVE;
    if(!strncmp(name,"jgr_",4))return PATH_LOG;
    if(!strncmp(name,"shot_",5) || !strcmp(name,"shot.raw") || !strcmp(name,"shot2.raw") ||
       !strcmp(name,"shot3.raw") || !strcmp(name,"shot4.raw") || !strncmp(name,"route_save_fixture.sav",22) ||
       !strcmp(name,"level.txt") || (!strchr(name,'.') && !strchr(name,'/')))return PATH_CACHE;
    return PATH_ASSET;
}
int portable_assets_resolve(char out[JGR_PORTABLE_PATH_MAX],const char *name,
                            int writing)
{
    static const char *prefix[]={"data/assets/","data/log/","data/cache/"};
    if(!name || !app_root_length){errno=EINVAL;return -1;}
    int kind=-1;
#ifdef __PSP__
    if(save_root_length && !strncmp(name,save_root,save_root_length) && name[save_root_length]=='/') {
        kind=PATH_SAVE;name+=save_root_length+1;
    } else
#endif
    if(!strncmp(name,app_root,app_root_length) && name[app_root_length]=='/')
        name+=app_root_length+1;
    for(unsigned i=0;kind<0 && i<sizeof prefix/sizeof *prefix;i++) {
        size_t n=strlen(prefix[i]);
        if(!strncmp(name,prefix[i],n)){kind=(int)i;name+=n;break;}
    }
    if(!safe_relative(name)){errno=EINVAL;return -1;}
    if(kind<0)kind=category(name);
    if(kind==PATH_ASSET && (writing || !assets_ready)){errno=EACCES;return -1;}
    if(kind==PATH_SAVE) {
#ifdef __PSP__
        if(!save_root_length){errno=EINVAL;return -1;}
        size_t n=strlen(name);
        if(save_root_length+1+n>=JGR_PORTABLE_PATH_MAX){errno=EINVAL;return -1;}
        if(writing && save_directory_prepare())return -1;
        memcpy(out,save_root,save_root_length);out[save_root_length]='/';
        memcpy(out+save_root_length+1,name,n+1);return 0;
#else
        errno=EACCES;return -1;
#endif
    }
    return prefixed_path(out,prefix[kind],name);
}
FILE *portable_assets_fopen(const char *name,const char *mode)
{
    char path[JGR_PORTABLE_PATH_MAX];
    int writing=mode && (strchr(mode,'w') || strchr(mode,'a') || strchr(mode,'+'));
    if(!mode || portable_assets_resolve(path,name,writing)<0)return NULL;
    return fopen(path,mode);
}
int portable_assets_stat(const char *name,struct stat *value)
{char path[JGR_PORTABLE_PATH_MAX];return portable_assets_resolve(path,name,0)<0 ? -1:stat(path,value);}
int portable_assets_remove(const char *name)
{char path[JGR_PORTABLE_PATH_MAX];return portable_assets_resolve(path,name,1)<0 ? -1:remove(path);}
int portable_assets_rename(const char *from,const char *to)
{
    char a[JGR_PORTABLE_PATH_MAX],b[JGR_PORTABLE_PATH_MAX];
    return portable_assets_resolve(a,from,1)<0 || portable_assets_resolve(b,to,1)<0 ? -1:rename(a,b);
}
#ifdef __PSP__
SceUID portable_assets_sce_open(const char *name,int flags,SceMode mode)
{
    char path[JGR_PORTABLE_PATH_MAX];
    /* PSP_O_RDONLY=1, WRONLY=2, RDWR=3: bit 2 identifies either write mode. */
    int writing=flags&(PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC|PSP_O_APPEND);
    if(portable_assets_resolve(path,name,writing)<0)return (int)0x80010016u;
    return sceIoOpen(path,flags,mode);
}
int portable_assets_sce_stat(const char *name,SceIoStat *value)
{char path[JGR_PORTABLE_PATH_MAX];return portable_assets_resolve(path,name,0)<0 ? (int)0x80010016u:sceIoGetstat(path,value);}
int portable_assets_sce_remove(const char *name)
{char path[JGR_PORTABLE_PATH_MAX];return portable_assets_resolve(path,name,1)<0 ? (int)0x80010016u:sceIoRemove(path);}
int portable_assets_sce_rename(const char *from,const char *to)
{
    char a[JGR_PORTABLE_PATH_MAX],b[JGR_PORTABLE_PATH_MAX];
    return portable_assets_resolve(a,from,1)<0 || portable_assets_resolve(b,to,1)<0 ? (int)0x80010016u:sceIoRename(a,b);
}
#endif

static int failure(PortableAssetsResult *r,int code,const char *name)
{
    r->status=code;
    if(name){size_t n=strlen(name);if(n>=sizeof r->file)n=sizeof r->file-1;memcpy(r->file,name,n);r->file[n]=0;}
    return code;
}
static int read_line(FILE *f,char line[256])
{
    if(!fgets(line,256,f))return 0;
    size_t n=strlen(line);
    if(!n || line[n-1]!='\n' || memchr(line,'\r',n))return -1;
    line[n-1]=0;return 1;
}
static int decimal(const char **p,unsigned *out)
{
    const char *s=*p;unsigned n=0;
    if(*s<'0' || *s>'9')return 0;
    do{unsigned digit=(unsigned)(*s-'0');if(n>(0x7fffffffu-digit)/10)return 0;n=n*10+digit;s++;}
    while(*s>='0' && *s<='9');
    *p=s;*out=n;return 1;
}
static int manifest_header(FILE *f,PortableAssetsResult *r)
{
    char line[256];unsigned value;const char *p;
    if(read_line(f,line)!=1)return failure(r,PORTABLE_ASSETS_INVALID_MANIFEST,MANIFEST_NAME);
    if(strcmp(line,"JGR_ASSETS 1"))return failure(r,PORTABLE_ASSETS_VERSION,MANIFEST_NAME);
    if(read_line(f,line)!=1 || strncmp(line,"layout ",7))return failure(r,PORTABLE_ASSETS_INVALID_MANIFEST,MANIFEST_NAME);
    p=line+7;
    if(!decimal(&p,&value) || *p)return failure(r,PORTABLE_ASSETS_INVALID_MANIFEST,MANIFEST_NAME);
    if(value!=JGR_ASSET_LAYOUT_VERSION)return failure(r,PORTABLE_ASSETS_VERSION,MANIFEST_NAME);
    if(read_line(f,line)!=1 || strncmp(line,"count ",6))return failure(r,PORTABLE_ASSETS_INVALID_MANIFEST,MANIFEST_NAME);
    p=line+6;
    if(!decimal(&p,&value) || *p || !value || value>MAX_ASSET_COUNT)
        return failure(r,PORTABLE_ASSETS_INVALID_MANIFEST,MANIFEST_NAME);
    if(value!=JGR_ASSET_FILE_COUNT)return failure(r,PORTABLE_ASSETS_IDENTITY,MANIFEST_NAME);
    r->files=value;return 0;
}
static int manifest_entry(FILE *f,char name[JGR_PORTABLE_NAME_MAX],unsigned *bytes,
                          unsigned *crc)
{
    char line[256];const char *p=line;unsigned value=0;
    if(read_line(f,line)!=1 || !decimal(&p,bytes) || *p++!=' ')return -1;
    for(unsigned i=0;i<8;i++) {
        unsigned char c=(unsigned char)*p++;unsigned digit;
        if(c>='0' && c<='9')digit=c-'0';
        else if(c>='a' && c<='f')digit=c-'a'+10;
        else if(c>='A' && c<='F')digit=c-'A'+10;
        else return -1;
        value=value*16+digit;
    }
    if(*p++!=' ' || !safe_relative(p))return -1;
    /* Manifest entries name content, never runtime-owned trees or files. */
    if(!strncmp(p,"data/",5) || category(p)!=PATH_ASSET)return -1;
    memcpy(name,p,strlen(p)+1);*crc=value;return 0;
}
static int crc_file(FILE *f,uint32_t *out,unsigned *bytes,unsigned limit)
{
    unsigned char buffer[4096];size_t n;uint32_t crc=0xffffffffu;unsigned total=0;
    while((n=fread(buffer,1,sizeof buffer,f))!=0) {
        setup_power_tick();
        if(n>limit-total)return -1;
        total+=(unsigned)n;crc=crc_update(crc,buffer,n);
    }
    if(ferror(f))return -1;
    *out=crc^0xffffffffu;*bytes=total;return 0;
}
static int make_directory(const char *relative)
{
    char path[JGR_PORTABLE_PATH_MAX];struct stat info;
    if(app_path(path,relative)<0)return -1;
    if(mkdir(path,0777)==0)return 0;
    return errno==EEXIST && stat(path,&info)==0 && S_ISDIR(info.st_mode) ? 0:-1;
}
#ifdef __PSP__
static int save_directory_prepare(void)
{
    /* Prepare directories only for save writes. Save storage errors must not
       invalidate asset receipts or send a completed install into setup. */
    static const char *directory[]={"PSP","PSP/SAVEDATA","PSP/SAVEDATA/JGR_PORTABLE"};
    for(unsigned i=0;i<sizeof directory/sizeof *directory;i++) {
        char path[64];struct stat info;
        snprintf(path,sizeof path,"%.4s/%s",app_root,directory[i]);
        if(stat(path,&info)==0) {
            if(!S_ISDIR(info.st_mode)){errno=ENOTDIR;return -1;}
        } else if(errno!=ENOENT || mkdir(path,0777))return -1;
    }
    return 0;
}
#endif
static uint32_t metadata_add(uint32_t crc,const struct stat *info,const char *name)
{
    /* Serialize fields, not platform-specific stat padding. FAT timestamp
       granularity applies: this is a cache invalidator, not authentication. */
    uint64_t fields[2]={(uint64_t)info->st_size,(uint64_t)info->st_mtime};
    crc=crc_update(crc,name,strlen(name)+1);
    for(unsigned i=0;i<2;i++)for(unsigned j=0;j<8;j++) {
        unsigned char b=(unsigned char)(fields[i]>>(j*8));crc=crc_update(crc,&b,1);
    }
    return crc;
}
static int walk_manifest(FILE *f,PortableAssetsResult *r,int full,uint32_t *metadata,
                         void (*progress)(unsigned,unsigned,const char *))
{
    if(fseek(f,0,SEEK_SET) || manifest_header(f,r)<0)return r->status ? r->status:failure(r,PORTABLE_ASSETS_IO,MANIFEST_NAME);
    uint32_t stamp=0xffffffffu;
    for(unsigned i=0;i<r->files;i++) {
        char name[JGR_PORTABLE_NAME_MAX],path[JGR_PORTABLE_PATH_MAX];
        unsigned bytes,crc;struct stat info;
        if(manifest_entry(f,name,&bytes,&crc)<0)return failure(r,PORTABLE_ASSETS_INVALID_MANIFEST,MANIFEST_NAME);
        if(prefixed_path(path,"data/assets/",name)<0)return failure(r,PORTABLE_ASSETS_PATH,name);
        if(stat(path,&info)<0 || !S_ISREG(info.st_mode))return failure(r,PORTABLE_ASSETS_MISSING_FILE,name);
        if(info.st_size<0 || (uint64_t)info.st_size!=bytes)return failure(r,PORTABLE_ASSETS_SIZE,name);
        stamp=metadata_add(stamp,&info,name);
        if(full) {
            FILE *asset=fopen(path,"rb");uint32_t actual;unsigned total;
            if(!asset)return failure(r,PORTABLE_ASSETS_MISSING_FILE,name);
            if(setvbuf(asset,NULL,_IONBF,0)){fclose(asset);return failure(r,PORTABLE_ASSETS_IO,name);}
            int failed=crc_file(asset,&actual,&total,bytes);
            if(fclose(asset))failed=1;
            if(failed)return failure(r,PORTABLE_ASSETS_IO,name);
            if(total!=bytes)return failure(r,PORTABLE_ASSETS_SIZE,name);
            if(actual!=crc)return failure(r,PORTABLE_ASSETS_CHECKSUM,name);
        }
        if(progress && (i==0 || i+1==r->files || !(i&31)))progress(i+1,r->files,name);
    }
    if(fgetc(f)!=EOF || ferror(f))return failure(r,PORTABLE_ASSETS_INVALID_MANIFEST,MANIFEST_NAME);
    *metadata=stamp^0xffffffffu;return 0;
}
static int seal_matches(const uint32_t *metadata)
{
    char path[JGR_PORTABLE_PATH_MAX],expected[128],actual[128];
    if(app_path(path,SEAL_NAME)<0)return 0;
    int length=snprintf(expected,sizeof expected,"JGR_CHECKED 1\n%08x %u %u ",
                        JGR_ASSET_MANIFEST_CRC32,JGR_ASSET_LAYOUT_VERSION,JGR_ASSET_FILE_COUNT);
    FILE *f=fopen(path,"rb");if(!f)return 0;
    size_t n=fread(actual,1,sizeof actual,f);int bad=ferror(f);if(fclose(f))bad=1;
    if(bad || length<=0 || (size_t)length+9!=n || memcmp(actual,expected,(size_t)length) ||
       actual[n-1]!='\n')return 0;
    uint32_t stamp=0;
    for(unsigned i=0;i<8;i++){
        unsigned char c=(unsigned char)actual[length+i];unsigned digit;
        if(c>='0' && c<='9')digit=c-'0';
        else if(c>='a' && c<='f')digit=c-'a'+10;
        else return 0;
        stamp=stamp*16+digit;
    }
    /* The seal records a successful installation of this build's assets.
       Its metadata fingerprint is compared only when explicitly checking
       the installed files. */
    return !metadata || stamp==*metadata;
}
static int seal_invalidate(void)
{
    char path[JGR_PORTABLE_PATH_MAX];
    if(app_path(path,SEAL_NAME)<0 || (remove(path) && errno!=ENOENT))return -1;
    if(app_path(path,SEAL_TEMP)<0 || (remove(path) && errno!=ENOENT))return -1;
    return 0;
}
static int seal_write(uint32_t metadata)
{
    char path[JGR_PORTABLE_PATH_MAX],temporary[JGR_PORTABLE_PATH_MAX],body[128];
    if(app_path(path,SEAL_NAME)<0 || app_path(temporary,SEAL_TEMP)<0)return 0;
    int n=snprintf(body,sizeof body,"JGR_CHECKED 1\n%08x %u %u %08x\n",
                   JGR_ASSET_MANIFEST_CRC32,JGR_ASSET_LAYOUT_VERSION,JGR_ASSET_FILE_COUNT,(unsigned)metadata);
    FILE *f=fopen(temporary,"wb");if(!f)return 0;
    int bad=fwrite(body,1,(size_t)n,f)!=(size_t)n;
    if(fflush(f))bad=1;
    if(fclose(f))bad=1;
    /* PSP FAT rename does not replace files. Removing only a prior cache is
       safe; a power loss here simply requires verification next launch. */
    if(!bad && remove(path) && errno!=ENOENT)bad=1;
    if(!bad && rename(temporary,path))bad=1;
    if(bad)remove(temporary);
    return !bad;
}
static int assets_init(PortableAssetsResult *r,
                       void (*progress)(unsigned,unsigned,const char *),int verified_install)
{
    if(!r)return PORTABLE_ASSETS_IO;
    memset(r,0,sizeof *r);assets_ready=0;app_root_length=0;
#ifdef __PSP__
    save_root_length=0;
#endif
    if(!getcwd(app_root,sizeof app_root))return failure(r,PORTABLE_ASSETS_PATH,NULL);
    app_root_length=(unsigned)strlen(app_root);
    while(app_root_length>1 && app_root[app_root_length-1]=='/')app_root[--app_root_length]=0;
#ifdef __PSP__
    save_root_init();
#endif
    if(make_directory("data") || make_directory("data/log") ||
       make_directory("data/cache"))return failure(r,PORTABLE_ASSETS_IO,"data/");
    if(verified_install && seal_invalidate()<0)return failure(r,PORTABLE_ASSETS_IO,SEAL_NAME);
    char path[JGR_PORTABLE_PATH_MAX];
    if(app_path(path,MANIFEST_NAME)<0)return failure(r,PORTABLE_ASSETS_PATH,MANIFEST_NAME);
    FILE *f=fopen(path,"rb");if(!f)return failure(r,PORTABLE_ASSETS_MISSING_MANIFEST,MANIFEST_NAME);
    uint32_t crc=0,metadata=0;unsigned bytes=0;
    int result=manifest_header(f,r);
    if(!result && (fseek(f,0,SEEK_SET) || crc_file(f,&crc,&bytes,MAX_MANIFEST_BYTES)))result=failure(r,PORTABLE_ASSETS_IO,MANIFEST_NAME);
    if(!result && crc!=JGR_ASSET_MANIFEST_CRC32)result=failure(r,PORTABLE_ASSETS_IDENTITY,MANIFEST_NAME);
    /* The installer publishes this record only after successful validation.
       Rechecking 1,892 Memory Stick entries here costs over a minute and is
       repeated in the game executable. Trust completed installs; deleting
       the seal requests a full check, as does a reported game-data error. */
    if(!result && !verified_install && seal_matches(NULL)){
        if(fclose(f))return failure(r,PORTABLE_ASSETS_IO,MANIFEST_NAME);
        assets_ready=1;return 0;
    }
    if(!result)result=walk_manifest(f,r,0,&metadata,progress);
    /* Only a successful installer in this process can take this route. Its
       canonical manifest CRC and every size are still checked above. It has
       already hashed copied buffers and completed seek-based outputs. */
    if(!result && verified_install)r->cache_written=(unsigned)seal_write(metadata);
    else if(!result && !seal_matches(&metadata)) {
        uint32_t validated=0;r->full_verification=1;
        result=walk_manifest(f,r,1,&validated,progress);
        if(!result && validated!=metadata)result=failure(r,PORTABLE_ASSETS_IO,"assets changed during validation");
        if(!result)r->cache_written=(unsigned)seal_write(metadata);
    }
    if(fclose(f) && !result)result=failure(r,PORTABLE_ASSETS_IO,MANIFEST_NAME);
    if(!result)assets_ready=1;
    return result;
}
int portable_assets_init(PortableAssetsResult *r,
                        void (*progress)(unsigned,unsigned,const char *))
{return assets_init(r,progress,0);}
const char *portable_assets_error(int status)
{
    switch(status) {
    case PORTABLE_ASSETS_OK:return "Assets ready";
    case PORTABLE_ASSETS_MISSING_MANIFEST:return "Assets have not been installed";
    case PORTABLE_ASSETS_INVALID_MANIFEST:return "Asset manifest is damaged";
    case PORTABLE_ASSETS_VERSION:return "Asset format version does not match this build";
    case PORTABLE_ASSETS_IDENTITY:return "Asset package does not match this build";
    case PORTABLE_ASSETS_MISSING_FILE:return "An installed asset is missing";
    case PORTABLE_ASSETS_SIZE:return "An installed asset has the wrong size";
    case PORTABLE_ASSETS_CHECKSUM:return "An installed asset failed verification";
    case PORTABLE_ASSETS_PATH:return "The application path is invalid or too long";
    case PORTABLE_ASSETS_CANCELLED:return "Setup cancelled";
    default:return "Memory Stick access failed";
    }
}
#ifdef __PSP__
/* Setup uses the SDK font over optional local artwork. Normal startup uses
   the separately prepared original warning without overlays. Neither image
   is embedded in the executable or requires disc access during boot. */
#define SETUP_WIDTH 480
#define SETUP_HEIGHT 272
#define SETUP_STRIDE 512
#define SETUP_BACKGROUND "data/setup/background.jbg"
#define STARTUP_WARNING "data/setup/startup-warning.jbg"
#define SETUP_BACKGROUND_BYTES (SETUP_WIDTH*SETUP_HEIGHT*2u)
#ifndef JGR_PORTABLE_SCREEN_VRAM
#define JGR_PORTABLE_SCREEN_VRAM ((volatile uint32_t *)0x44000000u)
#endif
#ifndef JGR_PORTABLE_SCREEN_ALLOC
#define JGR_PORTABLE_SCREEN_ALLOC malloc
#define JGR_PORTABLE_SCREEN_FREE free
#endif
extern unsigned char msx[]; /* libpspdebug's 8x8 font */
static uint16_t *screen_background;
static volatile unsigned screen_active;
static const PortableAssetsResult *screen_asset_result;
static uint64_t screen_started;
static unsigned screen_tick,screen_log_tick;
static char screen_log_phase[80];
static unsigned screen_elapsed(void)
{return (unsigned)((sceKernelGetSystemTimeWide()-screen_started)/1000000u);}
static void screen_log(const char *phase,const char *file,unsigned current,unsigned total,int force)
{
    unsigned now=sceKernelGetSystemTimeLow();
    if(!force && !strcmp(screen_log_phase,phase) && now-screen_log_tick<15000000u)return;
    screen_log_tick=now;snprintf(screen_log_phase,sizeof screen_log_phase,"%s",phase);
    FILE *log=fopen("data/log/setup.log","a");
    if(log){fprintf(log,"SETUP_PHASE elapsed=%u phase=%s current=%u total=%u file=%s\n",
        screen_elapsed(),phase,current,total,file?file:"");fclose(log);}
}
static unsigned screen_word(const unsigned char *p)
{return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static void screen_background_free(void)
{
    screen_active=0;
    screen_asset_result=NULL;
    JGR_PORTABLE_SCREEN_FREE(screen_background);
    screen_background=NULL;
}
static void screen_background_load_path(const char *path)
{
    unsigned char header[24];uint16_t *pixels=NULL;
    screen_background_free();
    FILE *file=fopen(path,"rb");
    if(!file)return;
    if(fread(header,1,sizeof header,file)!=sizeof header ||
       memcmp(header,"JGRBG01\0",8) || screen_word(header+8)!=SETUP_WIDTH ||
       screen_word(header+12)!=SETUP_HEIGHT ||
       screen_word(header+16)!=SETUP_BACKGROUND_BYTES)goto done;
    pixels=JGR_PORTABLE_SCREEN_ALLOC(SETUP_BACKGROUND_BYTES);
    if(!pixels)goto done;
    if(fread(pixels,1,SETUP_BACKGROUND_BYTES,file)!=SETUP_BACKGROUND_BYTES ||
       fgetc(file)!=EOF || ferror(file) ||
       (crc_update(0xffffffffu,pixels,SETUP_BACKGROUND_BYTES)^0xffffffffu)!=screen_word(header+20))goto done;
    screen_background=pixels;pixels=NULL;
done:
    JGR_PORTABLE_SCREEN_FREE(pixels);
    fclose(file);
}
static void screen_background_load(void)
{screen_background_load_path(SETUP_BACKGROUND);}
static uint32_t screen_background_pixel(uint16_t pixel)
{
    unsigned r=pixel&31,g=(pixel>>5)&63,b=pixel>>11;
    r=((r<<3)|(r>>2))*3/5;g=((g<<2)|(g>>4))*3/5;b=((b<<3)|(b>>2))*3/5;
    return 0xff000000u|(b<<16)|(g<<8)|r;
}
static void screen_band(int top,int bottom)
{
    for(int y=top;y<bottom;y++)for(int x=0;x<SETUP_WIDTH;x++)
        JGR_PORTABLE_SCREEN_VRAM[y*SETUP_STRIDE+x]=screen_background ?
            screen_background_pixel(screen_background[y*SETUP_WIDTH+x]):0xff101010u;
}
static void screen_glyph(int x,int y,unsigned char ch,int scale,uint32_t color,int italic)
{
    for(int row=0;row<8;row++) {
        unsigned bits=msx[ch*8+row];
        int shear=italic ? (7-row)*scale/4:0;
        for(int col=0;col<8;col++)if(bits&(0x80u>>col))
            for(int sy=0;sy<scale;sy++)for(int sx=0;sx<scale;sx++) {
                int px=x+col*scale+sx+shear,py=y+row*scale+sy;
                if(px>=0 && px<SETUP_WIDTH && py>=0 && py<SETUP_HEIGHT)
                    JGR_PORTABLE_SCREEN_VRAM[py*SETUP_STRIDE+px]=color;
            }
    }
}
static void screen_center(int y,const char *text,int scale,uint32_t color,int italic)
{
    int x=(SETUP_WIDTH-8*scale*(int)strlen(text))/2;
    for(;*text;text++,x+=8*scale) {
        screen_glyph(x+scale,y+scale,(unsigned char)*text,scale,0xff000000u,italic);
        screen_glyph(x,y,(unsigned char)*text,scale,color,italic);
    }
}
static void screen_text(int y,const char *text)
{screen_center(y,text,1,0xffffffffu,0);}
static void screen_bar(unsigned percent);
/* This row belongs only to the callback thread while setup is active. Main
   thread progress drawing stops at row232; no debug-font cursor is shared. */
void portable_assets_setup_tick(void)
{
    if(!screen_active)return;
    unsigned seconds=screen_elapsed();char text[64];
    screen_band(232,245);
    snprintf(text,sizeof text,"Elapsed %02u:%02u:%02u",seconds/3600,(seconds/60)%60,seconds%60);
    screen_text(234,text);
}
static const char *screen_content_label(const char *file)
{
    if(!file || !*file)return NULL;
    if(!strncmp(file,"adx/",4) || !strncmp(file,"sfx",3) ||
       strstr(file,".adx") || strstr(file,".ADX"))return "Music and voices";
    if(!strncmp(file,"staffroute",10) || strstr(file,"_demo") ||
       !strcmp(file,"title_route.bin") || strstr(file,"_rival") || strstr(file,"_pose"))
        return "Demonstrations and credits";
    return "Graphics and level data";
}
static void screen_operation(const char *phase,const char *file,unsigned current,unsigned total,
                             const char *unit,const char *label,const uint64_t *prepared_bytes)
{
    unsigned now=sceKernelGetSystemTimeLow();
    int changed=strcmp(screen_log_phase,phase)!=0;
    screen_log(phase,file,current,total,changed);
    if(!changed && now-screen_tick<250000u && (!total || current!=total))return;
    screen_tick=now;
    char count[64];unsigned pct=total?(unsigned)((uint64_t)current*100/total):0;
    screen_band(128,232);
    if(label)screen_text(136,label);
    screen_text(160,phase);
    if(total)snprintf(count,sizeof count,"%u / %u %s",current,total,unit);
    else snprintf(count,sizeof count,"Working...");
    screen_text(176,count);screen_bar(pct);
    if(prepared_bytes)snprintf(count,sizeof count,"%llu MB prepared | %u%% of files",
        (unsigned long long)(*prepared_bytes/1000000u),pct);
    else if(total)snprintf(count,sizeof count,"%u%% of %s",pct,unit);
    else snprintf(count,sizeof count,"This step has no time estimate.");
    screen_text(216,count);
}
static void screen_begin(void)
{
    pspDebugScreenInit();
    pspDebugScreenEnableBackColor(0);
    screen_band(0,SETUP_HEIGHT);
    screen_center(14,"JGR",5,0xffffffffu,0);
    screen_center(58,"PORTABLE",2,0xffb0b0b0u,1);
}
static void screen_bar(unsigned percent)
{
    if(percent>100)percent=100;
    int fill=60+(int)(360*percent/100);
    for(int y=196;y<208;y++)for(int x=60;x<420;x++)
        JGR_PORTABLE_SCREEN_VRAM[y*SETUP_STRIDE+x]=x<fill ? 0xff00d6ffu:0xff404040u;
}
void portable_assets_show_startup(void)
{
    screen_background_load_path(STARTUP_WARNING);
    pspDebugScreenInit();
    pspDebugScreenEnableBackColor(0);
    for(int y=0;y<SETUP_HEIGHT;y++)for(int x=0;x<SETUP_WIDTH;x++) {
        unsigned p=screen_background ? screen_background[y*SETUP_WIDTH+x]:0;
        unsigned r=p&31,g=(p>>5)&63,b=p>>11;
        r=(r<<3)|(r>>2);g=(g<<2)|(g>>4);b=(b<<3)|(b>>2);
        JGR_PORTABLE_SCREEN_VRAM[y*SETUP_STRIDE+x]=0xff000000u|(b<<16)|(g<<8)|r;
    }
    /* No heading, countdown or minimum display time. The image is in VRAM;
       keep no allocation or callback ownership during table loading. */
    screen_background_free();
}
static void screen_progress(unsigned current,unsigned total,const char *name)
{
    setup_power_tick();
    screen_operation(screen_asset_result && !screen_asset_result->full_verification?
        "Checking installed file sizes":"Verifying installed assets",name,current,total,"files",
        screen_content_label(name),NULL);
}
#if JGR_BUNDLED_RUNTIME
static void screen_bundle_progress(unsigned current,unsigned total,const char *name,void *user)
{
    (void)user;setup_power_tick();
    const char *phase=name && (!strncmp(name,"Extracting",10) || !strncmp(name,"Verifying extracted",19))?
        "Preparing application":"Checking application";
    screen_operation(phase,name,(current+1023)/1024,(total+1023)/1024,"KB",NULL,NULL);
}
#endif
static void screen_error(const char *message)
{
    screen_active=0;
    screen_begin();
    screen_text(96,"ASSET SETUP REQUIRED");
    screen_text(120,message);
    screen_text(136,"Details saved in the setup log.");
#if JGR_DEVICE_SETUP
    screen_text(160,"Place your supported GDI and its tracks");
    screen_text(176,"in the same folder as EBOOT.PBP.");
    screen_text(200,"Completed setup files are retained.");
    screen_text(216,"Restart to continue asset preparation.");
#elif JGR_BUNDLED_RUNTIME
    screen_text(160,"Start EBOOT.PBP to check and repair");
    screen_text(176,"the installed game data automatically.");
    screen_text(200,"Completed setup files are retained.");
    screen_text(216,"Your save files are kept separately.");
#else
    screen_text(160,"Prepare assets on your computer using");
    screen_text(176,"your own compatible game disc.");
    screen_text(200,"Copy data/assets beside EBOOT.PBP.");
    screen_text(216,"See the portable setup README.");
#endif
    screen_text(248,"Press START to return to the XMB.");
}
#if JGR_DEVICE_SETUP
static void screen_worker_log(const JgrInstallResult *r,int force)
{
    static unsigned tick;
    unsigned now=sceKernelGetSystemTimeLow();
    if(!force && now-tick<15000000u)return;
    FILE *log=fopen("data/log/setup.log","a");
    if(log){
        unsigned elapsed=screen_elapsed();
        if(r)fprintf(log,"SETUP_PIPELINE elapsed=%u queued=%u published=%u overlapped_jobs=%u drain_us=%llu\n",
            elapsed,r->pipeline.queued,r->pipeline.published,r->pipeline.overlapped_jobs,
            (unsigned long long)r->pipeline.drain_us);
        JgrModelsStats models;jgr_models_stats(&models);
        fprintf(log,"SETUP_MODELS elapsed=%u jobs=%u rows=%u textures=%u hits=%u total_us=%llu geometry_us=%llu finish_us=%llu pack_us=%llu texture_us=%llu prepare_us=%llu wait_us=%llu cpu_texture_us=%llu scratch_peak=%llu spills=%u opens=%u\n",
            elapsed,models.jobs,models.rows,models.textures,models.cache_hits,
            (unsigned long long)models.total_us,(unsigned long long)models.geometry_us,
            (unsigned long long)models.finish_us,(unsigned long long)models.pack_us,
            (unsigned long long)models.texture_us,(unsigned long long)models.prepare_us,
            (unsigned long long)models.wait_us,(unsigned long long)models.cpu_texture_us,
            (unsigned long long)models.scratch_peak,models.spills,models.opens);
        JgrStageStats city;jgr_stage_stats(&city);
        fprintf(log,"SETUP_CITY elapsed=%u jobs=%u completed=%u failed=%u builds=%u active=%u stage=%u kind=%u phase=%u current_us=%llu total_us=%llu input_us=%llu geometry_us=%llu texture_us=%llu assembly_us=%llu postprocess_us=%llu objects=%llu batches=%llu vertices=%llu triangles=%llu textures=%llu output_bytes=%llu\n",
            elapsed,city.jobs,city.completed,city.failed,city.builds,city.active,city.stage,city.kind,city.phase,
            (unsigned long long)city.current_us,(unsigned long long)city.total_us,
            (unsigned long long)city.input_us,(unsigned long long)city.geometry_us,
            (unsigned long long)city.texture_us,(unsigned long long)city.assembly_us,
            (unsigned long long)city.postprocess_us,(unsigned long long)city.objects,
            (unsigned long long)city.batches,(unsigned long long)city.vertices,
            (unsigned long long)city.triangles,(unsigned long long)city.textures,
            (unsigned long long)city.output_bytes);
        JgrReplayStats replay;jgr_replay_stats(&replay);
        fprintf(log,"SETUP_REPLAY elapsed=%u requests=%u outputs=%u reused_outputs=%u jobs=%u native=%u interpreted=%u failed=%u active=%u job=%u state=%u backend=%u family=%u active_us=%llu state_us=%llu total_us=%llu tables_us=%llu bootstrap_us=%llu setup_us=%llu generation_us=%llu packing_us=%llu copy_us=%llu output_bytes=%llu demo_jobs=%u credits_jobs=%u opponent_jobs=%u title_jobs=%u demo_us=%llu credits_us=%llu opponent_us=%llu title_us=%llu file=%s\n",
            elapsed,replay.output_requests,replay.outputs,replay.reused_outputs,replay.jobs,
            replay.native_jobs,replay.interpreted_jobs,replay.failures,replay.active,replay.job_index,
            replay.state,replay.backend,replay.family,(unsigned long long)replay.active_us,
            (unsigned long long)replay.state_us,(unsigned long long)replay.total_us,
            (unsigned long long)replay.tables_us,(unsigned long long)replay.bootstrap_us,
            (unsigned long long)replay.setup_us,(unsigned long long)replay.generation_us,
            (unsigned long long)replay.packing_us,(unsigned long long)replay.copy_us,
            (unsigned long long)replay.output_bytes,replay.family_jobs[JGR_REPLAY_DEMO],
            replay.family_jobs[JGR_REPLAY_CREDITS],replay.family_jobs[JGR_REPLAY_OPPONENT],
            replay.family_jobs[JGR_REPLAY_TITLE],(unsigned long long)replay.family_us[JGR_REPLAY_DEMO],
            (unsigned long long)replay.family_us[JGR_REPLAY_CREDITS],
            (unsigned long long)replay.family_us[JGR_REPLAY_OPPONENT],
            (unsigned long long)replay.family_us[JGR_REPLAY_TITLE],replay.output);
        JgrInstallWorkStats work;jgr_install_work_stats(&work);
        for(unsigned i=0;i<2;i++){
            const JgrInstallWorkGroup *w=i?&work.replay:&work.city;
            fprintf(log,"SETUP_WORK elapsed=%u family=%s started=%u completed=%u failed=%u convert_us=%llu flush_us=%llu verify_us=%llu publish_us=%llu bytes=%llu\n",
                elapsed,i?"replay":"city",w->started,w->completed,w->failed,
                (unsigned long long)w->convert_us,(unsigned long long)w->flush_us,
                (unsigned long long)w->verify_us,(unsigned long long)w->publish_us,
                (unsigned long long)w->bytes);
        }
#if JGR_SETUP_ME
        JgrSetupMeStats m;jgr_setup_me_stats(&m);
        fprintf(log,"SETUP_ME elapsed=%u available=%u active=%u disabled=%u load_error=%d boot_status=%d jobs=%llu completed=%llu cancelled=%llu timeouts=%llu cache_us=%llu wait_us=%llu\n",
            elapsed,m.available,m.active,m.disabled,m.load_error,m.boot_status,
            (unsigned long long)m.jobs,(unsigned long long)m.completed,(unsigned long long)m.cancelled,
            (unsigned long long)m.timeouts,(unsigned long long)m.cache_us,(unsigned long long)m.wait_us);
#endif
        fclose(log);
    }
    tick=now;
}
static void screen_copy_log(const JgrInstallResult *r,int force)
{
    static uint64_t logged_bytes;
    static unsigned tick;
    const JgrInstallCopyStats *c=&r->copy;
    unsigned now=sceKernelGetSystemTimeLow();
    if(!c->chunks || (!force && (c->bytes==logged_bytes || now-tick<15000000u)))return;
    FILE *log=fopen("data/log/setup.log","a");
    if(log){
        fprintf(log,"SETUP_COPY elapsed=%u bytes=%llu files=%u chunks=%u bulk=%u max_chunk=%u read_us=%llu write_us=%llu hash_us=%llu callback_us=%llu publish_us=%llu\n",
            screen_elapsed(),(unsigned long long)c->bytes,c->files,c->chunks,c->bulk_chunks,c->max_chunk,
            (unsigned long long)c->read_us,(unsigned long long)c->write_us,
            (unsigned long long)c->hash_us,(unsigned long long)c->callback_us,
            (unsigned long long)c->publish_us);
        fclose(log);
    }
    logged_bytes=c->bytes;tick=now;
}
static int screen_install_progress(const JgrInstallResult *r,void *user)
{
    (void)user;
    setup_power_tick();
    screen_copy_log(r,0);
    screen_worker_log(r,0);
    if(setup_exit_requested)return 0;
    static const char *const phases[]={"Checking conversion instructions","Checking unfinished setup files",
        "Checking existing files","Checking free space","Verifying source disc",
        "Preparing game assets","Verifying prepared assets","Finishing installation","Installation complete"};
    const char *phase=r->phase<sizeof phases/sizeof *phases?phases[r->phase]:"Preparing game assets";
    int converting=r->phase==JGR_INSTALL_PHASE_CONVERT;
    const char *label=(converting || r->phase==JGR_INSTALL_PHASE_SCAN ||
                      r->phase==JGR_INSTALL_PHASE_VERIFY)?screen_content_label(r->file):NULL;
    screen_operation(phase,r->file,r->phase_current,r->phase_total,
                     converting?"files prepared":"files checked",label,
                     converting?&r->completed_bytes:NULL);
    return 1;
}
static void screen_install_error(int status)
{
    screen_error(jgr_install_error(status));
    if(status==JGR_INSTALL_DISC)return;
    screen_band(150,193);
    if(status==JGR_INSTALL_SPACE) {
        screen_text(160,"Free up space on your Memory Stick,");
        screen_text(176,"then restart to continue setup.");
    } else if(status==JGR_INSTALL_PLAN || status==JGR_INSTALL_INCOMPLETE) {
        screen_text(160,"Replace EBOOT.PBP with the latest build,");
        screen_text(176,"then restart to continue setup.");
    } else if(status==JGR_INSTALL_CHECKSUM) {
        screen_text(160,"Check your disc image and Memory Stick,");
        screen_text(176,"then restart to retry setup.");
    } else {
        screen_text(160,"Check your Memory Stick and restart.");
        screen_text(176,"If setup fails again, share the setup log.");
    }
}
#endif
#endif
void portable_assets_data_error(void)
{
    assets_ready=0;
    int invalidate_failed=seal_invalidate()<0;
#ifdef __PSP__
    screen_background_load();
    screen_error("Generated game data is invalid");
    if(invalidate_failed){
        screen_band(150,232);
        screen_text(160,"Could not reset asset verification.");
        screen_text(176,"Delete data/cache/assets.checked");
        screen_text(200,"then restart to check and repair.");
    }
    screen_background_free();
#else
    (void)invalidate_failed;
#endif
}
int portable_assets_bootstrap(void)
{
    PortableAssetsResult result;
    /* Completed installs enter the game before touching the setup display,
       manifest, asset inventory, or setup log. The receipt already identifies
       the verified pack; removing it requests the full check below. */
    assets_ready=0;app_root_length=0;
#ifdef __PSP__
    save_root_length=0;
#endif
    if(setup_exit_requested)return PORTABLE_ASSETS_CANCELLED;
    if(getcwd(app_root,sizeof app_root)){
        app_root_length=(unsigned)strlen(app_root);
        while(app_root_length>1 && app_root[app_root_length-1]=='/')app_root[--app_root_length]=0;
#ifdef __PSP__
        save_root_init();
#endif
    }
    if(app_root_length && seal_matches(NULL)){
#if JGR_BUNDLED_RUNTIME
        PortableBundleResult bundle;
        int bundle_status=portable_bundle_try_cached(app_root,&bundle);
        /* Updating EBOOT may require new project executables. Prepare these
           silently once; this never examines or regenerates game assets. */
        if(bundle_status<0 && !setup_exit_requested)
            bundle_status=portable_bundle_prepare(app_root,&bundle);
        if(setup_exit_requested)return PORTABLE_ASSETS_CANCELLED;
        if(bundle_status<0){
#ifdef __PSP__
            FILE *log=fopen("data/log/setup.log","a");
            if(log){fprintf(log,"APPLICATION status=%d detail=%s\n",bundle_status,
                            portable_bundle_error(bundle_status));fclose(log);}
            screen_background_load();
            screen_error("The application could not be prepared");
            screen_band(150,193);
            screen_text(160,"Replace EBOOT.PBP with the latest build,");
            screen_text(176,"then restart to continue setup.");
            screen_background_free();
#endif
            return PORTABLE_ASSETS_IO;
        }
#endif
        if(setup_exit_requested)return PORTABLE_ASSETS_CANCELLED;
        assets_ready=1;return 0;
    }
#ifdef __PSP__
    screen_background_load();
    screen_begin();
    screen_started=sceKernelGetSystemTimeWide();screen_tick=screen_log_tick=0;screen_log_phase[0]=0;
    screen_active=1;
    screen_text(96,"Checking your installed game data");
    screen_text(112,"Setup time depends on the current step.");
    screen_text(160,"Checking installed assets");
    screen_bar(0);
    screen_text(216,"0%");
    screen_text(248,"Keep the Memory Stick inserted.");
    screen_asset_result=&result;
    int status=portable_assets_init(&result,screen_progress);
    screen_log("Installed asset check complete",result.file,0,0,1);
    if(setup_exit_requested){screen_background_free();return PORTABLE_ASSETS_CANCELLED;}
#if JGR_BUNDLED_RUNTIME
    /* Helper extraction reads EBOOT, never the source disc. Do not invoke the
       disc installer at all when the generated pack has passed validation. */
    PortableBundleResult bundle;
    int bundle_status=portable_bundle_prepare_progress(app_root,&bundle,screen_bundle_progress,NULL);
    if(bundle_status<0) {
        screen_log(portable_bundle_error(bundle_status),"EBOOT.PBP",0,0,1);
        screen_error("The application could not be prepared");
        screen_band(150,193);
        screen_text(160,"Replace EBOOT.PBP with the latest build,");
        screen_text(176,"then restart to continue setup.");
        screen_background_free();return PORTABLE_ASSETS_IO;
    }
    if(setup_exit_requested){screen_background_free();return PORTABLE_ASSETS_CANCELLED;}
#endif
#if JGR_DEVICE_SETUP
#if JGR_SETUP_CRASH
    /* The verified bundle supplies the exception helper. Its reporter sleeps
       until a fault; conversion needs no extra polling or per-file I/O. */
    int crash_status=crash_init();
    FILE *crash_log=fopen("data/log/setup.log","a");
    if(crash_log){fprintf(crash_log,"SETUP_CRASH status=%d build=%s\n",crash_status,crash_build_id());fclose(crash_log);}
#endif
    if(status<0) {
        /* Never leave a trusted completion record behind while repair can
           replace files, even if power is lost before it finishes. */
        if(seal_invalidate()<0){
            screen_error("Could not reset asset verification");
            screen_background_free();return PORTABLE_ASSETS_IO;
        }
        char recipe[JGR_PORTABLE_PATH_MAX];JgrInstallResult installation;
        screen_band(88,128);screen_text(96,"One-time game preparation");
        screen_text(112,"File progress is not a time estimate.");
        int install_status=portable_bundle_path(PORTABLE_BUNDLE_RECIPE,recipe,sizeof recipe);
        if(!install_status){
#if JGR_SETUP_ME
            /* Only an already verified project helper may start the worker.
             * Unsupported firmware/emulators continue with CPU conversion. */
            char helper[PORTABLE_BUNDLE_PATH_MAX];
            if(!portable_bundle_path(PORTABLE_BUNDLE_ME,helper,sizeof helper))jgr_setup_me_init(helper);
            screen_worker_log(NULL,1);
#endif
            install_status=jgr_install_run(app_root,recipe,NULL,0,screen_install_progress,NULL,&installation);
            screen_copy_log(&installation,1);
#if JGR_SETUP_ME
            /* Installer has drained its CPU worker. The ME transport must
             * physically fence and unload before any executable handoff. */
            if(!jgr_setup_me_shutdown()){
                screen_worker_log(&installation,1);
                screen_error("Waiting for the setup worker to stop");
                screen_background_free();return PORTABLE_ASSETS_IO;
            }
#endif
            screen_worker_log(&installation,1);
        }
        else {memset(&installation,0,sizeof installation);install_status=JGR_INSTALL_PLAN;}
        if(setup_exit_requested){screen_background_free();return PORTABLE_ASSETS_CANCELLED;}
        if(install_status<0) {
            char log_path[JGR_PORTABLE_PATH_MAX];
            if(!app_path(log_path,"data/log/setup.log")){
                FILE *log=fopen(log_path,"a");if(log){fprintf(log,"SETUP status=%d file=%s detail=%s\n",install_status,installation.file,installation.detail);fclose(log);}
            }
            screen_install_error(install_status);
            screen_background_free();return PORTABLE_ASSETS_IO;
        }
        screen_log("Conversion complete",NULL,installation.total,installation.total,1);
        status=assets_init(&result,screen_progress,1);
    }
#endif
    if(setup_exit_requested){screen_background_free();return PORTABLE_ASSETS_CANCELLED;}
    if(status<0)screen_error(portable_assets_error(status));
    else {
        screen_log("Assets ready",NULL,result.files,result.files,1);
        screen_band(150,232);
        screen_text(160,"Assets ready");
        screen_bar(100);
        screen_text(216,"100%");
    }
    /* Setup pixels already reside in VRAM. Release the temporary 255KiB
       image on both success and failure, before any game loaders run. */
    screen_background_free();
    return status;
#else
    return portable_assets_init(&result,NULL);
#endif
}
#else
int portable_assets_bootstrap(void){return 0;}
void portable_assets_setup_request_exit(void){}
int portable_assets_setup_exiting(void){return 0;}
#endif
#if !defined(__PSP__) || !JGR_PORTABLE_ASSETS
void portable_assets_setup_tick(void){}
void portable_assets_show_startup(void){}
#endif
