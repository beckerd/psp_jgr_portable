/* Setup-session collision scratch and immutable pose storage; no state or
 * payload survives the session, and no prior launch's files are trusted. */
#include "portable_native_cache.h"
#include "portable_world_convert.h"
#include "portable_native_pose.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#undef fopen
#undef stat
#undef remove

struct JgrNativeCache {
    JgrDisc *disc;
    char directory[1024],path[3][1100];
    unsigned ready,created;
    long size[3];
    JgrTableDependency dependency;
    void *dependency_user;
    Chars bank;
    unsigned borrowers,closing;
};
static int file_info(const char *path,struct stat *info)
{
#ifdef __PSP__
    return stat(path,info);
#else
    return lstat(path,info);
#endif
}
JgrNativeCache *jgr_native_cache_create(JgrDisc *disc,const char *directory)
{
    struct stat info;
    if(!disc || !directory || !*directory || strlen(directory)>=1024 ||
       file_info(directory,&info) || !S_ISDIR(info.st_mode))return NULL;
    JgrNativeCache *cache=calloc(1,sizeof *cache);if(!cache)return NULL;
    cache->disc=disc;strcpy(cache->directory,directory);
    for(unsigned i=0;i<3;i++){
        int bytes=snprintf(cache->path[i],sizeof cache->path[i],"%s/native-stage%u.col.tmp",directory,i+1);
        if(bytes<0 || (size_t)bytes>=sizeof cache->path[i]){free(cache);return NULL;}
    }
    return cache;
}
int jgr_native_cache_collision(JgrNativeCache *cache,JgrDisc *disc,const char *directory,
                               unsigned stage,char *path,size_t path_bytes)
{
    if(!cache || cache->closing || cache->disc!=disc || !directory || strcmp(cache->directory,directory) ||
       (stage<1 || stage>3) || !path)return -1;
    unsigned index=stage-1,bit=1u<<index;const char *name=cache->path[index];
    size_t length=strlen(name);if(length>=path_bytes)return -1;
    struct stat info;int exists=file_info(name,&info);
    if((!exists && !S_ISREG(info.st_mode)) || (exists && errno!=ENOENT))return -1;
    if(!(cache->ready&bit) || exists || info.st_size!=cache->size[index]){
        cache->ready&=~bit;
        FILE *output=fopen(name,"w+b");if(!output)return -1;
        cache->created|=bit;setvbuf(output,NULL,_IOFBF,65536);
        int failed=jgr_world_native_collision(disc,stage,output);
        long size=ftell(output);if(size<=0)failed=1;
        if(fclose(output))failed=1;
        if(failed){remove(name);return -1;}
        cache->size[index]=size;cache->ready|=bit;
    }
    memcpy(path,name,length+1);return 0;
}
static int bank_identity(JgrNativeCache *cache,JgrDisc *disc,JgrTableDependency dependency,void *user)
{
    return cache && !cache->closing && cache->disc==disc && dependency &&
           (!cache->dependency || (cache->dependency==dependency && cache->dependency_user==user));
}
static void bank_drop(JgrNativeCache *cache)
{
    if(!cache->bank.base)return;
    jgr_native_chars_pose_forget();
    free(cache->bank.motion_data);free(cache->bank.base);
    memset(&cache->bank,0,sizeof cache->bank);
}
int jgr_native_cache_bank_borrow(JgrNativeCache *cache,JgrDisc *disc,
                                JgrTableDependency dependency,void *user,Chars *bank)
{
    if(!bank || bank->base || bank->motion_data || !bank_identity(cache,disc,dependency,user) ||
       !cache->bank.base || cache->borrowers==UINT_MAX)return 0;
    *bank=cache->bank;cache->borrowers++;return 1;
}
void jgr_native_cache_bank_adopt(JgrNativeCache *cache,JgrDisc *disc,
                                JgrTableDependency dependency,void *user,Chars *bank)
{
    if(!bank || !bank->base || bank->motion_owner || !bank_identity(cache,disc,dependency,user) ||
       cache->bank.base || cache->borrowers)return;
    /* Transfer the two existing allocations, never create another resident bank. */
    cache->dependency=dependency;cache->dependency_user=user;
    cache->bank=*bank;cache->borrowers=1;
}
int jgr_native_cache_bank_release(JgrNativeCache *cache,Chars *bank)
{
    if(!cache || !bank || !cache->borrowers || !cache->bank.base ||
       bank->base!=cache->bank.base || bank->motion_data!=cache->bank.motion_data)return 0;
    cache->borrowers--;
    if(cache->closing && !cache->borrowers){bank_drop(cache);free(cache);}
    return 1;
}
void jgr_native_cache_trim(JgrNativeCache *cache,size_t available_bytes)
{
    if(cache && !cache->borrowers && available_bytes<4u*1024u*1024u)bank_drop(cache);
}
void jgr_native_cache_destroy(JgrNativeCache *cache)
{
    if(!cache)return;
    for(unsigned i=0;i<3;i++)if(cache->created&(1u<<i))remove(cache->path[i]);
    cache->ready=cache->created=0;cache->closing=1;
    /* Normally all generators have released their views first. Keep ownership
     * valid even if a cancelled caller closes its session before its last view. */
    if(cache->borrowers)return;
    bank_drop(cache);
    free(cache);
}
