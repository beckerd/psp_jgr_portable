/* XMB presentation for our unencrypted story files.
   PSF/PBP format reference: https://uofw.github.io/upspd/docs/hardware/psp_doc.pdf
   ICON0 is read from the user's packaged EBOOT, never embedded in game code. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "savedata.h"
#include "portable_assets.h"
#include <stdint.h>
#include <string.h>
#include <errno.h>
#ifdef __PSP__
#include <pspchnnlsv.h>
#include <pspmodulemgr.h>
#include <pspkerror.h>
#endif
/* Release builds force-include the asset router before this file's guard. */
#undef fopen
#undef stat
#undef remove
#undef rename

static uint32_t save_word(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void save_put(unsigned char *p,uint32_t value,unsigned bytes)
{for(unsigned i=0;i<bytes;i++)p[i]=(unsigned char)(value>>(8*i));}

#ifdef __PSP__
static int save_hash_service;
static int save_hash_init(void)
{
    if(save_hash_service)return 0;
    /* The savedata service is not resident in every game session. Its weak
       imports bind when the firmware module loads; no firmware is bundled. */
    SceKernelLMOption option={.size=sizeof option,
        .mpidtext=PSP_MEMORY_PARTITION_KERNEL,.mpiddata=PSP_MEMORY_PARTITION_KERNEL,.access=1};
    int module=sceKernelLoadModule("flash0:/kd/chnnlsv.prx",0,&option);
    if(module>=0) {
        int status=-1;
        int result=sceKernelStartModule(module,0,NULL,&status,NULL);
        if(result<0 || status<0){errno=EIO;return -1;}
    } else if(module!=(int)SCE_KERNEL_ERROR_EXCLUSIVE_LOAD){errno=EIO;return -1;}
    save_hash_service=1;
    return 0;
}
#endif

/* Integrity-hash sequence adapted from PSPSDK's savedata/encrypt/hash.c.
   Copyright (c) 2005 Jim Paris <jim@jtan.com>
   Copyright (c) 2005 psp123
   BSD-3-Clause; see tools/portable/notices/PSPSDK-savedata.txt. */
static int save_sfo_hash(unsigned char *data,unsigned bytes,int mode,unsigned char hash[16])
{
#ifdef __PSP__
    if(save_hash_init())return -1;
    SceSdContext1 context __attribute__((aligned(16)));
    memset(&context,0,sizeof context);
    memset(hash,0,16);
    if(sceSdSetIndex(&context,mode)<0 || context.mode!=mode ||
       sceSdRemoveValue(&context,data,(int)((bytes+15)&~15u))<0 ||
       context.size!=16 ||
       sceSdGetLastIndex(&context,hash,NULL)<0){errno=EIO;return -1;}
    /* An unresolved import can return without producing output. Never
       publish an SFO with an empty integrity hash as a successful update. */
    unsigned nonzero=0;
    for(unsigned i=0;i<16;i++)nonzero|=hash[i];
    if(!nonzero){errno=EIO;return -1;}
    return 0;
#else
    (void)data;(void)bytes;(void)mode;(void)hash;errno=ENOSYS;return -1;
#endif
}
static unsigned save_sfo(unsigned char out[5120])
{
    /* Fixed PSP save fields, alphabetical keys and four-byte value alignment.
       The secure-file list stays empty: our story files are unencrypted.
       The SFO itself still needs integrity hashes for the XMB. */
    static const struct {const char *key,*text;unsigned format,capacity;} fields[]={
        {"CATEGORY","MS",0x0204,4},
        {"PARENTAL_LEVEL",NULL,0x0404,4},
        {"SAVEDATA_DETAIL","Story progress and settings.",0x0204,1024},
        {"SAVEDATA_DIRECTORY","JGR_PORTABLE",0x0204,64},
        {"SAVEDATA_FILE_LIST",NULL,0x0004,3168},
        {"SAVEDATA_PARAMS",NULL,0x0004,128},
        {"SAVEDATA_TITLE","Story progress",0x0204,128},
        {"TITLE","JGR Portable",0x0204,128}
    };
    const unsigned count=sizeof fields/sizeof *fields,key_base=20+16*count;
    unsigned key_end=key_base;
    for(unsigned i=0;i<count;i++)key_end+=(unsigned)strlen(fields[i].key)+1;
    unsigned value_base=(key_end+3)&~3u,key=key_base,value=value_base,params=0;
    memset(out,0,5120);memcpy(out,"\0PSF",4);
    save_put(out+4,0x101,4);save_put(out+8,key_base,4);
    save_put(out+12,value_base,4);save_put(out+16,count,4);
    for(unsigned i=0;i<count;i++) {
        unsigned char *entry=out+20+16*i;
        unsigned length=fields[i].text ? (unsigned)strlen(fields[i].text)+1:fields[i].capacity;
        unsigned key_length=(unsigned)strlen(fields[i].key)+1;
        if(value+fields[i].capacity>5120 || length>fields[i].capacity)return 0;
        save_put(entry,key-key_base,2);save_put(entry+2,fields[i].format,2);
        save_put(entry+4,length,4);save_put(entry+8,fields[i].capacity,4);
        save_put(entry+12,value-value_base,4);
        memcpy(out+key,fields[i].key,key_length);
        if(fields[i].text)memcpy(out+value,fields[i].text,length);
        if(!strcmp(fields[i].key,"SAVEDATA_PARAMS"))params=value;
        key+=key_length;value+=fields[i].capacity;
    }
    unsigned char hash[16] __attribute__((aligned(16)));
    if(!params || save_sfo_hash(out,value,2,hash))return 0;
    memcpy(out+params+0x20,hash,16);out[params]=1;
    if(save_sfo_hash(out,value,1,hash))return 0;
    memcpy(out+params+0x10,hash,16);
    return value;
}

static int save_metadata_matches(const char *path,FILE *source,long offset,
                                  const unsigned char *data,unsigned bytes)
{
    if(source && fseek(source,offset,SEEK_SET))return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    unsigned char expected[512],actual[512];unsigned position=0;int equal=1;
    while(position<bytes && equal) {
        unsigned n=bytes-position;if(n>sizeof actual)n=sizeof actual;
        const unsigned char *wanted=data ? data+position:expected;
        if(source && fread(expected,1,n,source)!=n){equal=0;break;}
        if(fread(actual,1,n,f)!=n || memcmp(actual,wanted,n))equal=0;
        position+=n;
    }
    if(fgetc(f)!=EOF || ferror(f))equal=0;
    if(fclose(f))equal=0;
    return equal;
}
static int save_metadata_publish(const char *save_root,const char *name,FILE *source,long offset,
                                 const unsigned char *data,unsigned bytes,int force)
{
    char path[JGR_PORTABLE_PATH_MAX],temporary[JGR_PORTABLE_PATH_MAX];
    int n=snprintf(path,sizeof path,"%s/%s",save_root,name);
    if(n<0 || (unsigned)n+5>=sizeof path){errno=ENAMETOOLONG;return -1;}
    memcpy(temporary,path,(size_t)n);memcpy(temporary+n,".tmp",5);
    if(!force && save_metadata_matches(path,source,offset,data,bytes))return 0;
    if(source && fseek(source,offset,SEEK_SET))return -1;
    FILE *f=fopen(temporary,"wb");if(!f)return -1;
    unsigned char buffer[512];unsigned position=0;int bad=0;
    while(position<bytes) {
        unsigned count=bytes-position;if(count>sizeof buffer)count=sizeof buffer;
        const unsigned char *chunk=data ? data+position:buffer;
        if(source && fread(buffer,1,count,source)!=count){bad=1;break;}
        if(fwrite(chunk,1,count,f)!=count){bad=1;break;}
        position+=count;
    }
    if(fflush(f))bad=1;
    if(fclose(f))bad=1;
    if(!bad && !save_metadata_matches(temporary,source,offset,data,bytes))bad=1;
    /* PSP rename cannot replace an existing file. Only presentation files
       reach this helper; interrupted promotion is repaired on the next load.
       Verify the temporary before removing the previous presentation. */
    if(!bad && remove(path) && errno!=ENOENT)bad=1;
    if(!bad && rename(temporary,path))bad=1;
    if(bad){int code=errno;remove(temporary);errno=code ? code:EIO;return -1;}
    return 0;
}
static int save_packaged_icon(const char *save_root,const char *executable)
{
    unsigned char header[40],png[33];
    FILE *f=fopen(executable,"rb");if(!f)return -1;
    int result=-1;long end;
    if(fseek(f,0,SEEK_END) || (end=ftell(f))<40 || fseek(f,0,SEEK_SET) ||
       fread(header,1,sizeof header,f)!=sizeof header || memcmp(header,"\0PBP",4) ||
       save_word(header+8)!=40)goto done;
    uint32_t previous=40;
    for(unsigned i=0;i<8;i++) {
        uint32_t at=save_word(header+8+i*4);
        if(at<previous || at>(uint32_t)end)goto done;
        previous=at;
    }
    uint32_t start=save_word(header+12),bytes=save_word(header+16)-start;
    if(!bytes){result=0;goto done;} /* Artwork-free development EBOOT. */
    if(bytes<sizeof png || bytes>128u*1024u || fseek(f,(long)start,SEEK_SET) ||
       fread(png,1,sizeof png,f)!=sizeof png || memcmp(png,"\x89PNG\r\n\x1a\n",8) ||
       memcmp(png+8,"\0\0\0\x0dIHDR\0\0\0\x90\0\0\0\x50",16))goto done;
    result=save_metadata_publish(save_root,"ICON0.PNG",f,(long)start,NULL,bytes,0);
done:
    if(fclose(f))result=-1;
    if(result)errno=EIO;
    return result;
}
int savedata_update(const char *save_root,const char *executable,int saved)
{
    struct stat info;
    if(!save_root || !*save_root || !executable || !*executable){errno=EINVAL;return -1;}
    /* Never create an empty save entry just by starting a new game. */
    if(stat(save_root,&info) || !S_ISDIR(info.st_mode))return -1;
    unsigned char sfo[5120] __attribute__((aligned(16)));
    unsigned bytes=save_sfo(sfo);
    if(!bytes)return -1;
    if(save_metadata_publish(save_root,"PARAM.SFO",NULL,0,sfo,bytes,saved))return -1;
    return save_packaged_icon(save_root,executable);
}
