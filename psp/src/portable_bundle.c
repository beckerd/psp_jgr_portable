/* Project-built runtime helpers carried in EBOOT's DATA.PSAR section. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#include "portable_crc32.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#undef sceIoOpen
#undef sceIoGetstat
#undef sceIoRemove
#undef sceIoRename
#include "portable_bundle.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HEADER_BYTES 24u
#define ENTRY_BYTES 16u
#define INDEX_BYTES (40u+HEADER_BYTES+5u*ENTRY_BYTES)
#define RECEIPT_NAME "bundle.checked"
#define TRANSFER_BYTES (16u*1024u)
#define MAX_MODULE (256u*1024u)
#define MAX_NOTICE (128u*1024u)
#define MAX_RECIPE (4u*1024u*1024u)
#define MAX_CONVERTER (8u*1024u*1024u)
#define MAX_BUNDLE (HEADER_BYTES+5u*ENTRY_BYTES+2u*MAX_MODULE+MAX_NOTICE+MAX_RECIPE+MAX_CONVERTER)
typedef struct { uint32_t role,offset,bytes,crc; } BundleEntry;
typedef struct { unsigned char data[INDEX_BYTES];size_t bytes; } BundleIndex;
typedef struct {
    PortableBundleProgress callback;
    void *user;
    char name[96];
} BundleProgress;
static BundleEntry entries[5];
static unsigned entry_count,ready;
static uint32_t psar_offset;
static char executable[PORTABLE_BUNDLE_PATH_MAX],cache[PORTABLE_BUNDLE_PATH_MAX];
static const char *const names[]={"jgr_me.prx","jgr_crash.prx","crash_handler_license.txt","install.recipe","setup.prx"};

static uint32_t word(const unsigned char *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t crc_update(uint32_t crc,const void *data,size_t n)
{return jgr_crc32_add(crc,data,n);}
static FILE *bulk_open(const char *path,const char *mode)
{
    FILE *file=fopen(path,mode);
    /* Call before any stream I/O. The transfer loops already provide their
     * own buffer; PSP newlib otherwise splits them into 1 KiB device calls. */
    if(file)(void)setvbuf(file,NULL,_IONBF,0);
    return file;
}
static void progress_begin(BundleProgress *p,const char *action,const char *name,unsigned total)
{
    if(!p->callback)return;
    snprintf(p->name,sizeof p->name,"%s%s%s",action,name?" ":"",name?name:"");
    p->callback(0,total,p->name,p->user);
}
static void progress_step(BundleProgress *p,unsigned current,unsigned total)
{if(p->callback)p->callback(current,total,p->name,p->user);}
static int join(char *out,size_t capacity,const char *root,const char *leaf)
{
    int n=snprintf(out,capacity,"%s%s%s",root,root[strlen(root)-1]=='/' ? "":"/",leaf);
    return n<0 || (size_t)n>=capacity ? PORTABLE_BUNDLE_PATH:0;
}
static int regular(const char *path,struct stat *info)
{
#ifndef __PSP__
    if(lstat(path,info)<0 || S_ISLNK(info->st_mode))return 0;
#endif
    return stat(path,info)==0 && S_ISREG(info->st_mode);
}
static int directory(const char *path)
{
    struct stat info;
#ifndef __PSP__
    if(lstat(path,&info)==0 && S_ISLNK(info.st_mode))return PORTABLE_BUNDLE_PATH;
#endif
    if(mkdir(path,0777)==0)return 0;
    return errno==EEXIST && stat(path,&info)==0 && S_ISDIR(info.st_mode) ? 0:PORTABLE_BUNDLE_IO;
}
static int existing_directory(const char *path)
{
    struct stat info;
#ifndef __PSP__
    if(lstat(path,&info)<0 || S_ISLNK(info.st_mode))return PORTABLE_BUNDLE_PATH;
#endif
    return stat(path,&info)==0 && S_ISDIR(info.st_mode) ? 0:PORTABLE_BUNDLE_PATH;
}
static int checksum(FILE *file,uint32_t bytes,uint32_t expected,BundleProgress *progress)
{
    unsigned char block[TRANSFER_BYTES];uint32_t crc=0xffffffffu,total=bytes;
    while(bytes) {
        size_t n=bytes<sizeof block ? bytes:sizeof block;
        if(fread(block,1,n,file)!=n)return PORTABLE_BUNDLE_IO;
        crc=crc_update(crc,block,n);bytes-=(uint32_t)n;
        progress_step(progress,total-bytes,total);
    }
    return (crc^0xffffffffu)==expected ? 0:PORTABLE_BUNDLE_CHECKSUM;
}
static int module_header(FILE *file,uint32_t bytes)
{
    unsigned char h[52];
    if(bytes<sizeof h || fread(h,1,sizeof h,file)!=sizeof h)return PORTABLE_BUNDLE_FORMAT;
    return memcmp(h,"\177ELF\1\1\1",7) || h[16]!=0xa0 || h[17]!=0xff ||
           h[18]!=8 || h[19]!=0 || h[40]!=52 || h[41]!=0 ? PORTABLE_BUNDLE_FORMAT:0;
}
static int bundle_index(FILE *file,uint32_t *index_crc,BundleIndex *index)
{
    unsigned char pbp[40],header[HEADER_BYTES],table[5*ENTRY_BYTES];
    if(fseek(file,0,SEEK_END))return PORTABLE_BUNDLE_IO;
    long end=ftell(file);
    if(end<40 || (unsigned long)end>INT32_MAX || fseek(file,0,SEEK_SET) ||
       fread(pbp,1,sizeof pbp,file)!=sizeof pbp)return PORTABLE_BUNDLE_FORMAT;
    if(memcmp(pbp,"\0PBP",4) || word(pbp+8)!=40)return PORTABLE_BUNDLE_FORMAT;
    uint32_t previous=40;
    for(unsigned i=0;i<8;i++) {
        uint32_t offset=word(pbp+8+i*4);
        if(offset<previous || offset>(uint32_t)end)return PORTABLE_BUNDLE_FORMAT;
        previous=offset;
    }
    psar_offset=previous;
    uint32_t total=(uint32_t)end-psar_offset;
    if(total<HEADER_BYTES+5*ENTRY_BYTES || total>MAX_BUNDLE ||
       fseek(file,(long)psar_offset,SEEK_SET) || fread(header,1,sizeof header,file)!=sizeof header)
        return PORTABLE_BUNDLE_FORMAT;
    entry_count=word(header+12);*index_crc=word(header+20);
    if(memcmp(header,"JGRBND2\0",8) || word(header+8)!=2 ||
       entry_count!=5 || word(header+16)!=total)return PORTABLE_BUNDLE_FORMAT;
    if(fread(table,ENTRY_BYTES,entry_count,file)!=entry_count)return PORTABLE_BUNDLE_IO;
    if((crc_update(0xffffffffu,table,entry_count*ENTRY_BYTES)^0xffffffffu)!=*index_crc)
        return PORTABLE_BUNDLE_CHECKSUM;
    uint32_t cursor=HEADER_BYTES+entry_count*ENTRY_BYTES;
    for(unsigned i=0;i<entry_count;i++) {
        const unsigned char *p=table+i*ENTRY_BYTES;BundleEntry *e=&entries[i];
        *e=(BundleEntry){word(p),word(p+4),word(p+8),word(p+12)};
        uint32_t limit=i<2 ? MAX_MODULE:i==2 ? MAX_NOTICE:i==3 ? MAX_RECIPE:MAX_CONVERTER;
        if(e->role!=i+1 || e->offset!=cursor || !e->bytes || e->bytes>limit ||
           cursor>total || e->bytes>total-cursor)return PORTABLE_BUNDLE_FORMAT;
        cursor+=e->bytes;
    }
    if(cursor!=total)return PORTABLE_BUNDLE_FORMAT;
    memcpy(index->data,pbp,sizeof pbp);
    memcpy(index->data+sizeof pbp,header,sizeof header);
    memcpy(index->data+sizeof pbp+sizeof header,table,entry_count*ENTRY_BYTES);
    index->bytes=sizeof pbp+sizeof header+entry_count*ENTRY_BYTES;
    return 0;
}
static int bundle_verify(FILE *file,BundleProgress *progress)
{
    for(unsigned i=0;i<entry_count;i++) {
        BundleEntry *e=&entries[i];int result;
        if(fseek(file,(long)(psar_offset+e->offset),SEEK_SET))return PORTABLE_BUNDLE_IO;
        if((i<2 || i==4) && (result=module_header(file,e->bytes))!=0)return result;
        if(fseek(file,(long)(psar_offset+e->offset),SEEK_SET))return PORTABLE_BUNDLE_IO;
        progress_begin(progress,"Checking bundled",names[i],e->bytes);
        if((result=checksum(file,e->bytes,e->crc,progress))!=0)return result;
    }
    return 0;
}
static size_t receipt_image(unsigned char *out,const BundleIndex *index)
{
    memcpy(out,"JGRCCH1\0",8);memcpy(out+8,index->data,index->bytes);
    return 8+index->bytes;
}
static int receipt_matches(const BundleIndex *index)
{
    char path[PORTABLE_BUNDLE_PATH_MAX];struct stat info;
    unsigned char expected[8+INDEX_BYTES],actual[8+INDEX_BYTES+1];
    size_t bytes=receipt_image(expected,index);
    if(join(path,sizeof path,cache,RECEIPT_NAME) ||
       !regular(path,&info) || info.st_size!=(long)bytes)return 0;
    FILE *file=bulk_open(path,"rb");if(!file)return 0;
    size_t got=fread(actual,1,bytes+1,file);
    int same=got==bytes && !ferror(file) && !memcmp(actual,expected,bytes);
    if(fclose(file))same=0;
    return same;
}
static int receipt_invalidate(void)
{
    char path[PORTABLE_BUNDLE_PATH_MAX];
    if(join(path,sizeof path,cache,RECEIPT_NAME))return PORTABLE_BUNDLE_PATH;
    return !remove(path) || errno==ENOENT ? 0:PORTABLE_BUNDLE_IO;
}
static int receipt_publish(const BundleIndex *index)
{
    char path[PORTABLE_BUNDLE_PATH_MAX],temporary[PORTABLE_BUNDLE_PATH_MAX];
    unsigned char body[8+INDEX_BYTES];size_t bytes=receipt_image(body,index);
    if(join(path,sizeof path,cache,RECEIPT_NAME))return PORTABLE_BUNDLE_PATH;
    int n=snprintf(temporary,sizeof temporary,"%s.tmp",path);
    if(n<0 || (size_t)n>=sizeof temporary)return PORTABLE_BUNDLE_PATH;
    if(remove(temporary) && errno!=ENOENT)return PORTABLE_BUNDLE_IO;
    FILE *file=bulk_open(temporary,"wb");if(!file)return PORTABLE_BUNDLE_IO;
    int failed=fwrite(body,1,bytes,file)!=bytes;
    if(fflush(file))failed=1;
    if(fclose(file))failed=1;
    /* The old receipt was removed before any cache repair. An interrupted
       publish therefore leaves only an ignored temporary, never a hit. */
    if(!failed && rename(temporary,path))failed=1;
    if(failed){remove(temporary);return PORTABLE_BUNDLE_IO;}
    return 0;
}
static int cached(const char *path,const BundleEntry *entry,BundleProgress *progress,
                  const char *action,const char *name)
{
    struct stat info;
    if(!regular(path,&info) || info.st_size!=(long)entry->bytes)return 0;
    FILE *file=bulk_open(path,"rb");if(!file)return 0;
    progress_begin(progress,action,name,entry->bytes);
    int same=checksum(file,entry->bytes,entry->crc,progress)==0;
    if(fclose(file))same=0;
    return same;
}
static int extract(FILE *source,unsigned index,PortableBundleResult *result,BundleProgress *progress)
{
    char path[PORTABLE_BUNDLE_PATH_MAX],temporary[PORTABLE_BUNDLE_PATH_MAX];
    BundleEntry *entry=&entries[index];
    if(join(path,sizeof path,cache,names[index]))return PORTABLE_BUNDLE_PATH;
    if(cached(path,entry,progress,"Checking cached",names[index])){result->reused++;return 0;}
    int n=snprintf(temporary,sizeof temporary,"%s.tmp",path);
    if(n<0 || (size_t)n>=sizeof temporary)return PORTABLE_BUNDLE_PATH;
    if(remove(temporary) && errno!=ENOENT)return PORTABLE_BUNDLE_IO;
    if(fseek(source,(long)(psar_offset+entry->offset),SEEK_SET))return PORTABLE_BUNDLE_IO;
    FILE *file=bulk_open(temporary,"wb");if(!file)return PORTABLE_BUNDLE_IO;
    unsigned char block[TRANSFER_BYTES];uint32_t remaining=entry->bytes,crc=0xffffffffu;int failed=0;
    progress_begin(progress,"Extracting",names[index],entry->bytes);
    while(remaining) {
        size_t size=remaining<sizeof block ? remaining:sizeof block;
        if(fread(block,1,size,source)!=size || fwrite(block,1,size,file)!=size){failed=1;break;}
        crc=crc_update(crc,block,size);remaining-=(uint32_t)size;
        progress_step(progress,entry->bytes-remaining,entry->bytes);
    }
    if((crc^0xffffffffu)!=entry->crc || fflush(file))failed=1;
    if(fclose(file))failed=1;
    if(!failed && !cached(temporary,entry,progress,"Verifying extracted",names[index]))failed=1;
    if(!failed && rename(temporary,path)) {
        /* PSP FAT cannot replace a file. Only a known-invalid cache entry is
           removed here; an interruption is repaired from EBOOT next boot. */
        if((remove(path) && errno!=ENOENT) || rename(temporary,path))failed=1;
    }
    if(failed){remove(temporary);return PORTABLE_BUNDLE_IO;}
    result->written++;return 0;
}
static int bundle_root(const char *application_root,char *root,size_t capacity)
{
    ready=0;entry_count=0;cache[0]=executable[0]=0;
    if(application_root) {
        size_t n=strlen(application_root);
        if(!n || n>=capacity)return PORTABLE_BUNDLE_PATH;
        memcpy(root,application_root,n+1);
    } else if(!getcwd(root,capacity))return PORTABLE_BUNDLE_PATH;
    size_t n=strlen(root);while(n>1 && root[n-1]=='/')root[--n]=0;
    if(!(root[0]=='/' || !strncmp(root,"ms0:/",5) || !strncmp(root,"ef0:/",5) ||
         !strcmp(root,"ms0:") || !strcmp(root,"ef0:") || !strncmp(root,"umd0:/",6) || !strcmp(root,"umd0:")) ||
       join(executable,sizeof executable,root,"EBOOT.PBP")) {
        return PORTABLE_BUNDLE_PATH;
    }
    return 0;
}
static int cache_path(const char *root,uint32_t index_crc)
{
    char leaf[80];snprintf(leaf,sizeof leaf,"data/cache/modules/%08x",(unsigned)index_crc);
    return join(cache,sizeof cache,root,leaf);
}
int portable_bundle_try_cached(const char *application_root,PortableBundleResult *result)
{
    PortableBundleResult local={0};char root[PORTABLE_BUNDLE_PATH_MAX],path[PORTABLE_BUNDLE_PATH_MAX];
    BundleIndex index;
    if(!result)result=&local;
    memset(result,0,sizeof *result);
    int status=bundle_root(application_root,root,sizeof root);
    if(status){result->status=status;return status;}
    FILE *file=bulk_open(executable,"rb");
    if(!file){result->status=PORTABLE_BUNDLE_IO;return result->status;}
    uint32_t index_crc=0;status=bundle_index(file,&index_crc,&index);
    if(fclose(file) && !status)status=PORTABLE_BUNDLE_IO;
    result->index_crc=(unsigned)index_crc;
    if(!status)status=cache_path(root,index_crc);
    const char *directories[]={"data","data/cache","data/cache/modules"};
    for(unsigned i=0;i<3 && !status;i++) {
        status=join(path,sizeof path,root,directories[i]);
        if(!status)status=existing_directory(path);
    }
    if(!status)status=existing_directory(cache);
    if(!status && !receipt_matches(&index))status=PORTABLE_BUNDLE_MISSING;
    for(unsigned i=0;i<entry_count && !status;i++) {
        struct stat info;status=join(path,sizeof path,cache,names[i]);
        if(!status && (!regular(path,&info) || info.st_size!=(long)entries[i].bytes))
            status=PORTABLE_BUNDLE_MISSING;
    }
    if(!status){ready=1;result->reused=result->entries=entry_count;}
    else cache[0]=executable[0]=0;
    result->status=status;return status;
}
int portable_bundle_prepare_progress(const char *application_root,PortableBundleResult *result,
                                     PortableBundleProgress callback,void *user)
{
    PortableBundleResult local={0};char root[PORTABLE_BUNDLE_PATH_MAX],path[PORTABLE_BUNDLE_PATH_MAX];
    BundleProgress progress={callback,user,{0}};BundleIndex index;
    if(!result)result=&local;
    memset(result,0,sizeof *result);
    int status=bundle_root(application_root,root,sizeof root);
    if(status){result->status=status;return status;}
    progress_begin(&progress,"Reading application bundle",NULL,0);
    FILE *file=bulk_open(executable,"rb");
    if(!file){result->status=PORTABLE_BUNDLE_IO;return result->status;}
    uint32_t index_crc=0;
    status=bundle_index(file,&index_crc,&index);
    result->index_crc=(unsigned)index_crc;
    if(!status)status=cache_path(root,index_crc);
    if(!status)status=bundle_verify(file,&progress);
    if(!status) {
        const char *directories[]={"data","data/cache","data/cache/modules"};
        for(unsigned i=0;i<3 && !status;i++) {
            status=join(path,sizeof path,root,directories[i]);
            if(!status)status=directory(path);
        }
        if(!status) {
            status=directory(cache);
        }
        if(!status)status=receipt_invalidate();
        for(unsigned i=0;i<entry_count && !status;i++)status=extract(file,i,result,&progress);
    }
    if(fclose(file) && !status)status=PORTABLE_BUNDLE_IO;
    if(!status)status=receipt_publish(&index);
    if(!status){ready=1;result->entries=entry_count;}
    else cache[0]=executable[0]=0;
    result->status=status;return status;
}
int portable_bundle_prepare(const char *application_root,PortableBundleResult *result)
{return portable_bundle_prepare_progress(application_root,result,NULL,NULL);}
int portable_bundle_path(unsigned role,char *out,size_t capacity)
{
    if(!out || !capacity || !ready || role<1 || role>entry_count)return PORTABLE_BUNDLE_MISSING;
    return join(out,capacity,cache,names[role-1]);
}
int portable_bundle_open_entry(unsigned role,PortableBundleStream *stream)
{
    if(!stream)return PORTABLE_BUNDLE_IO;
    memset(stream,0,sizeof *stream);
    if(!ready || role<1 || role>entry_count)return PORTABLE_BUNDLE_MISSING;
    BundleEntry *entry=&entries[role-1];FILE *file=bulk_open(executable,"rb");
    if(!file)return PORTABLE_BUNDLE_IO;
    if(fseek(file,(long)(psar_offset+entry->offset),SEEK_SET)){fclose(file);return PORTABLE_BUNDLE_IO;}
    stream->file=file;stream->remaining=entry->bytes;stream->crc=0xffffffffu;stream->expected_crc=entry->crc;
    return 0;
}
size_t portable_bundle_read(PortableBundleStream *stream,void *out,size_t bytes)
{
    if(!stream || !stream->file || stream->error)return 0;
    if(bytes>stream->remaining)bytes=stream->remaining;
    if(!bytes)return 0;
    if(!out){stream->error=PORTABLE_BUNDLE_IO;return 0;}
    size_t got=fread(out,1,bytes,stream->file);
    stream->crc=crc_update(stream->crc,out,got);stream->remaining-=(uint32_t)got;
    if(got!=bytes)stream->error=PORTABLE_BUNDLE_IO;
    return got;
}
int portable_bundle_close(PortableBundleStream *stream)
{
    if(!stream || !stream->file)return PORTABLE_BUNDLE_IO;
    int status=stream->error;
    if(fclose(stream->file) && !status)status=PORTABLE_BUNDLE_IO;
    if(!status && stream->remaining)status=PORTABLE_BUNDLE_IO;
    if(!status && (stream->crc^0xffffffffu)!=stream->expected_crc)status=PORTABLE_BUNDLE_CHECKSUM;
    memset(stream,0,sizeof *stream);return status;
}
const char *portable_bundle_error(int status)
{
    switch(status) {
    case PORTABLE_BUNDLE_OK:return "Runtime helpers ready";
    case PORTABLE_BUNDLE_FORMAT:return "EBOOT runtime bundle is invalid";
    case PORTABLE_BUNDLE_CHECKSUM:return "EBOOT runtime bundle failed verification";
    case PORTABLE_BUNDLE_PATH:return "Runtime helper cache path is invalid";
    case PORTABLE_BUNDLE_MISSING:return "Runtime bundle entry is unavailable";
    default:return "Could not prepare runtime helpers on the Memory Stick";
    }
}
