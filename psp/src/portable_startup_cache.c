/* A sequential startup cache assembled only from the user's installed data.
 * The executable contains file names, sizes and integrity metadata, no payload. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef remove
#undef rename
#include "portable_startup_cache.h"
#ifndef JGR_PORTABLE_IDENTITY_HEADER
#define JGR_PORTABLE_IDENTITY_HEADER "portable_asset_identity.h"
#endif
#include JGR_PORTABLE_IDENTITY_HEADER
#include "portable_crc32.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

typedef struct {const char *name;unsigned bytes;} StartupEntry;
static const StartupEntry entries[]={
#include "source_data_startup_registry.inc"
};
enum {HEADER_BYTES=48,MAXIMUM_BYTES=1024*1024};
#define ENTRY_COUNT ((unsigned)(sizeof entries/sizeof *entries))
#define CACHE_NAME "data/cache/startup-tables.bin"
static unsigned word(const unsigned char *p)
{return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static void put(unsigned char *p,unsigned n)
{p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);p[2]=(unsigned char)(n>>16);p[3]=(unsigned char)(n>>24);}
static unsigned checksum(const void *p,size_t n)
{return jgr_crc32_add(0xffffffffu,p,n)^0xffffffffu;}
static size_t image_size(void)
{
    size_t bytes=HEADER_BYTES;
    for(unsigned i=0;i<ENTRY_COUNT;i++){
        if(!entries[i].bytes || entries[i].bytes>MAXIMUM_BYTES-bytes)return 0;
        bytes+=entries[i].bytes;
    }
    return bytes;
}
static unsigned registry_crc(void)
{
    unsigned crc=0xffffffffu;unsigned char size[4];
    for(unsigned i=0;i<ENTRY_COUNT;i++){
        crc=jgr_crc32_add(crc,entries[i].name,strlen(entries[i].name)+1);
        put(size,entries[i].bytes);crc=jgr_crc32_add(crc,size,sizeof size);
    }
    return crc^0xffffffffu;
}
static int path(char out[1024],const char *root,const char *name)
{
    if(!root || !*root || !name)return -1;
    int n=snprintf(out,1024,"%s%s%s",root,root[strlen(root)-1]=='/'?"":"/",name);
    return n<0 || n>=1024 ? -1:0;
}
static FILE *open_bulk(const char *name,const char *mode)
{
    FILE *f=fopen(name,mode);
    if(f && setvbuf(f,NULL,_IONBF,0)){fclose(f);return NULL;}
    return f;
}
static int valid(const PortableStartupCache *cache)
{
    const unsigned char *p=cache->image;size_t bytes=image_size();
    return bytes && cache->bytes==bytes && p && !memcmp(p,"JGRBOOT1",8) &&
        word(p+8)==1 && word(p+12)==bytes && word(p+16)==ENTRY_COUNT &&
        word(p+20)==JGR_ASSET_MANIFEST_CRC32 && word(p+24)==JGR_ASSET_LAYOUT_VERSION &&
        word(p+28)==JGR_ASSET_FILE_COUNT && word(p+32)==registry_crc() &&
        word(p+40)==checksum(p,40) && !word(p+44) &&
        word(p+36)==checksum(p+HEADER_BYTES,bytes-HEADER_BYTES);
}
void portable_startup_cache_free(PortableStartupCache *cache)
{if(cache){free(cache->image);cache->image=NULL;cache->bytes=0;}}
int portable_startup_cache_read(PortableStartupCache *cache,const char *root)
{
    char name[1024];size_t bytes=image_size();
    if(!cache || cache->image || !bytes || path(name,root,CACHE_NAME))return -1;
    FILE *f=open_bulk(name,"rb");if(!f)return -1;
    unsigned char *image=malloc(bytes+1);if(!image){fclose(f);return -1;}
    /* One sequential read, including a byte beyond the expected end, rejects
     * both truncated and appended files without a stat or seek. */
    size_t got=fread(image,1,bytes+1,f);int bad=got!=bytes || ferror(f);
    if(fclose(f))bad=1;
    PortableStartupCache candidate={image,bytes};
    if(bad || !valid(&candidate)){portable_startup_cache_free(&candidate);return -1;}
    *cache=candidate;return 0;
}
int portable_startup_cache_collect(PortableStartupCache *cache,const char *root)
{
    size_t bytes=image_size();char name[1024],relative[192];
    if(!cache || cache->image || !bytes)return -1;
    PortableStartupCache candidate={malloc(bytes+1),bytes};if(!candidate.image)return -1;
    memset(candidate.image,0,HEADER_BYTES);size_t offset=HEADER_BYTES;
    for(unsigned i=0;i<ENTRY_COUNT;i++){
        int n=snprintf(relative,sizeof relative,"data/assets/%s",entries[i].name);
        if(n<0 || (size_t)n>=sizeof relative || path(name,root,relative))goto failed;
        FILE *f=open_bulk(name,"rb");if(!f)goto failed;
        /* The spare byte overlaps the next unread entry, or the allocation's
         * final spare byte. No temporary per-file allocation is required. */
        size_t got=fread(candidate.image+offset,1,(size_t)entries[i].bytes+1,f);
        int bad=got!=entries[i].bytes || ferror(f);if(fclose(f))bad=1;
        if(bad)goto failed;
        offset+=entries[i].bytes;
    }
    unsigned char *p=candidate.image;memcpy(p,"JGRBOOT1",8);
    put(p+8,1);put(p+12,(unsigned)bytes);put(p+16,ENTRY_COUNT);
    put(p+20,JGR_ASSET_MANIFEST_CRC32);put(p+24,JGR_ASSET_LAYOUT_VERSION);
    put(p+28,JGR_ASSET_FILE_COUNT);put(p+32,registry_crc());
    put(p+36,checksum(p+HEADER_BYTES,bytes-HEADER_BYTES));put(p+40,checksum(p,40));
    *cache=candidate;return 0;
failed:portable_startup_cache_free(&candidate);return -1;
}
int portable_startup_cache_write(const PortableStartupCache *cache,const char *root)
{
    char name[1024],temporary[1024];
    if(!cache || !valid(cache) || path(name,root,CACHE_NAME) || path(temporary,root,CACHE_NAME ".tmp"))return -1;
    FILE *f=open_bulk(temporary,"wb");if(!f)return -1;
    int bad=fwrite(cache->image,1,cache->bytes,f)!=cache->bytes;
    if(fflush(f))bad=1;if(fclose(f))bad=1;
    /* Only a fully closed image is published. PSP FAT cannot replace an
     * existing file: a power loss during replacement is a cache miss, never
     * a partial cache hit. The original installed tables remain untouched. */
    if(!bad && rename(temporary,name)){
        if((remove(name) && errno!=ENOENT) || rename(temporary,name))bad=1;
    }
    if(bad){remove(temporary);return -1;}return 0;
}
int portable_startup_cache_view(void *user,const char *name,const void **data,size_t *bytes)
{
    const PortableStartupCache *cache=user;size_t offset=HEADER_BYTES;
    if(!cache || !cache->image || !name || !data || !bytes)return -1;
    for(unsigned i=0;i<ENTRY_COUNT;i++){
        if(!strcmp(name,entries[i].name)){
            if(offset>cache->bytes || entries[i].bytes>cache->bytes-offset)return -1;
            *data=cache->image+offset;*bytes=entries[i].bytes;return 1;
        }
        offset+=entries[i].bytes;
    }
    return -1;
}
int portable_startup_cache_prepare(const char *root)
{
    PortableStartupCache cache={0};
    int result=portable_startup_cache_collect(&cache,root);
    if(!result)result=portable_startup_cache_write(&cache,root);
    portable_startup_cache_free(&cache);return result;
}
