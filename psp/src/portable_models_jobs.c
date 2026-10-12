/* Payload-free JGC recipe execution during first-launch setup. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#include "portable_models_jobs.h"
#include "portable_models_colors.h"
#include "portable_models_pack.h"
#include "portable_convert.h"
#include "portable_indexed.h"
#include "portable_crc32.h"
#if JGR_SETUP_ME
#include "portable_texture_job.h"
#include "portable_setup_me.h"
#endif
#include "portable_scratch.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __PSP__
#include <pspkernel.h>
#else
#include <sys/time.h>
#endif

#ifndef JGR_MODELS_STATS
#define JGR_MODELS_STATS JGR_DEVICE_SETUP
#endif
#define MODEL_STATISTICS(...) do { if(JGR_MODELS_STATS) { __VA_ARGS__; } } while(0)
static JgrModelsStats model_statistics;
static uint64_t model_clock(void)
{
#if !JGR_MODELS_STATS
    return 0;
#elif defined(__PSP__)
    return sceKernelGetSystemTimeWide();
#else
    struct timeval value;gettimeofday(&value,NULL);
    return (uint64_t)value.tv_sec*1000000u+(unsigned)value.tv_usec;
#endif
}
void jgr_models_stats(JgrModelsStats *out){if(out)*out=model_statistics;}

#define ROW_BYTES 688u
#define PARAM_AT 608u
#define MAX_TEXTURES 8192u
#define MAX_BANKS 256u
#define MAX_BLOCK (4u*1024u*1024u)
#ifndef JGR_MODEL_SOURCE_PRELOAD
#define JGR_MODEL_SOURCE_PRELOAD 1
#endif
/* Larger optional blocks fragment the PSP-1000 heap enough to spill later
 * texture output even after the source is released. Keep those streaming. */
#define MODEL_SOURCE_LIMIT (4u*1024u*1024u)
#define MODEL_SOURCE_HEADROOM (8u*1024u*1024u)
typedef struct {JgrDiscView view;JgrDiscFile attempted;unsigned char *data;JgrScratchReclaim reclaim;} ModelSource;
static int model_source_suppressed,model_source_used;
static void model_source_release(ModelSource *source)
{
    (void)jgr_scratch_reclaim_end(&source->reclaim);
    (void)jgr_disc_view_end(&source->view);free(source->data);source->data=NULL;
}
#if JGR_MODEL_SOURCE_PRELOAD
static void model_source_reclaim(void *source){model_source_release(source);}
#endif
static int model_source_prepare(ModelSource *source,JgrScratchPool *scratch,JgrDisc *disc,const JgrDiscFile *file,unsigned rows)
{
#if JGR_MODEL_SOURCE_PRELOAD
    if(source->attempted.offset==file->offset && source->attempted.size==file->size){
        /* Optional residency must yield as an expanding geometry job reaches
         * its workspace reserve. Do not reload this same extent afterward. */
        if(source->data && jgr_scratch_heap_available()<MODEL_SOURCE_HEADROOM)model_source_release(source);
        return 0;
    }
    model_source_release(source);source->attempted=*file;
    if(model_source_suppressed || !rows || file->size<4096 || file->size>MODEL_SOURCE_LIMIT ||
       (rows<2 && file->size>1024u*1024u))return 0;
    size_t slack=JGR_DISC_SEQUENTIAL_CAPACITY-JGR_DISC_SEQUENTIAL_BYTES;
    size_t capacity=(size_t)file->size+slack;
    if(jgr_scratch_heap_available()<MODEL_SOURCE_HEADROOM+capacity)return 0;
    int saved=errno;source->data=malloc(capacity);errno=saved;
    if(!source->data)return 0;
    for(uint32_t at=0;at<file->size;){
        unsigned n=file->size-at;if(n>JGR_DISC_SEQUENTIAL_BYTES)n=JGR_DISC_SEQUENTIAL_BYTES;
        if(jgr_disc_read_sequential(disc,file,at,source->data+at,n,capacity-at)){model_source_release(source);return -1;}
        at+=n;
    }
    if(jgr_disc_view_begin(disc,&source->view,file,source->data,file->size)){model_source_release(source);return -1;}
    if(jgr_scratch_reclaim_begin(scratch,&source->reclaim,model_source_reclaim,source)){model_source_release(source);return -1;}
    model_source_used=1;
#else
    (void)source;(void)scratch;(void)disc;(void)file;(void)rows;
#endif
    return 0;
}

/* Count source reuse while validating the already-read recipe. Hashes affect
 * optional admission only; all actual views still require an exact extent.
 * Overflow leaves an unknown source streaming. Stage and executable images
 * contain sparse prop/sky geometry, so their full images are not preloaded. */
typedef struct {uint64_t key[64];unsigned rows[64],count;} ModelSourceUsage;
static uint64_t model_source_key(const unsigned char *path)
{uint64_t key=UINT64_C(14695981039346656037);for(unsigned i=0;i<192 && path[i];i++){key^=path[i];key*=UINT64_C(1099511628211);}return key;}
static void model_source_count(ModelSourceUsage *usage,const unsigned char *path)
{
    if(!usage)return;
    uint64_t key=model_source_key(path);unsigned i;
    for(i=0;i<usage->count;i++)if(usage->key[i]==key)break;
    if(i==64)return;
    if(i==usage->count){usage->key[i]=key;usage->rows[i]=0;usage->count++;}usage->rows[i]++;
}
static unsigned model_source_rows(const ModelSourceUsage *usage,const unsigned char *path,unsigned raw,unsigned flags)
{
    /* Lighting-only jobs and isolated raw subtrees read a small part of the
     * source. Full face-color exports explicitly traverse their whole mesh. */
    if(flags&1024u)return 0;
    if(!strncmp((const char*)path,"decompile/afs/STAGE",19) ||
       !strncmp((const char*)path,"decompile/afs/U_GARAGE/",23) ||
       !strncmp((const char*)path,"decompile/extracted/",20))return 0;
    uint64_t key=model_source_key(path);
    for(unsigned i=0;i<usage->count;i++)if(usage->key[i]==key){
        unsigned rows=usage->rows[i];return raw && rows<2 && !(flags&2048u)?0:rows;
    }
    return 0;
}

/* Setup executes one converter at a time. Retain the failing operation before
 * closing temporary streams, which can otherwise overwrite errno. */
static char model_error[192];
static const char *model_stage;
static unsigned model_item;
static int model_failure_error;
const char *jgr_models_error(void){return model_error;}
static void model_step(const char *stage,unsigned item)
{model_stage=stage;model_item=item;errno=0;}
static void model_failure(int error)
{model_failure_error=error;snprintf(model_error,sizeof model_error,"Model conversion: %s (item %u, errno %d)",model_stage?model_stage:"unknown",model_item,error);}

