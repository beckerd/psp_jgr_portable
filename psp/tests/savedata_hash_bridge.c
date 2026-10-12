/* Mock only the firmware API boundary, not PSP cryptography. Capture the full
   hash inputs so Python can check both SFO integrity passes independently. */
#include <pspchnnlsv.h>
#include <pspmodulemgr.h>
#include <pspkerror.h>
#include <stdint.h>
#include <string.h>
int savedata_test_hash_step,savedata_test_hash_fail,savedata_test_hash_bad;
int savedata_test_hash_noop,savedata_test_service_fail;
int savedata_test_service_loaded,savedata_test_service_started,savedata_test_service_calls;
unsigned char savedata_test_hash_input[2][5120];
unsigned savedata_test_hash_bytes[2];
static int next_step(void)
{return ++savedata_test_hash_step==savedata_test_hash_fail ? -1:0;}
int sceKernelLoadModule(const char *path,int flags,SceKernelLMOption *option)
{
    savedata_test_service_calls++;
    if(strcmp(path,"flash0:/kd/chnnlsv.prx") || flags || option->size!=sizeof *option ||
       option->mpidtext!=1 || option->mpiddata!=1 || option->access!=1){savedata_test_hash_bad=1;return -1;}
    if(savedata_test_service_fail==1)return -1;
    if(savedata_test_service_loaded)return (int)SCE_KERNEL_ERROR_EXCLUSIVE_LOAD;
    savedata_test_service_loaded=1;return 42;
}
int sceKernelStartModule(int module,unsigned bytes,void *args,int *status,void *option)
{
    savedata_test_service_calls++;
    if(module!=42 || bytes || args || option){savedata_test_hash_bad=1;return -1;}
    if(savedata_test_service_fail==2)return -1;
    *status=savedata_test_service_fail==3 ? -1:0;
    if(!*status)savedata_test_service_started=1;
    return 0;
}
int sceSdSetIndex(SceSdContext1 *ctx,int mode)
{
    if(next_step())return -1;
    if(!savedata_test_service_started || savedata_test_hash_noop==1)return 0;
    if((uintptr_t)ctx%16 || (mode!=1 && mode!=2)){savedata_test_hash_bad=1;return -1;}
    ctx->mode=mode;return 0;
}
int sceSdRemoveValue(SceSdContext1 *ctx,unsigned char *data,int bytes)
{
    if(next_step())return -1;
    if(savedata_test_hash_noop==2)return 0;
    if((uintptr_t)data%16 || bytes%16 || bytes<=0 || bytes>5120){savedata_test_hash_bad=1;return -1;}
    memcpy(savedata_test_hash_input[ctx->mode-1],data,(unsigned)bytes);
    savedata_test_hash_bytes[ctx->mode-1]=(unsigned)bytes;ctx->size=16;return 0;
}
int sceSdGetLastIndex(SceSdContext1 *ctx,unsigned char *hash,unsigned char *key)
{
    if(next_step())return -1;
    if(savedata_test_hash_noop==3)return 0;
    if((uintptr_t)hash%16 || key){savedata_test_hash_bad=1;return -1;}
    for(unsigned i=0;i<16;i++)hash[i]=(unsigned char)(16*ctx->mode+i);
    return 0;
}
