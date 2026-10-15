/* Host implementation of the PSP raw-I/O boundary. SDK flags deliberately
   retain their PSP numeric values, which differ from POSIX open flags. */
#undef fopen
#undef stat
#undef remove
#undef rename
#undef sceIoOpen
#undef sceIoGetstat
#undef sceIoRemove
#undef sceIoRename
#include <pspiofilemgr.h>
#include <pspdebug.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
unsigned portable_test_asset_stats,portable_test_asset_opens;
unsigned portable_test_seal_remove_fail;
unsigned portable_test_manifest_opens,portable_test_background_opens;
unsigned portable_test_file_writes,portable_test_directory_writes,portable_test_debug_calls;
unsigned portable_test_background_alloc_calls;
/* PSP device names are relative directories on the host. Tests create them
   only beneath their disposable cwd, never in a real mounted PSP or /PSP. */
char portable_test_app_root[512];
char *portable_test_raw_getcwd(char *buffer,size_t size)
{
    if(!portable_test_app_root[0])return getcwd(buffer,size);
    size_t n=strlen(portable_test_app_root);
    if(n>=size){errno=ERANGE;return NULL;}
    memcpy(buffer,portable_test_app_root,n+1);return buffer;
}
static int installed_payload(const char *name)
{
    const char *p=strstr(name,"/data/assets/");
    return p && strcmp(p+13,"assets-manifest.txt");
}
int portable_test_raw_stat(const char *name,struct stat *info)
{if(installed_payload(name))portable_test_asset_stats++;return stat(name,info);}
FILE *portable_test_raw_fopen(const char *name,const char *mode)
{
    if(installed_payload(name))portable_test_asset_opens++;
    if(strstr(name,"data/assets/assets-manifest.txt"))portable_test_manifest_opens++;
    if(strstr(name,"data/setup/background.jbg") || strstr(name,"data/setup/startup-warning.jbg"))portable_test_background_opens++;
    if(strpbrk(mode,"wa+"))portable_test_file_writes++;
    return fopen(name,mode);
}
int portable_test_raw_mkdir(const char *name,mode_t mode)
{portable_test_directory_writes++;return mkdir(name,mode);}
int portable_test_raw_rename(const char *from,const char *to)
{portable_test_directory_writes++;return rename(from,to);}
int portable_test_raw_remove(const char *name)
{
    portable_test_directory_writes++;
    static const char prefix[]="/data/cache/assets.checked";
    const char *p=strstr(name,prefix);
    if(portable_test_seal_remove_fail && p && (!p[sizeof prefix-1] ||
       !strcmp(p+sizeof prefix-1,".tmp"))){errno=EACCES;return -1;}
    return remove(name);
}
SceUID sceIoOpen(const char *name,int flags,SceMode mode)
{
    int native=(flags&3)==PSP_O_RDWR ? O_RDWR:(flags&3)==PSP_O_WRONLY ? O_WRONLY:O_RDONLY;
    if(flags&PSP_O_CREAT)native|=O_CREAT;
    if(flags&PSP_O_TRUNC)native|=O_TRUNC;
    if(flags&PSP_O_APPEND)native|=O_APPEND;
    return open(name,native,mode);
}
int sceIoGetstat(const char *name,SceIoStat *value)
{struct stat s;int result=stat(name,&s);if(!result)value->size=(unsigned)s.st_size;return result;}
int sceIoRemove(const char *name){return remove(name);}
int sceIoRename(const char *a,const char *b){return rename(a,b);}
int sceIoRead(int fd,void *data,unsigned n){return (int)read(fd,data,n);}
int sceIoClose(int fd){return close(fd);}
void pspDebugScreenInit(void){portable_test_debug_calls++;}
void pspDebugScreenEnableBackColor(int value){(void)value;portable_test_debug_calls++;}
void pspDebugScreenSetTextColor(unsigned value){(void)value;portable_test_debug_calls++;}
void pspDebugScreenSetXY(int x,int y){(void)x;(void)y;portable_test_debug_calls++;}
void pspDebugScreenClear(void){portable_test_debug_calls++;}
int pspDebugScreenPrintf(const char *format,...){(void)format;portable_test_debug_calls++;return 0;}
unsigned portable_test_power_ticks,portable_test_clock;
unsigned sceKernelGetSystemTimeLow(void){return portable_test_clock+=1000000;}
unsigned long long sceKernelGetSystemTimeWide(void){return portable_test_clock+=1000000;}
int scePowerTick(int flags){(void)flags;portable_test_power_ticks++;return 0;}
/* A guarded framebuffer and synthetic font exercise startup UI writes. */
unsigned portable_test_screen[512*274];
unsigned char msx[256*8];
int portable_test_background_live,portable_test_background_fail_alloc;
unsigned portable_test_background_last_size;
void *portable_test_background_alloc(size_t size)
{
    portable_test_background_alloc_calls++;
    portable_test_background_last_size=(unsigned)size;
    if(portable_test_background_fail_alloc)return NULL;
    void *p=malloc(size);if(p)portable_test_background_live++;
    return p;
}
void portable_test_background_free(void *p)
{if(p)portable_test_background_live--;free(p);}