typedef struct {
    JgrDiscFile source;
    char path[192];
    uint32_t *offsets;
    unsigned first,count,cap,offset_mode,stage_list,member;
} TextureBank;
typedef struct PreparedTexture PreparedTexture;
typedef struct {
    JgrJgcBuild *build;PreparedTexture *prepared;
    JgrDisc *disc;TextureBank *banks;unsigned count,total,white,sector_tail,legacy;
    uint16_t *caps;unsigned char *used_caps;
    unsigned stage;JgrDiscFile exe;unsigned char *code;
} Textures;
static uint32_t word(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned half(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
/* These bytes are regenerated from the current open disc. Destination stream
 * offsets are deliberately absent: a hit must relocate its palette/pixels. */
#define TEXTURE_CACHE_BYTES (512u*1024u)
#define TEXTURE_CACHE_ENTRIES 64u
#define TEXTURE_CACHE_HEADROOM (6u*1024u*1024u)
typedef struct {
    uint64_t source_offset;
    uint32_t source_size,payload_at,payload_size;
    unsigned source_width,source_height,width,height,cap,white,format,type;
} TextureKey;
typedef struct {
    TextureKey key;
    unsigned char *data;
    unsigned bytes,flags;
    uint64_t age;
} CachedTexture;
static struct {
    JgrDisc *disc;
    CachedTexture entries[TEXTURE_CACHE_ENTRIES];
    size_t bytes;
    uint64_t clock;
} texture_cache;
static void texture_disk_end(void);
static void texture_cache_drop(unsigned i)
{
    CachedTexture *e=texture_cache.entries+i;
    texture_cache.bytes-=e->bytes;free(e->data);memset(e,0,sizeof *e);
}
void jgr_models_cache_clear(void)
{for(unsigned i=0;i<TEXTURE_CACHE_ENTRIES;i++)texture_cache_drop(i);texture_cache.clock=0;}
void jgr_models_cache_end(void)
{jgr_models_cache_clear();texture_disk_end();texture_cache.disc=NULL;jgr_indexed_cache_end();}
void jgr_models_cache_begin(JgrDisc *disc)
{jgr_models_cache_end();texture_cache.disc=disc;jgr_indexed_cache_begin();}
static int texture_cache_evict(void)
{
    unsigned oldest=TEXTURE_CACHE_ENTRIES;
    for(unsigned i=0;i<TEXTURE_CACHE_ENTRIES;i++)if(texture_cache.entries[i].data &&
       (oldest==TEXTURE_CACHE_ENTRIES || texture_cache.entries[i].age<texture_cache.entries[oldest].age))oldest=i;
    if(oldest==TEXTURE_CACHE_ENTRIES)return 0;
    texture_cache_drop(oldest);return 1;
}
static void texture_cache_trim(void)
{while(jgr_scratch_heap_available()<TEXTURE_CACHE_HEADROOM && texture_cache_evict()){} }
static CachedTexture *texture_cache_find(JgrDisc *disc,const TextureKey *key)
{
    if(!disc || texture_cache.disc!=disc)return NULL;
    for(unsigned i=0;i<TEXTURE_CACHE_ENTRIES;i++){
        CachedTexture *e=texture_cache.entries+i;
        if(e->data && !memcmp(&e->key,key,sizeof *key)){e->age=++texture_cache.clock;return e;}
    }return NULL;
}
static unsigned char *texture_cache_allocate(JgrDisc *disc,unsigned bytes)
{
    if(!disc || texture_cache.disc!=disc || bytes>TEXTURE_CACHE_BYTES)return NULL;
    while(texture_cache.bytes>TEXTURE_CACHE_BYTES-bytes ||
          jgr_scratch_heap_available()<TEXTURE_CACHE_HEADROOM+bytes){
        if(!texture_cache_evict())return NULL;
    }
    /* Keep the ordinary decoder usable if this optional allocation fails. */
    int saved=errno;unsigned char *data=malloc(bytes);errno=saved;return data;
}
static void texture_cache_store(const TextureKey *key,unsigned char *data,unsigned bytes,unsigned flags)
{
    unsigned slot=0;
    while(slot<TEXTURE_CACHE_ENTRIES && texture_cache.entries[slot].data)slot++;
    if(slot==TEXTURE_CACHE_ENTRIES){texture_cache_evict();slot=0;while(texture_cache.entries[slot].data)slot++;}
    CachedTexture *e=texture_cache.entries+slot;e->key=*key;e->data=data;e->bytes=bytes;e->flags=flags;e->age=++texture_cache.clock;
    texture_cache.bytes+=bytes;
}
static int texture_cache_write(FILE *out,JgrJgcTexture *record,const TextureKey *key,const unsigned char *data,unsigned flags)
{
    long at=ftell(out);if(at<0 || (unsigned long)at>0x7fffff00UL)return -1;
    while(at&15){if(fputc(0,out)==EOF)return -1;at++;}
    record->width=key->width;record->height=key->height;record->flags=flags;
    record->clut=(uint32_t)at;record->pixels=(uint32_t)at+1024;
    unsigned bytes=1024+key->width*key->height;
    return fwrite(data,1,bytes,out)==bytes?0:-1;
}
/* Texture archives duplicate many identical PVR payloads at different disc
 * offsets. Keep their encoded results once per setup invocation. Hashes only
 * select candidates: reuse also requires byte-for-byte original payload
 * equality and a checksum of the temporary encoded bytes being read. */
#define DISK_TEXTURE_ENTRIES 2048u
#define DISK_TEXTURE_SLOTS 4096u
#define DISK_TEXTURE_BUFFER (64u*1024u)
typedef struct {
    TextureKey key;
    uint32_t raw_crc,encoded_crc,offset,bytes,flags;
} DiskTexture;
static struct {
    FILE *file;
    DiskTexture *entries;
    uint16_t *slots;
    unsigned char *buffer;
    uint32_t bytes,count;
    int attempted,owned,append;
    char path[1024];
} texture_disk;
static void texture_disk_disable(void)
{
    int saved=errno;
    if(texture_disk.file)fclose(texture_disk.file);
    if(texture_disk.owned)remove(texture_disk.path);
    free(texture_disk.entries);free(texture_disk.slots);free(texture_disk.buffer);
    memset(&texture_disk,0,sizeof texture_disk);texture_disk.attempted=1;errno=saved;
}
static void texture_disk_end(void)
{texture_disk_disable();texture_disk.attempted=0;}
static int texture_info(const char *path,struct stat *st)
{
#ifdef __PSP__
    return stat(path,st);
#else
    return lstat(path,st);
#endif
}
static void texture_disk_open(JgrDisc *disc,const char *directory)
{
    if(texture_disk.attempted || texture_cache.disc!=disc)return;
    int saved=errno;struct stat st;texture_disk.attempted=1;
    int n=snprintf(texture_disk.path,sizeof texture_disk.path,"%s/%s",directory,JGR_MODEL_TEXTURE_CACHE_FILE);
    if(n<0 || (size_t)n>=sizeof texture_disk.path || texture_info(directory,&st) || !S_ISDIR(st.st_mode))goto fail;
    int exists=texture_info(texture_disk.path,&st);
    if((!exists && !S_ISREG(st.st_mode)) || (exists && errno!=ENOENT))goto fail;
    size_t budget=DISK_TEXTURE_ENTRIES*sizeof(DiskTexture)+DISK_TEXTURE_SLOTS*sizeof(uint16_t)+DISK_TEXTURE_BUFFER;
    if(jgr_scratch_heap_available()<TEXTURE_CACHE_HEADROOM+budget)goto fail;
    texture_disk.entries=calloc(DISK_TEXTURE_ENTRIES,sizeof *texture_disk.entries);
    texture_disk.slots=calloc(DISK_TEXTURE_SLOTS,sizeof *texture_disk.slots);
    texture_disk.buffer=malloc(DISK_TEXTURE_BUFFER);
    if(!texture_disk.entries || !texture_disk.slots || !texture_disk.buffer)goto fail;
    texture_disk.file=fopen(texture_disk.path,"w+b");if(!texture_disk.file)goto fail;
    texture_disk.owned=1;
    if(setvbuf(texture_disk.file,(char *)texture_disk.buffer,_IOFBF,DISK_TEXTURE_BUFFER))goto fail;
    texture_disk.append=1;
    errno=saved;return;
fail:texture_disk_disable();errno=saved;
}
static uint32_t texture_raw_crc(const unsigned char *raw,unsigned bytes)
{return jgr_crc32_add(UINT32_MAX,raw,bytes)^UINT32_MAX;}
static unsigned texture_disk_slot(const TextureKey *key,uint32_t crc)
{
    uint32_t value=crc^key->payload_size^(key->width*2654435761u)^(key->height*2246822519u);
    value^=value>>16;return value&(DISK_TEXTURE_SLOTS-1);
}
static int texture_same_encoding(const TextureKey *a,const TextureKey *b)
{
    return a->payload_size==b->payload_size && a->source_width==b->source_width &&
        a->source_height==b->source_height && a->width==b->width && a->height==b->height &&
        a->white==b->white && a->format==b->format && a->type==b->type;
}
/* The current payload is already in memory. Read only the candidate source
 * for exact collision rejection, with no retained copy of source pixels. */
static int texture_same_payload(JgrDisc *disc,const TextureKey *a,const TextureKey *b,const unsigned char *raw)
{
    if(a->source_offset==b->source_offset && a->payload_at==b->payload_at)return 1;
    JgrDiscFile source={b->source_offset,b->source_size};unsigned char block[8192];
    for(unsigned at=0;at<a->payload_size;) {
        unsigned n=a->payload_size-at;if(n>sizeof block)n=sizeof block;
        if(jgr_disc_read(disc,&source,b->payload_at+at,block,n))return -1;
        if(memcmp(block,raw+at,n))return 0;
        at+=n;
    }
    return 1;
}
static unsigned char *texture_disk_find(JgrDisc *disc,const TextureKey *key,const unsigned char *raw,uint32_t crc,unsigned *flags)
{
    if(!texture_disk.file || texture_cache.disc!=disc)return NULL;
    int saved=errno;unsigned slot=texture_disk_slot(key,crc);
    while(texture_disk.slots[slot]) {
        DiskTexture *e=texture_disk.entries+texture_disk.slots[slot]-1;
        if(e->raw_crc==crc && texture_same_encoding(key,&e->key)) {
            int same=texture_same_payload(disc,key,&e->key,raw);
            if(same<0){texture_disk_disable();errno=saved;return NULL;}
            if(same) {
                unsigned char *data=malloc(e->bytes);
                if(!data){errno=saved;return NULL;}
                texture_disk.append=0;
                if(fseek(texture_disk.file,e->offset,SEEK_SET) || fread(data,1,e->bytes,texture_disk.file)!=e->bytes ||
                    texture_raw_crc(data,e->bytes)!=e->encoded_crc) {
                    free(data);texture_disk_disable();errno=saved;return NULL;
                }
                *flags=e->flags;errno=saved;return data;
            }
        }
        slot=(slot+1)&(DISK_TEXTURE_SLOTS-1);
    }
    errno=saved;return NULL;
}
static void texture_disk_store(JgrDisc *disc,const TextureKey *key,uint32_t crc,const unsigned char *data,unsigned flags)
{
    unsigned bytes=1024+key->width*key->height;
    if(!texture_disk.file || texture_cache.disc!=disc || texture_disk.count>=DISK_TEXTURE_ENTRIES ||
        bytes>JGR_MODEL_TEXTURE_CACHE_DISK_BYTES-texture_disk.bytes)return;
    int saved=errno;unsigned slot=texture_disk_slot(key,crc);
    while(texture_disk.slots[slot])slot=(slot+1)&(DISK_TEXTURE_SLOTS-1);
    if((!texture_disk.append && fseek(texture_disk.file,texture_disk.bytes,SEEK_SET)) || fwrite(data,1,bytes,texture_disk.file)!=bytes) {
        texture_disk_disable();errno=saved;return;
    }
    texture_disk.append=1;
    DiskTexture *e=texture_disk.entries+texture_disk.count;
    e->key=*key;e->raw_crc=crc;e->encoded_crc=texture_raw_crc(data,bytes);
    e->offset=texture_disk.bytes;e->bytes=bytes;e->flags=flags;
    texture_disk.slots[slot]=(uint16_t)++texture_disk.count;texture_disk.bytes+=bytes;errno=saved;
}
static int read_plan(FILE *f,uint32_t offset,void *out,unsigned bytes)
{return fseek(f,(long)offset,SEEK_SET) || fread(out,1,bytes,f)!=bytes?-1:0;}
static int text_field(const unsigned char *p,unsigned size,int optional,int path)
{
    unsigned n=0;while(n<size && p[n]){if(p[n]<32 || p[n]>126 || p[n]=='\\')return -1;n++;}
    if(n==size || (!n && !optional))return -1;
    for(unsigned i=n;i<size;i++)if(p[i])return -1;
    if(!n)return 0;
    if(path){if(strncmp((const char*)p,"decompile/afs/",14) && strncmp((const char*)p,"decompile/extracted/",20))return -1;
        unsigned start=0;for(unsigned i=0;i<=n;i++)if(i==n || p[i]=='/'){
            unsigned len=i-start;if(!len || (len==1 && p[start]=='.') || (len==2 && p[start]=='.' && p[start+1]=='.'))return -1;start=i+1;
        }}return 0;
}
static int model_recipe_validate(FILE *plan,uint32_t offset,uint32_t bytes,ModelSourceUsage *usage)
{
    long saved;unsigned char header[32],row[ROW_BYTES],address[8];int result=-1;uint32_t cap_seen[256]={0};
    if(!plan || bytes<32 || bytes>MAX_BLOCK || offset>UINT32_MAX-bytes || (saved=ftell(plan))<0)return -1;
    if(read_plan(plan,offset,header,32) || memcmp(header,"JGRJGC1\0",8) || word(header+8)!=1 || word(header+12)!=ROW_BYTES || word(header+28))goto done;
    unsigned count=word(header+16),cap=word(header+20),flags=word(header+24);
    if(!count || count>4096 || count>(bytes-32)/ROW_BYTES || cap<16 || cap>512 || (cap&(cap-1)) || (flags&~32766u) || (flags&24u)==24u)goto done;
    if((flags&14336u) && (count!=1 || ((flags&14336u)&((flags&14336u)-1))))goto done;
    uint32_t tail=32+count*ROW_BYTES;
    for(unsigned i=0;i<count;i++){
        if(read_plan(plan,offset+32+i*ROW_BYTES,row,ROW_BYTES) || text_field(row,192,0,1) || text_field(row+192,192,0,1) || text_field(row+384,192,1,1) || text_field(row+576,16,0,0))goto done;
        model_source_count(usage,row);
        for(unsigned k=0;k<4;k++){uint32_t bits=word(row+592+4*k);float value;memcpy(&value,&bits,4);if(!isfinite(value))goto done;}
        uint32_t p[20];for(unsigned k=0;k<20;k++)p[k]=word(row+PARAM_AT+4*k);
        if(p[0]>1 || p[3]>4096 || p[5]>4096 || (p[7]!=UINT32_MAX && p[7]>=MAX_TEXTURES) || (p[8]&~1023u) || p[9]>2 || (p[9] && !(p[8]&8)) || (p[10]&~2032u) || (p[10] && !(p[10]&16)) || (p[11]<16 || p[11]>512 || (p[11]&(p[11]-1))))goto done;
        if(p[12]>3 || p[17]>4 || p[19]>MAX_TEXTURES || (i && p[19]))goto done;
        if(!p[12])for(unsigned k=13;k<18;k++)if(p[k])goto done;
        if(p[12] && (p[7]!=UINT32_MAX || !p[0]))goto done;
        if(p[12]){
            if(p[17]<2 && !p[14] && (p[15] || !p[17]))goto done;
            if(p[17]==2 && (p[14] || !p[15] || p[15]>UINT32_MAX-8))goto done;
            if(p[17]==4 && (p[12]!=1 || p[13]!=0x8c010000 || (p[14]!=1 && p[14]!=2 && p[14]!=3) || p[16]))goto done;
            if(p[17]==3 && (!p[14] || !(p[15]&65535u) || (p[15]&65535u)>256 || (p[15]>>16)>=(p[15]&65535u)))goto done;
        }
        if(!row[384] && p[6])goto done;
        if(!p[0] && (p[1] || p[2]>=64 || p[4] || p[5] || p[6] || row[384]))goto done;
        if(p[0] && p[2]<p[1])goto done;
        if(p[5]){
            if(p[4]!=tail || p[5]>(bytes-tail)/4)goto done;
            for(unsigned k=0;k<p[5];k++){if(read_plan(plan,offset+tail+4*k,address,4) || !word(address) || word(address)<(row[384]?p[6]:p[1]))goto done;}
            tail+=p[5]*4;
        }else if(p[4])goto done;
        if(p[19]){
            if(p[18]!=tail || p[19]>(bytes-tail)/8)goto done;
            for(unsigned k=0;k<p[19];k++){
                if(read_plan(plan,offset+tail+k*8,address,8))goto done;
                unsigned index=word(address),maximum=word(address+4);
                if(index>=MAX_TEXTURES || maximum<16 || maximum>512 || (maximum&(maximum-1)) || (cap_seen[index/32]&(1u<<(index&31))))goto done;
                cap_seen[index/32]|=1u<<(index&31);
            }tail+=p[19]*8;
        }else if(p[18])goto done;
    }if(tail!=bytes)goto done;result=0;
done:if(fseek(plan,saved,SEEK_SET))result=-1;return result;
}
int jgr_model_recipe_validate(FILE *plan,uint32_t offset,uint32_t bytes)
{return model_recipe_validate(plan,offset,bytes,NULL);}
static int scan_textures(JgrDisc *disc,const JgrDiscFile *file,uint32_t **result,unsigned *count)
{
    unsigned char buffer[4096];uint32_t *offsets=malloc(MAX_TEXTURES*sizeof *offsets);unsigned n=0;uint32_t at=0;
    if(!offsets)return -1;
    while(at<file->size && file->size-at>=4){unsigned size=file->size-at;if(size>sizeof buffer)size=sizeof buffer;
        if(jgr_disc_read(disc,file,at,buffer,size))goto fail;
        unsigned i=0;while(i+4<=size){if(!memcmp(buffer+i,"GBIX",4)){if(n>=MAX_TEXTURES || at+i>file->size-8)goto fail;offsets[n++]=at+i;i+=8;}else i++;}
        at+=i;
    }if(!n)goto fail;*result=offsets;*count=n;return 0;
fail:free(offsets);return -1;
}
static int source_read(JgrDisc *d,const JgrDiscFile *f,uint32_t base,uint32_t address,void *out,unsigned n)
{if(address<base || address-base>f->size || n>f->size-(address-base))return -1;return jgr_disc_read(d,f,address-base,out,n);}
static int offset_compare(const void *a,const void *b)
{uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b;return x<y?-1:x>y;}
static int loader_offsets(JgrDisc *disc,const JgrDiscFile *image,uint32_t base,uint32_t table,uint32_t texture_base,const JgrDiscFile *textures,uint32_t **out,unsigned *count)
{
    uint32_t *values=malloc(MAX_TEXTURES*sizeof *values),*hash=calloc(MAX_TEXTURES*2,sizeof *hash);unsigned n=0;unsigned char row[8],data[4];int result=-1;
    if(!values || !hash)goto done;
    if(table)for(unsigned i=0;i<1024;i++){
        if(table>UINT32_MAX-i*8 || source_read(disc,image,base,table+i*8,row,8))goto done;
        uint32_t list=word(row),array=word(row+4);if(!list)break;
        if(source_read(disc,image,base,list,row,8))goto done;
        unsigned entries=word(row+4);if(!entries || entries>256)goto done;
        for(unsigned j=0;j<entries;j++){
            if(array>UINT32_MAX-j*4 || source_read(disc,image,base,array+j*4,data,4))goto done;
            uint32_t address=word(data);if(address<texture_base)goto done;address-=texture_base;
            if(address>textures->size || 8>textures->size-address || jgr_disc_read(disc,textures,address,data,4) || memcmp(data,"GBIX",4))goto done;
            unsigned slot=(address*2654435761u)&(MAX_TEXTURES*2-1);while(hash[slot] && values[hash[slot]-1]!=address)slot=(slot+1)&(MAX_TEXTURES*2-1);
            if(!hash[slot]){if(n>=MAX_TEXTURES)goto done;values[n]=address;hash[slot]=++n;}
        }if(i==1023)goto done;
    }
    qsort(values,n,sizeof *values,offset_compare);*out=values;*count=n;values=NULL;result=0;
done:free(values);free(hash);return result;
}
static int texture_image(JgrDisc *disc,const JgrDiscFile *source,const JgrDiscFile *texture,const uint32_t p[20],JgrDiscFile *image)
{if(p[12]==1)return jgr_disc_find(disc,"1ST_READ.BIN",image);if(p[12]==2){*image=*source;return 0;}if(p[12]==3){*image=*texture;return 0;}return -1;}
/* modes 0/1 resolve a loader table; mode 2 reads an inline NJS_TEXLIST;
 * mode 3 reads a bounded original pointer array with an optional rotation. */
static int texture_mapping(JgrDisc *disc,const JgrDiscFile *source,const TextureBank *bank,const uint32_t p[20],uint16_t map[512],unsigned *count)
{
    JgrDiscFile image;unsigned char row[8],data[4];unsigned entries=0,rotation=0;uint32_t array=0;
    *count=0;if(!p[15])return 0;
    if(texture_image(disc,source,&bank->source,p,&image))return -1;
    if(p[17]==2){
        if(source_read(disc,&image,p[13],p[15],row,8))return -1;
        entries=word(row+4);array=p[15]+8;
    }else if(p[17]==3){entries=p[15]&65535u;rotation=p[15]>>16;array=p[14];}
    else for(unsigned i=0;i<1024;i++){
        if(p[14]>UINT32_MAX-i*8 || source_read(disc,&image,p[13],p[14]+i*8,row,8))return -1;
        uint32_t list=word(row);if(!list)break;
        if(list==p[15]){
            array=word(row+4);
            if(source_read(disc,&image,p[13],list,row,8))return -1;
            entries=word(row+4);break;
        }
    }
    if(!entries || entries>256 || rotation>=entries)return -1;
    for(unsigned j=0;j<entries;j++){
        unsigned item=(j+rotation)%entries;
        if(array>UINT32_MAX-item*4 || source_read(disc,&image,p[13],array+item*4,data,4))return -1;
        uint32_t address=word(data);if(address<p[16])return -1;address-=p[16];unsigned lo=0,hi=bank->count;
        while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(bank->offsets[mid]<address)lo=mid+1;else hi=mid;}
        if(lo>=bank->count || bank->offsets[lo]!=address)return -1;
        map[j]=(uint16_t)lo;
    }*count=entries;return 0;
}
static int expand_sector_tail(JgrDisc *disc,const char *path,JgrDiscFile *file)
{
    const char *archive=path+14,*slash=strchr(archive,'/');char name[256];if(!slash || slash==archive || slash-archive>180)return -1;
    int n=snprintf(name,sizeof name,"JETRADIO/%.*s.AFS",(int)(slash-archive),archive);if(n<0 || (size_t)n>=sizeof name || file->size>UINT32_MAX-2047)return -1;
    JgrDiscFile container;if(jgr_disc_find(disc,name,&container))return -1;uint32_t size=(file->size+2047)&~2047u;
    if(file->offset<container.offset || file->offset-container.offset>container.size || size>container.size-(file->offset-container.offset))return -1;
    file->size=size;return 0;
}
static int stage_code(Textures *t,uint32_t a,unsigned n)
{return a>=0x8c010000 && a-0x8c010000<=t->exe.size && n<=t->exe.size-(a-0x8c010000);}
static uint32_t stage_word(Textures *t,uint32_t a)
{return stage_code(t,a,4)?word(t->code+(a-0x8c010000)):0;}
static int stage_order(const void *a,const void *b)
{
    const TextureBank *x=a,*y=b;int xa=strstr(x->path,"_ALL")!=NULL,ya=strstr(y->path,"_ALL")!=NULL;
    if(xa!=ya)return ya-xa;
    int n=strcmp(x->path,y->path);return n?n:x->member<y->member?-1:x->member>y->member;
}
static int stage_banks(Textures *t,unsigned stage,unsigned cap)
{
    char (*names)[64]=NULL;unsigned count=0;int result=-1;
    if(t->stage)return t->stage==stage?0:-1;
    if(t->count || jgr_disc_find(t->disc,"1ST_READ.BIN",&t->exe) || t->exe.size>4u*1024u*1024u)return -1;
    t->code=malloc(t->exe.size);names=calloc(1024,64);if(!t->code || !names || jgr_disc_read(t->disc,&t->exe,0,t->code,t->exe.size))goto done;
    for(unsigned i=0;i<t->exe.size;){
        unsigned begin=i;while(i<t->exe.size && ((t->code[i]>='A'&&t->code[i]<='Z')||(t->code[i]>='0'&&t->code[i]<='9')||t->code[i]=='_'))i++;
        unsigned n=i-begin;if(n && n<64 && i+4<=t->exe.size && !memcmp(t->code+i,".AFS",4)){
            if(count>=1024)goto done;
            memcpy(names[count++],t->code+begin,n);i+=4;
        }else if(i<t->exe.size)i++;
    }
    char prefix[20];snprintf(prefix,sizeof prefix,"STAGE%uTXP",stage);
    for(unsigned at=0;at+64<t->exe.size;at+=4){
        unsigned id=word(t->code+at),member=word(t->code+at+4),base=word(t->code+at+8),list=word(t->code+at+12);
        if(word(t->code+at+20)!=1 || id>=count || member>=256 || base<0x8c800000 || base>=0x8d000000 || !stage_code(t,list,1) || strncmp(names[id],prefix,strlen(prefix)))continue;
        unsigned i;for(i=0;i<t->count;i++)if(!strcmp(t->banks[i].path,names[id]) && t->banks[i].member==member)break;
        if(i<t->count)continue;
        if(t->count>=MAX_BANKS)goto done;
        TextureBank *p=t->banks+t->count++;strcpy(p->path,names[id]);p->member=member;p->stage_list=list;p->cap=cap;
    }
    if(!t->count)goto done;
    qsort(t->banks,t->count,sizeof *t->banks,stage_order);
    for(unsigned i=0;i<t->count;i++){
        TextureBank *p=t->banks+i;JgrDiscFile afs;char archive[256];int n=snprintf(archive,sizeof archive,"JETRADIO/%s.AFS",p->path);
        if(n<0 || (size_t)n>=sizeof archive || jgr_disc_find(t->disc,archive,&afs) || jgr_disc_afs(t->disc,&afs,p->member,&p->source) || scan_textures(t->disc,&p->source,&p->offsets,&p->count) || p->count>MAX_TEXTURES-t->total)goto done;
        p->first=t->total;t->total+=p->count;
    }
    t->stage=stage;result=0;
done:free(names);return result;
}
static int stage_mapping(Textures *t,unsigned texlist,uint16_t map[512],unsigned *count)
{
    if(!texlist){map[0]=0;*count=1;return 0;}
    unsigned n=stage_word(t,texlist+4);if(!stage_code(t,texlist,8) || !n || n>128)return -1;
    for(unsigned i=0;i<n;i++)map[i]=0xffff;
    for(unsigned i=0;i<t->count;i++){
        TextureBank *p=t->banks+i;uint32_t base=UINT32_MAX,at=p->stage_list;unsigned guard=0;
        while(stage_word(t,at)){
            if(!stage_code(t,at,8)||++guard>MAX_TEXTURES)return -1;
            unsigned data=stage_word(t,at+4);if(data<base)base=data;at+=8;
        }
        for(at=p->stage_list;stage_word(t,at);at+=8){
            unsigned address=stage_word(t,at+4)-base,lo=0,hi=p->count;
            while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(p->offsets[mid]<address)lo=mid+1;else hi=mid;}
            if(lo>=p->count || p->offsets[lo]!=address)continue;
            unsigned refs=stage_word(t,at),limit=0;
            while(stage_code(t,refs,8)&&stage_word(t,refs)){
                if(++limit>4096)return -1;
                if(stage_word(t,refs)==texlist){unsigned slot=stage_word(t,refs+4);if(slot<n)map[slot]=(uint16_t)(p->first+lo);}
                refs+=8;
            }
        }
    }
    for(unsigned i=0;i<n;i++)if(map[i]==0xffff)return -1;
    *count=n;return 0;
}
static int bank(Textures *all,const char *path,unsigned cap,const JgrDiscFile *source,const uint32_t p[20],unsigned *index)
{
    if(p[17]==4){if(stage_banks(all,p[14],cap))return -1;*index=0;return 0;}
    if(all->stage)return -1;
    for(unsigned i=0;!all->legacy && i<all->count;i++)if(!strcmp(all->banks[i].path,path)){if(all->banks[i].cap!=cap || all->banks[i].offset_mode!=(p[17]==1))return -1;*index=i;return 0;}
    if(all->count>=MAX_BANKS)return -1;
    TextureBank *b=all->banks+all->count;
    if(jgr_disc_source(all->disc,path,&b->source) || (all->sector_tail && expand_sector_tail(all->disc,path,&b->source)))return -1;
    b->offset_mode=p[17]==1;
    if(p[17]==1){JgrDiscFile image;if(texture_image(all->disc,source,&b->source,p,&image) || loader_offsets(all->disc,&image,p[13],p[14],p[16],&b->source,&b->offsets,&b->count))return -1;}
    else if(scan_textures(all->disc,&b->source,&b->offsets,&b->count))return -1;
    if(b->count>MAX_TEXTURES-all->total){free(b->offsets);b->offsets=NULL;return -1;}
    strcpy(b->path,path);b->first=all->total;b->cap=cap;all->total+=b->count;*index=all->count++;return 0;
}
static int emit_texture_cpu_impl(unsigned index,FILE *pixels,JgrJgcTexture *record,void *opaque)
{
    Textures *all=opaque;TextureBank *b=NULL;unsigned char header[64];
    model_step("read texture",index);
    for(unsigned i=0;i<all->count;i++)if(index>=all->banks[i].first && index-all->banks[i].first<all->banks[i].count){b=all->banks+i;break;}
    if(!b)return -1;
    uint32_t at=b->offsets[index-b->first];unsigned n=b->source.size-at;if(n>sizeof header)n=sizeof header;
    if(n<24 || jgr_disc_read(all->disc,&b->source,at,header,n))return -1;
    unsigned pvr=8;while(pvr+4<=n && memcmp(header+pvr,"PVRT",4))pvr++;
    if(pvr+16>n)return -1;
    unsigned size=word(header+pvr+4),sw=half(header+pvr+12),sh=half(header+pvr+14);
    if(size<8 || size>4u*1024u*1024u || !sw || !sh || sw>1024 || sh>1024)return -1;
    size-=8;
    uint32_t payload_at=at+pvr+16;if(size>b->source.size-payload_at)size=b->source.size-payload_at;
    unsigned cap=all->caps[index]?all->caps[index]:b->cap;
    if(all->caps[index])all->used_caps[index]=1;
    unsigned w=sw,h=sh;while(w>cap || h>cap){w/=2;h/=2;}if(w<16)w=16;if(h<8)h=8;
    TextureKey key;memset(&key,0,sizeof key);key.source_offset=b->source.offset;key.source_size=b->source.size;
    key.payload_at=payload_at;key.payload_size=size;key.source_width=sw;key.source_height=sh;
    key.width=w;key.height=h;key.cap=cap;key.white=all->white;key.format=header[pvr+8];key.type=header[pvr+9];
    CachedTexture *cached=texture_cache_find(all->disc,&key);
    if(cached){MODEL_STATISTICS(model_statistics.cache_hits++);return texture_cache_write(pixels,record,&key,cached->data,cached->flags);}
    while(jgr_scratch_heap_available()<TEXTURE_CACHE_HEADROOM+size+sw*sh*4 && texture_cache_evict()){}
    unsigned char *raw=malloc(size?size:1),*rgba=NULL;int result=-1;if(!raw)return -1;
    if(jgr_disc_read(all->disc,&b->source,payload_at,raw,size))goto done;
    uint32_t raw_crc=texture_disk.file?texture_raw_crc(raw,size):0;
    unsigned cached_flags=0;unsigned char *reused=texture_disk_find(all->disc,&key,raw,raw_crc,&cached_flags);
    if(reused) {
        MODEL_STATISTICS(model_statistics.cache_hits++);
        result=texture_cache_write(pixels,record,&key,reused,cached_flags);free(reused);goto done;
    }
    model_step("decode texture",index);
    rgba=jgr_pvr_decode_payload(raw,size,header[pvr+8],header[pvr+9],sw,sh);if(!rgba)goto done;
    if(all->white)for(unsigned i=0;i<sw*sh;i++)rgba[4*i]=rgba[4*i+1]=rgba[4*i+2]=255;
    model_step("encode texture",index);
    unsigned bytes=1024+w*h,flags=0;unsigned char *encoded=texture_cache_allocate(all->disc,bytes);
    if(encoded){
        result=jgr_indexed_memory(rgba,sw,sh,w,h,encoded,bytes,&flags);
        if(result){
            /* Resizing mutates its input. Re-decode before the ordinary path
             * if the optional output buffer crowded out an encoder allocation. */
            free(encoded);free(rgba);jgr_models_cache_clear();
            rgba=jgr_pvr_decode_payload(raw,size,key.format,key.type,sw,sh);
            if(!rgba)goto done;
            if(all->white)for(unsigned i=0;i<sw*sh;i++)rgba[4*i]=rgba[4*i+1]=rgba[4*i+2]=255;
            result=jgr_indexed_texture(rgba,sw,sh,w,h,pixels,record);
        }else{
            result=texture_cache_write(pixels,record,&key,encoded,flags);
            if(!result){texture_disk_store(all->disc,&key,raw_crc,encoded,flags);texture_cache_store(&key,encoded,bytes,flags);}else free(encoded);
        }
    }else result=jgr_indexed_texture(rgba,sw,sh,w,h,pixels,record);
done:free(raw);free(rgba);return result;
}
static int emit_texture_cpu(unsigned index,FILE *pixels,JgrJgcTexture *record,void *opaque)
{uint64_t start=model_clock();int result=emit_texture_cpu_impl(index,pixels,record,opaque);MODEL_STATISTICS(model_statistics.cpu_texture_us+=model_clock()-start);return result;}
#if JGR_SETUP_ME
struct PreparedTexture {TextureKey key;uint32_t raw_crc;unsigned index,flags,cached;unsigned char *image;};
static int texture_worker_verified,texture_worker_disabled;
/* An unconfirmed reset may not release borrowed memory. Retain it for the
 * setup transport's final fence; no fallback may touch this allocation. */
