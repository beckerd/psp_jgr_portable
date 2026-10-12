/* Isolated target-compiler proof for native conversions. The supplied plan can
 * be a subset and therefore does not establish complete-release readiness. */
#include "portable_install.h"
#include "portable_indexed.h"
#include <pspkernel.h>
#include <psppower.h>
#include <pspdebug.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
PSP_MODULE_INFO("JGR Setup Proof",0,1,0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-256);
/* PPSSPP maps external homebrew folders to writable umd0:, but implements
 * the Memory Stick free-space devctl only on ms0:. This proof-only link wrapper
 * changes that one query; production builds do not include or link it. */
int __real_sceIoDevctl(const char *,unsigned,void *,int,void *,int);
int __wrap_sceIoDevctl(const char *device,unsigned command,void *in,int inbytes,void *out,int outbytes)
{return __real_sceIoDevctl(!strcmp(device,"umd0:")?"ms0:":device,command,in,inbytes,out,outbytes);}
static unsigned tick;
static int progress(const JgrInstallResult *r,void *unused)
{
    (void)unused;unsigned now=sceKernelGetSystemTimeLow();
    if(now-tick>500000){tick=now;pspDebugScreenSetXY(0,3);pspDebugScreenPrintf("%u/%u %s          \n",r->written+r->reused,r->total,r->file);}
    return 1;
}
int main(void)
{
    pspDebugScreenInit();pspDebugScreenPrintf("JGR native setup proof (partial asset plan)\n");
    scePowerSetClockFrequency(333,333,166);
    char root[512];JgrInstallResult result={0};int status=-1,indexed=-1;
    unsigned long long start=sceKernelGetSystemTimeWide();
    if(getcwd(root,sizeof root))status=jgr_install_run(root,"install.recipe",NULL,0,progress,NULL,&result);
    FILE *input=fopen("texture-source.rgba","rb");
    if(input){unsigned d[4];if(fread(d,4,4,input)==4 && d[0]<=1024 && d[1]<=1024) {
        unsigned n=d[0]*d[1]*4;unsigned char *p=malloc(n);FILE *out=NULL;
        if(p && fread(p,1,n,input)==n && (out=fopen("texture-output.bin","wb"))) {
            JgrJgcTexture record;indexed=jgr_indexed_texture(p,d[0],d[1],d[2],d[3],out,&record);
            if(fclose(out))indexed=-1;
        }
        free(p);
    }fclose(input);}
    FILE *log=fopen("install-result.json","wb");
    if(log){fprintf(log,"{\"status\":%d,\"files\":%u,\"written\":%u,\"reused\":%u,\"indexed\":%d,\"microseconds\":%llu,\"heap_free\":%u,\"file\":\"%s\",\"detail\":\"%s\"}\n",
        status,result.total,result.written,result.reused,indexed,sceKernelGetSystemTimeWide()-start,
        sceKernelTotalFreeMemSize(),result.file,result.detail);fclose(log);}
    sceKernelDelayThread(1000000);sceKernelExitGame();return status?1:0;
}