static JgrTextureJob *texture_quarantine;
static void prepared_free(PreparedTexture *p){if(p){free(p->image);free(p);}}
static PreparedTexture *prepare_texture_impl(Textures *all,unsigned index)
{
    TextureBank *b=NULL;unsigned char header[64];
    for(unsigned i=0;i<all->count;i++)if(index>=all->banks[i].first && index-all->banks[i].first<all->banks[i].count){b=all->banks+i;break;}
    if(!b)return NULL;
    uint32_t at=b->offsets[index-b->first];if(at>b->source.size)return NULL;
    unsigned n=b->source.size-at;if(n>sizeof header)n=sizeof header;
    if(n<24 || jgr_disc_read(all->disc,&b->source,at,header,n))return NULL;
    unsigned pvr=8;while(pvr+4<=n && memcmp(header+pvr,"PVRT",4))pvr++;
    if(pvr+16>n)return NULL;
    unsigned size=word(header+pvr+4),sw=half(header+pvr+12),sh=half(header+pvr+14);
    if(size<8 || size>4u*1024u*1024u || !sw || !sh || sw>1024 || sh>1024)return NULL;
    size-=8;uint32_t payload_at=at+pvr+16;
    if(payload_at>b->source.size)return NULL;
    if(size>b->source.size-payload_at)size=b->source.size-payload_at;
    unsigned cap=all->caps[index]?all->caps[index]:b->cap,w=sw,h=sh;
    if(all->caps[index])all->used_caps[index]=1;
    while(w>cap || h>cap){w/=2;h/=2;}if(w<16)w=16;if(h<8)h=8;
    TextureKey key;memset(&key,0,sizeof key);key.source_offset=b->source.offset;key.source_size=b->source.size;
    key.payload_at=payload_at;key.payload_size=size;key.source_width=sw;key.source_height=sh;
    key.width=w;key.height=h;key.cap=cap;key.white=all->white;key.format=header[pvr+8];key.type=header[pvr+9];
    if(texture_cache_find(all->disc,&key))return NULL;
    /* Resampler can own source, destination, horizontal intermediate and taps
     * simultaneously. A conservative bound avoids crowding normal allocation. */
    size_t peak=(size_t)size+(size_t)sw*sh*4+(size_t)w*h*4+(size_t)w*sh*4+256u*1024u;
    if(jgr_scratch_heap_available()<TEXTURE_CACHE_HEADROOM+peak)return NULL;
    PreparedTexture *p=calloc(1,sizeof *p);unsigned char *raw=NULL,*rgba=NULL;
    if(!p)return NULL;
    p->key=key;p->index=index;raw=malloc(size?size:1);if(!raw)goto fail;
    if(jgr_disc_read(all->disc,&b->source,payload_at,raw,size))goto fail;
    p->raw_crc=texture_disk.file?texture_raw_crc(raw,size):0;
    p->image=texture_disk_find(all->disc,&key,raw,p->raw_crc,&p->flags);
    if(p->image){p->cached=1;free(raw);return p;}
    rgba=jgr_pvr_decode_payload(raw,size,key.format,key.type,sw,sh);free(raw);raw=NULL;if(!rgba)goto fail;
    if(all->white)for(unsigned i=0;i<sw*sh;i++)rgba[4*i]=rgba[4*i+1]=rgba[4*i+2]=255;
    p->image=jgr_indexed_prepare(rgba,sw,sh,w,h,&p->flags);free(rgba);rgba=NULL;
    if(!p->image)goto fail;
    return p;
fail:free(raw);free(rgba);prepared_free(p);return NULL;
}
static PreparedTexture *prepare_texture(Textures *all,unsigned index)
{uint64_t start=model_clock();PreparedTexture *p=prepare_texture_impl(all,index);MODEL_STATISTICS(model_statistics.prepare_us+=model_clock()-start);return p;}
static int emit_texture_impl(unsigned index,FILE *pixels,JgrJgcTexture *record,void *opaque)
{
    Textures *all=opaque;PreparedTexture *p=all->prepared;all->prepared=NULL;
    if(p && p->index!=index){prepared_free(p);p=NULL;}
    if(texture_quarantine){prepared_free(p);return -1;}
    if(texture_worker_disabled || !jgr_setup_me_available()){
        prepared_free(p);return emit_texture_cpu(index,pixels,record,opaque);
    }
    if(!p)p=prepare_texture(all,index);
    if(!p)return emit_texture_cpu(index,pixels,record,opaque);
    if(p->cached) {
        MODEL_STATISTICS(model_statistics.cache_hits++);
        int result=texture_cache_write(pixels,record,&p->key,p->image,p->flags);
        prepared_free(p);return result;
    }
    TextureKey key=p->key;uint32_t raw_crc=p->raw_crc;size_t job_bytes=jgr_texture_job_bytes(key.width,key.height);
    JgrTextureJob *job=NULL;
    if(job_bytes && jgr_scratch_heap_available()>=TEXTURE_CACHE_HEADROOM+job_bytes)
        job=jgr_texture_job_create(p->image,key.width,key.height,p->flags);
    prepared_free(p);
    if(!job)return emit_texture_cpu(index,pixels,record,opaque);
    JgrSetupMeRange range={jgr_texture_job_range(job,&job_bytes),job_bytes,JGR_SETUP_ME_READ|JGR_SETUP_ME_WRITE};
    int started=jgr_setup_me_submit(jgr_texture_job_worker,(intptr_t)job,&range,1),result=-1;
    if(started){
        /* This is the overlap: only the CPU touches the disc/resampler while
         * ME independently quantizes the preceding, immutable whole image. */
        int next=jgr_jgc_next_texture(all->build,index+1);
        if(next>=0)all->prepared=prepare_texture(all,(unsigned)next);
        uint64_t wait_start=model_clock();int status=jgr_setup_me_wait(10000000u,&result);
        MODEL_STATISTICS(model_statistics.wait_us+=model_clock()-wait_start);
        if(status==JGR_SETUP_ME_PENDING){texture_quarantine=job;return -1;}
        if(status!=JGR_SETUP_ME_COMPLETE)result=-1;
        if(!result && !texture_worker_verified){
            if(jgr_texture_job_verify(job)){texture_worker_disabled=1;result=-1;}
            else texture_worker_verified=1;
        }
    }
    if(!started || result)result=jgr_texture_job_retry(job);
    const unsigned char *data=NULL;unsigned flags=0;
    if(!result)result=jgr_texture_job_result(job,&data,&flags);
    if(!result){
        result=texture_cache_write(pixels,record,&key,data,flags);
        if(!result){texture_disk_store(all->disc,&key,raw_crc,data,flags);unsigned bytes=1024+key.width*key.height;unsigned char *copy=texture_cache_allocate(all->disc,bytes);
            if(copy){memcpy(copy,data,bytes);texture_cache_store(&key,copy,bytes,flags);}}
    }
    jgr_texture_job_destroy(job);return result;
}
#else
static int emit_texture_impl(unsigned index,FILE *pixels,JgrJgcTexture *record,void *opaque)
{return emit_texture_cpu(index,pixels,record,opaque);}
#endif
static int emit_texture(unsigned index,FILE *pixels,JgrJgcTexture *record,void *opaque)
{uint64_t start=model_clock();int result=emit_texture_impl(index,pixels,record,opaque);MODEL_STATISTICS(model_statistics.textures++;model_statistics.texture_us+=model_clock()-start);return result;}
static int temp_path(char *out,size_t capacity,const char *directory,unsigned index)
{int n=snprintf(out,capacity,"%s/jgc-setup-%02u.tmp",directory,index);return n<0 || (size_t)n>=capacity?-1:0;}
static int postprocess(FILE *file,unsigned flags)
{
    unsigned char header[68],row[48];if(fflush(file) || fseek(file,0,SEEK_END))return -1;
    long size=ftell(file);if(size<68 || read_plan(file,0,header,sizeof header))return -1;
    unsigned bones=word(header+8),batches=word(header+12),bone_at=word(header+36),batch_at=word(header+40);
    if(bones>8192 || batches>131072 || bone_at>(unsigned long)size || (uint64_t)bones*48>(unsigned long)size-bone_at || batch_at>(unsigned long)size || (uint64_t)batches*16>(unsigned long)size-batch_at)return -1;
    if(flags&256)for(unsigned i=0;i<batches;i++){
        unsigned at=batch_at+i*16;if(read_plan(file,at,row,16))return -1;row[14]&=(unsigned char)~8u;
        if(fseek(file,at,SEEK_SET) || fwrite(row,1,16,file)!=16)return -1;
    }
    if(flags&512)for(unsigned i=0;i<bones;i++){
        if(read_plan(file,bone_at+i*48,row,48))return -1;
        unsigned first=word(row+40),count=word(row+44);
        if(count!=2 || first>batches || count>batches-first || read_plan(file,batch_at+first*16,row,32) || half(row+12)!=0 || half(row+28)!=1)return -1;
        if(fseek(file,batch_at+first*16,SEEK_SET) || fwrite(row+16,1,16,file)!=16 || fwrite(row,1,16,file)!=16)return -1;
    }
    return fflush(file)?-1:0;
}
static int copy_result(FILE *from,FILE *to)
{
    unsigned char bytes[8192];if(fseek(from,0,SEEK_SET))return -1;size_t n;
    while((n=fread(bytes,1,sizeof bytes,from)))if(fwrite(bytes,1,n,to)!=n)return -1;
    return ferror(from)?-1:0;
}
static int write_lighting(FILE *vertices,FILE *records,unsigned characters,FILE *output)
{
    if(fflush(vertices)||fflush(records)||fseek(vertices,0,SEEK_END)||fseek(records,0,SEEK_END))return -1;
    long size=ftell(vertices),lighting=ftell(records);if(size<0 || size%28 || (uint64_t)size/28*20!=(unsigned long)lighting)return -1;
    if(fseek(vertices,0,SEEK_SET))return -1;
    uint32_t hash=2166136261u;unsigned char row[28],header[20];
    for(long i=0;i<size/28;i++){if(fread(row,1,28,vertices)!=28)return -1;for(unsigned j=0;j<28;j++)if(j<8 || j>=12)hash=(hash^row[j])*16777619u;}
    memcpy(header,"PLT1",4);uint32_t values[4]={(uint32_t)(size/28),characters,hash,20};
    for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++)header[4+i*4+j]=(unsigned char)(values[i]>>(j*8));
    if(fwrite(header,1,20,output)!=20)return -1;
    return copy_result(records,output);
}
int jgr_models_convert(JgrDisc *disc,FILE *plan,uint32_t offset,uint32_t bytes,const char *directory,FILE *output)
{return jgr_models_convert_with_lighting(disc,plan,offset,bytes,directory,output,NULL);}
static int models_convert_impl(JgrDisc *disc,FILE *plan,uint32_t offset,uint32_t bytes,const char *directory,FILE *output,FILE *lighting_output)
{
    uint64_t started=model_clock(),part_started=0;int pack_started=0;
    FILE *sections[13]={0};JgrModelLight light;JgrJgcBuild *build=NULL;Textures textures;unsigned char header[32],row[ROW_BYTES];char name[1024];uint32_t *motions=NULL;int result=-1;
    JgrScratchPool *scratch=NULL;
    ModelSource source_view={0};ModelSourceUsage source_usage={0};model_source_used=0;
    model_error[0]=0;model_failure_error=0;model_step("validate recipe",0);
    texture_cache_trim();
    memset(&textures,0,sizeof textures);textures.disc=disc;
    if(!disc || !directory || !*directory || !output || model_recipe_validate(plan,offset,bytes,&source_usage) || read_plan(plan,offset,header,32)){model_failure(errno);return -1;}
    model_step("allocate texture banks",0);
    if(lighting_output && (lighting_output==output || word(header+24)!=16384)){errno=EINVAL;model_failure(errno);return -1;}
    textures.banks=calloc(MAX_BANKS,sizeof *textures.banks);if(!textures.banks){model_failure(errno);return -1;}textures.white=!!(word(header+24)&4);textures.sector_tail=!!(word(header+24)&32);textures.legacy=!!(word(header+24)&128);
    textures.caps=calloc(MAX_TEXTURES,sizeof *textures.caps);textures.used_caps=calloc(MAX_TEXTURES,1);
    MODEL_STATISTICS(model_statistics.jobs++);
    if(!textures.caps || !textures.used_caps)goto done;
    unsigned streams=(word(header+24)&16384)?13:(word(header+24)&(1024u|8192u))?12:(word(header+24)&(24u|256u|512u))?11:10;
    int light_loaded=0;
    scratch=jgr_scratch_begin();if(!scratch)goto done;
    texture_disk_open(disc,directory);
    for(unsigned i=0;i<streams;i++){
        if(temp_path(name,sizeof name,directory,i))goto done;
        model_step("open scratch stream",i);
        sections[i]=jgr_scratch_open(scratch,name);if(!sections[i])goto done;
    }
    model_step("initialize model builder",0);
    build=jgr_jgc_create(disc,sections,!!(word(header+24)&128));if(!build)goto done;
    for(unsigned i=0;i<word(header+16);i++){
        JgrJgcCharacter c;uint16_t map[512];unsigned bi;memset(&c,0,sizeof c);jgr_model_options_default(&c.model);
        model_step("read model recipe",i);
        if(read_plan(plan,offset+32+i*ROW_BYTES,row,ROW_BYTES))goto done;
        uint32_t p[20];for(unsigned k=0;k<20;k++)p[k]=word(row+PARAM_AT+4*k);
        for(unsigned k=0;k<p[19];k++){unsigned char cap[8];if(read_plan(plan,offset+p[18]+k*8,cap,8))goto done;textures.caps[word(cap)]=(uint16_t)word(cap+4);}
        model_step("read model source",i);
        if(jgr_disc_source(disc,(const char*)row,&c.source))goto done;
        model_step("preload geometry source",i);
        if(model_source_prepare(&source_view,scratch,disc,&c.source,model_source_rows(&source_usage,row,p[0],word(header+24))))goto done;
        model_step("read texture bank",i);
        if(bank(&textures,(const char*)row+192,p[11],&c.source,p,&bi))goto done;
        TextureBank *tb=textures.banks+bi;
        memcpy(c.name,row+576,16);for(unsigned k=0;k<4;k++){uint32_t value=word(row+592+4*k);memcpy(c.placement+k,&value,4);}
        c.collision_tree=!!(p[8]&512);
        c.motion_count=p[3];c.base=p[1];c.root=p[2];c.model.texture_base=tb->first;c.model.texture_count=textures.stage?textures.total:tb->count;
        c.model.light=!!(p[8]&1);c.model.toon=!!(p[8]&2);c.model.source_material=!!(p[8]&4);c.model.source_environment=!!(p[8]&8);c.force_alpha=!!(p[8]&16);c.force_two_sided=!!(p[8]&32);c.model.source_lighting=p[9];c.effect_blend=p[10];
        c.model.shadow_lift=p[8]&128?0.35:p[8]&256?0.45:0;
        model_step("prepare lighting",i);
        if(p[8]&64){
            if(!light_loaded){if(jgr_model_baked_light(disc,&light))goto done;light_loaded=1;}
            c.model.palette_mix=1;c.model.baked_light=&light;c.model.shadow_lift=p[8]&128?0.35:p[8]&256?0.45:0;
        }
        if(lighting_output || (word(header+24)&(1024u|8192u))){
            unsigned char magic[4];if(jgr_disc_read(disc,&c.source,0,magic,4))goto done;
            c.lighting=sections[11];c.model.retain_lighting=1|(!memcmp(magic,"PLAY",4)?512:0);
        }
        model_step("resolve model and motions",i);
        if(!p[0]){if(jgr_jgc_resolve_play(disc,&c.source,&tb->source,p[2],tb->first,map,&c))goto done;}
        else{
            if(row[384]){if(jgr_disc_source(disc,(const char*)row+384,&c.motion_source))goto done;c.motion_base=p[6];}
            if(p[5]){motions=malloc(p[5]*sizeof *motions);if(!motions)goto done;for(unsigned k=0;k<p[5];k++){unsigned char data[4];if(read_plan(plan,offset+p[4]+4*k,data,4))goto done;motions[k]=word(data);}c.motion_headers=motions;}
            if(c.motion_count>p[5])c.motion_count=p[5];
        }
        model_step("bind textures",i);
        if(p[12]){unsigned count;if(p[17]==4?stage_mapping(&textures,p[15],map,&count):texture_mapping(disc,&c.source,tb,p,map,&count))goto done;if(count){c.model.texture_map=map;c.model.texture_slots=count;}}
        if(p[7]!=UINT32_MAX){if(p[7]>=tb->count)goto done;map[0]=(uint16_t)p[7];c.model.texture_map=map;c.model.texture_slots=1;}
        if((word(header+24)&64) && c.model.texture_map)for(unsigned k=0;k<c.model.texture_slots;k++)if(jgr_jgc_keep_texture(build,tb->first+c.model.texture_map[k]))goto done;
        if((word(header+24)&16384) && i==26){
            if(tb->count<=1 || jgr_jgc_keep_texture(build,tb->first+1))goto done;
            textures.caps[tb->first+1]=256;
        }
        model_step("assemble geometry and motions",i);
        part_started=model_clock();int added=jgr_jgc_add(build,&c);
        MODEL_STATISTICS(model_statistics.geometry_us+=model_clock()-part_started;model_statistics.rows++);
        if(added)goto done;
        model_step("prepare face colors",i);
        if(word(header+24)&14336u){unsigned mode=word(header+24)&2048?1:word(header+24)&4096?2:3;
            if(jgr_models_face_colors(disc,&c,&tb->source,sections,sections[11],mode))goto done;}
        free(motions);motions=NULL;
    }
    model_source_release(&source_view);
    model_step("write lighting",0);
    if(word(header+24)&1024){if(write_lighting(sections[3],sections[11],word(header+16),output))goto done;result=0;goto done;}
    if(lighting_output && write_lighting(sections[3],sections[11],word(header+16),lighting_output))goto done;
    if(word(header+24)&2)for(unsigned i=0;i<textures.total;i++)if(jgr_jgc_keep_texture(build,i))goto done;
    model_step("finish model",0);textures.build=build;
    part_started=model_clock();int finished=jgr_jgc_finish(build,(streams==11 || (word(header+24)&16384))?sections[10]:output,emit_texture,&textures);
    MODEL_STATISTICS(model_statistics.finish_us+=model_clock()-part_started);
    if(finished)goto done;
    model_step("verify texture caps",0);
    for(unsigned i=0;i<MAX_TEXTURES;i++)if(textures.caps[i] && !textures.used_caps[i])goto done;
    model_step("pack model",0);
    part_started=model_clock();pack_started=1;
    if(word(header+24)&16384){
        /* The completed raw bank now owns every byte needed by packing. Drop
         * the original geometry/key sections before rebuilding their layout. */
        jgr_jgc_free(build);build=NULL;
        for(unsigned i=0;i<10;i++)if(jgr_scratch_reset(scratch,sections[i]))goto done;
        size_t view_bytes=0;const void *view=jgr_scratch_view(scratch,sections[10],&view_bytes);
        int packed=jgr_models_pack_cast_view(sections[10],sections[12],sections,view,view_bytes);
        if(view)jgr_scratch_unview(scratch,sections[10]);
        if(packed)goto done;
        for(unsigned i=0;i<12;i++)if(jgr_scratch_reset(scratch,sections[i]))goto done;
        if(word(header+24)&8){
            view=jgr_scratch_view(scratch,sections[12],&view_bytes);
            int compacted=jgr_model_compact_view(sections[12],output,0,view,view_bytes);
            if(view)jgr_scratch_unview(scratch,sections[12]);
            if(compacted)goto done;
        }
        else if(copy_result(sections[12],output))goto done;
    }else if(streams==11){
        if(postprocess(sections[10],word(header+24)))goto done;
        if(word(header+24)&24){if(jgr_model_compact(sections[10],output,!!(word(header+24)&16)))goto done;}
        else if(copy_result(sections[10],output))goto done;
    }
    result=0;
done:MODEL_STATISTICS(if(pack_started)model_statistics.pack_us+=model_clock()-part_started);
    if(result)model_failure(errno);
    model_source_release(&source_view);
#if JGR_SETUP_ME
    prepared_free(textures.prepared);
    if(texture_quarantine && jgr_setup_me_cancel()){jgr_texture_job_destroy(texture_quarantine);texture_quarantine=NULL;}
#endif
    free(motions);jgr_jgc_free(build);for(unsigned i=0;i<textures.count;i++)free(textures.banks[i].offsets);free(textures.banks);free(textures.caps);free(textures.used_caps);free(textures.code);
    for(unsigned i=0;i<13;i++)if(sections[i] && jgr_scratch_discard(scratch,sections[i]) && !result){
        int error=errno;model_step("close scratch stream",i);model_failure(error);result=-1;
    }
    if(scratch && jgr_scratch_end()){model_failure(errno);result=-1;}
    MODEL_STATISTICS(if(scratch){model_statistics.spills+=scratch->spills;model_statistics.opens+=scratch->opens;
        if(scratch->peak>model_statistics.scratch_peak)model_statistics.scratch_peak=scratch->peak;});
    MODEL_STATISTICS(model_statistics.total_us+=model_clock()-started);
    return result;
}
int jgr_models_convert_with_lighting(JgrDisc *disc,FILE *plan,uint32_t offset,uint32_t bytes,const char *directory,FILE *output,FILE *lighting_output)
{
    long initial=output?ftell(output):-1,lighting_initial=lighting_output?ftell(lighting_output):0;
    int result=models_convert_impl(disc,plan,offset,bytes,directory,output,lighting_output);
#if JGR_SETUP_ME
    if(texture_quarantine)return result;
#endif
    /* Optional cache residency cannot turn a previously viable job into an
     * allocation failure. Each failed attempt has released its working set;
     * rewind the caller's private streams and retry once without any cache.
     * A partial first attempt writes only a prefix of the same final bank. */
    if(result && model_failure_error==ENOMEM && (texture_cache.bytes || texture_disk.file || model_source_used) && initial>=0 && lighting_initial>=0) {
        jgr_models_cache_clear();texture_disk_disable();texture_cache.disc=NULL;jgr_indexed_cache_end();
        clearerr(output);if(lighting_output)clearerr(lighting_output);
        if(fseek(output,initial,SEEK_SET) || (lighting_output && fseek(lighting_output,lighting_initial,SEEK_SET))){model_failure(errno);return -1;}
        int suppressed=model_source_suppressed;model_source_suppressed=1;
        result=models_convert_impl(disc,plan,offset,bytes,directory,output,lighting_output);
        model_source_suppressed=suppressed;
    }
    return result;
}
