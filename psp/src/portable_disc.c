/* Installer input exists before the verified asset namespace. The portable
 * build force-includes its I/O redirects before this source-level guard. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#undef sceIoOpen
#undef sceIoGetstat
#undef sceIoRemove
#undef sceIoRename
#include "portable_disc.h"
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DISC_CACHE_WAYS 8u
#define DISC_CACHE_SECTORS 8u
#define DISC_CACHE_BYTES (DISC_CACHE_SECTORS*2352u)
#define DISC_PATH_BYTES 1024u
#define DISC_DIRECTORY_MAX (16u * 1024u * 1024u)
#define DISC_DIRECTORY_DEPTH 32u
#define DISC_AFS_CACHE_WAYS 16u
#define DISC_AFS_CACHE_BYTES (16u * 1024u)
#define DISC_FIND_CACHE_WAYS 16u
#define DISC_FIND_NAME_BYTES 128u

typedef struct {
    JgrDiscFile file;
    unsigned char *rows;
    uint32_t count, age, name_offset;
    int name_known;
} DiscAfsIndex;
typedef struct {
    JgrDiscFile file;
    uint32_t age;
    char path[DISC_FIND_NAME_BYTES];
} DiscFileIndex;

struct JgrDisc {
    FILE *stream;
    JgrDiscView *view;
    uint64_t stream_position;
    uint32_t lba, sectors, sector_size, track_offset;
    uint32_t cached_first[DISC_CACHE_WAYS],cached_count[DISC_CACHE_WAYS],age[DISC_CACHE_WAYS],clock;
    JgrDiscFile root;
    char track_path[DISC_PATH_BYTES];
    struct stat track_identity;
    DiscAfsIndex archives[DISC_AFS_CACHE_WAYS];
    DiscFileIndex files[DISC_FIND_CACHE_WAYS];
    uint32_t metadata_clock, archive_bytes;
#ifdef JGR_DISC_TESTING
    JgrDiscCacheStats stats;
    JgrDiscReadStats read_stats;
    JgrDiscViewStats view_stats;
#endif
    char error[192];
    unsigned char cache[DISC_CACHE_WAYS][DISC_CACHE_BYTES];
};

static uint32_t le32(const unsigned char *p)
{return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;}
static uint32_t be32(const unsigned char *p)
{return (uint32_t)p[3] | (uint32_t)p[2]<<8 | (uint32_t)p[1]<<16 | (uint32_t)p[0]<<24;}
static uint32_t rotate(uint32_t a, unsigned n) {return a>>n | a<<(32-n);}

/* SHA-256's round constants are specified by the hash algorithm. */
static void sha_block(JgrSha256 *s, const unsigned char *p)
{
    static const uint32_t k[64]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    /* Eight-word groups avoid the state-register shuffle after every round
     * on Allegrex. The message schedule and SHA-256 operations are unchanged. */
    uint32_t w[64],a,b,c,d,e,f,g,h; unsigned i;
    for(i=0;i<16;i++)w[i]=be32(p+4*i);
#define SCHEDULE(j) do { \
        uint32_t x=w[i+(j)-15],y=w[i+(j)-2]; \
        w[i+(j)]=w[i+(j)-16]+(rotate(x,7)^rotate(x,18)^(x>>3))+w[i+(j)-7]+(rotate(y,17)^rotate(y,19)^(y>>10)); \
    } while(0)
    for(i=16;i<64;i+=8) {
        SCHEDULE(0); SCHEDULE(1); SCHEDULE(2); SCHEDULE(3);
        SCHEDULE(4); SCHEDULE(5); SCHEDULE(6); SCHEDULE(7);
    }
#undef SCHEDULE
    a=s->state[0];b=s->state[1];c=s->state[2];d=s->state[3];
    e=s->state[4];f=s->state[5];g=s->state[6];h=s->state[7];
#define ROUND(a,b,c,d,e,f,g,h,j) do { \
    uint32_t t=(h)+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((g)^((e)&((f)^(g))))+k[i+(j)]+w[i+(j)]; \
    (d)+=t; (h)=t+(rotate(a,2)^rotate(a,13)^rotate(a,22))+(((a)&(b))|((c)&((a)|(b)))); \
}while(0)
    for(i=0;i<64;i+=8) {
        ROUND(a,b,c,d,e,f,g,h,0); ROUND(h,a,b,c,d,e,f,g,1);
        ROUND(g,h,a,b,c,d,e,f,2); ROUND(f,g,h,a,b,c,d,e,3);
        ROUND(e,f,g,h,a,b,c,d,4); ROUND(d,e,f,g,h,a,b,c,5);
        ROUND(c,d,e,f,g,h,a,b,6); ROUND(b,c,d,e,f,g,h,a,7);
    }
#undef ROUND
    s->state[0]+=a;s->state[1]+=b;s->state[2]+=c;s->state[3]+=d;
    s->state[4]+=e;s->state[5]+=f;s->state[6]+=g;s->state[7]+=h;
}
void jgr_sha256_init(JgrSha256 *s)
{
    static const uint32_t initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(s->state,initial,sizeof initial);s->bytes=0;s->used=0;
}
void jgr_sha256_update(JgrSha256 *s,const void *data,size_t size)
{
    const unsigned char *p=(const unsigned char *)data;s->bytes+=size;
    while(size) {
        /* Complete input blocks need no intermediate copy. sha_block reads
         * bytes explicitly, so this also accepts unaligned caller buffers. */
        if(!s->used && size>=64){sha_block(s,p);p+=64;size-=64;continue;}
        size_t n=64-s->used;if(n>size)n=size;
        memcpy(s->block+s->used,p,n);s->used+=(unsigned)n;p+=n;size-=n;
        if(s->used==64){sha_block(s,s->block);s->used=0;}
    }
}
void jgr_sha256_final(JgrSha256 *s,unsigned char digest[32])
{
    uint64_t bits=s->bytes*8;unsigned i;
    s->block[s->used++]=0x80;
    if(s->used>56){memset(s->block+s->used,0,64-s->used);sha_block(s,s->block);s->used=0;}
    memset(s->block+s->used,0,56-s->used);
    for(i=0;i<8;i++)s->block[63-i]=(unsigned char)(bits>>(i*8));
    sha_block(s,s->block);
    for(i=0;i<32;i++)digest[i]=(unsigned char)(s->state[i/4]>>(24-(i%4)*8));
    memset(s,0,sizeof *s);
}
void jgr_sha256_hex(const unsigned char digest[32],char hex[65])
{
    static const char digits[]="0123456789abcdef";unsigned i;
    for(i=0;i<32;i++){hex[2*i]=digits[digest[i]>>4];hex[2*i+1]=digits[digest[i]&15];}hex[64]=0;
}

static int fail(JgrDisc *d,const char *format,...)
{
    va_list args;va_start(args,format);vsnprintf(d->error,sizeof d->error,format,args);va_end(args);return -1;
}
const char *jgr_disc_error(const JgrDisc *d) {return d ? d->error : "No disc is open";}
int jgr_disc_clone(JgrDisc **out,const JgrDisc *source,char *error,size_t error_size)
{
    JgrDisc *copy=NULL;const char *why="Cannot clone source disc";struct stat before,after;
    if(out)*out=NULL;
    if(!out || !source || !source->stream || !source->track_path[0])goto bad;
    if(fstat(fileno(source->stream),&before))goto bad;
    const struct stat *original=&source->track_identity;
    if(before.st_dev!=original->st_dev || before.st_ino!=original->st_ino ||
       before.st_size!=original->st_size || before.st_mtime!=original->st_mtime || before.st_ctime!=original->st_ctime){
        why="Validated source track metadata changed";goto bad;
    }
    copy=calloc(1,sizeof *copy);if(!copy){why="Not enough memory for source reader";goto bad;}
    copy->stream=fopen(source->track_path,"rb");
    if(!copy->stream || fstat(fileno(copy->stream),&after))goto bad;
    if(!S_ISREG(after.st_mode) || before.st_dev!=after.st_dev || before.st_ino!=after.st_ino ||
       before.st_size!=after.st_size || before.st_mtime!=after.st_mtime || before.st_ctime!=after.st_ctime){
        why="Source track changed while opening worker reader";goto bad;
    }
    if(setvbuf(copy->stream,NULL,_IONBF,0))goto bad;
    copy->lba=source->lba;copy->sectors=source->sectors;
    copy->sector_size=source->sector_size;copy->track_offset=source->track_offset;
    copy->root=source->root;copy->track_identity=source->track_identity;strcpy(copy->track_path,source->track_path);
    copy->stream_position=UINT64_MAX;*out=copy;return 0;
bad:
    if(error && error_size)snprintf(error,error_size,"%s",why);
    jgr_disc_close(copy);return -1;
}
void jgr_disc_close(JgrDisc *d)
{
    if(d) {
        while(d->view){JgrDiscView *v=d->view;d->view=v->previous;memset(v,0,sizeof *v);}
        for(unsigned i=0;i<DISC_AFS_CACHE_WAYS;i++)free(d->archives[i].rows);
        if(d->stream)fclose(d->stream);
        free(d);
    }
}
#ifdef JGR_DISC_TESTING
#define DISC_COUNT(d,field,n) ((d)->stats.field+=(n))
void jgr_disc_cache_stats(const JgrDisc *d,JgrDiscCacheStats *stats)
{
    if(!d || !stats)return;
    *stats=d->stats;stats->afs_cache_bytes=d->archive_bytes;
    stats->afs_cache_limit=DISC_AFS_CACHE_BYTES;
    for(unsigned i=0;i<DISC_AFS_CACHE_WAYS;i++)if(d->archives[i].rows)stats->afs_cache_entries++;
    for(unsigned i=0;i<DISC_FIND_CACHE_WAYS;i++)if(d->files[i].path[0])stats->find_cache_entries++;
}
void jgr_disc_read_stats(const JgrDisc *d,JgrDiscReadStats *stats)
{if(d && stats)*stats=d->read_stats;}
void jgr_disc_view_stats(const JgrDisc *d,JgrDiscViewStats *stats)
{if(d && stats)*stats=d->view_stats;}
#else
#define DISC_COUNT(d,field,n) ((void)0)
#endif
static uint32_t metadata_touch(JgrDisc *d)
{
    if(++d->metadata_clock==0) {
        for(unsigned i=0;i<DISC_AFS_CACHE_WAYS;i++)d->archives[i].age=0;
        for(unsigned i=0;i<DISC_FIND_CACHE_WAYS;i++)d->files[i].age=0;
        d->metadata_clock=1;
    }
    return d->metadata_clock;
}

static int safe_path(const char *path)
{
    const char *part=path,*p=path;
    if(!path || !*path || *path=='/')return 0;
    do {
        unsigned char c=(unsigned char)*p;
        if(c && (c<32 || c>=127 || c=='\\' || c==':'))return 0;
        if(!c || c=='/') {
            size_t n=(size_t)(p-part);
            if(!n || (n==1 && part[0]=='.') || (n==2 && part[0]=='.' && part[1]=='.'))return 0;
            part=p+1;
        }
    }while(*p++);
    return 1;
}
static int equal_name(const char *a,const char *b)
{while(*a && *b){if(toupper((unsigned char)*a++)!=toupper((unsigned char)*b++))return 0;}return *a==*b;}

static int read_stream(JgrDisc *d,uint64_t physical,void *out,size_t size)
{
#ifdef JGR_DISC_TESTING
    d->read_stats.physical_reads++;d->read_stats.physical_bytes+=size;
#endif
    if(physical>INT32_MAX ||
       (physical!=d->stream_position && fseek(d->stream,(long)physical,SEEK_SET)) ||
       fread(out,1,size,d->stream)!=size) {
        d->stream_position=UINT64_MAX;
        return fail(d,"Cannot read data track");
    }
    d->stream_position=physical+size;
    return 0;
}
static int read_physical(JgrDisc *d,uint32_t first,uint32_t count,void *out)
{
    return read_stream(d,(uint64_t)d->track_offset+(uint64_t)first*d->sector_size,
                       out,(size_t)count*d->sector_size);
}
static int raw_sector_valid(JgrDisc *d,const unsigned char *raw)
{
    if(raw[0] || raw[11] || raw[15]!=1)return fail(d,"Only raw MODE1 sectors are supported");
    for(unsigned i=1;i<11;i++)if(raw[i]!=255)return fail(d,"Invalid raw sector synchronization");
    return 0;
}
static void touch_cache(JgrDisc *d,unsigned slot)
{
    if(++d->clock==0){memset(d->age,0,sizeof d->age);d->clock=1;}
    d->age[slot]=d->clock;
}
static int read_logical(JgrDisc *d,uint64_t offset,void *destination,size_t size)
{
    unsigned char *out=(unsigned char *)destination;
    uint64_t limit=(uint64_t)d->sectors*2048;
    if(offset>limit || size>limit-offset || (size && !out))return fail(d,"Read exceeds data track");
    while(size) {
        uint32_t sector=(uint32_t)(offset/2048),within=(uint32_t)(offset%2048);
        const unsigned char *raw;size_t n=2048-within;
        /* Geometry alternates between index, vertex, object and executable
         * pages. Keep them resident independently instead of replacing a
         * single large sequential window for every small source read. */
        unsigned slot=DISC_CACHE_WAYS;
        for(unsigned k=0;k<DISC_CACHE_WAYS;k++)if(sector>=d->cached_first[k] && sector-d->cached_first[k]<d->cached_count[k]){slot=k;break;}
        if(slot==DISC_CACHE_WAYS) {
            uint32_t first=sector&~(DISC_CACHE_SECTORS-1u);
            uint32_t count=DISC_CACHE_SECTORS;
            if(size>=DISC_CACHE_SECTORS*2048u) {
                /* Bulk copies fill adjacent cache ways in one Memory Stick
                 * read, using the same bounded storage as random geometry
                 * reads. Every consumed raw sector is still checked below.
                 * Cooked sectors are moved to each way's fixed raw stride. */
                uint64_t needed=((offset-(uint64_t)first*2048)+size+2047)/2048;
                count=needed>DISC_CACHE_WAYS*DISC_CACHE_SECTORS ?
                    DISC_CACHE_WAYS*DISC_CACHE_SECTORS:(uint32_t)needed;
                count=(count+DISC_CACHE_SECTORS-1u)&~(DISC_CACHE_SECTORS-1u);
                if(count>d->sectors-first)count=d->sectors-first;
                unsigned ways=(count+DISC_CACHE_SECTORS-1u)/DISC_CACHE_SECTORS;
                for(unsigned k=0;k<ways;k++)d->cached_count[k]=0;
                if(read_physical(d,first,count,d->cache))return -1;
                for(unsigned k=ways;k--;) {
                    unsigned start=k*DISC_CACHE_SECTORS,available=count-start;
                    if(available>DISC_CACHE_SECTORS)available=DISC_CACHE_SECTORS;
                    if(d->sector_size!=2352)
                        memmove(d->cache[k],(unsigned char *)d->cache+start*d->sector_size,available*d->sector_size);
                    d->cached_first[k]=first+start;d->cached_count[k]=available;touch_cache(d,k);
                }
                slot=0;
            } else {
                slot=0;for(unsigned k=1;k<DISC_CACHE_WAYS;k++)if(d->age[k]<d->age[slot])slot=k;
                if(count>d->sectors-first)count=d->sectors-first;
                d->cached_count[slot]=0;
                if(read_physical(d,first,count,d->cache[slot]))return -1;
                d->cached_first[slot]=first;d->cached_count[slot]=count;
            }
        }
        touch_cache(d,slot);
        raw=d->cache[slot]+(sector-d->cached_first[slot])*d->sector_size;
        if(d->sector_size==2352) {
            if(raw_sector_valid(d,raw))return -1;
            raw+=16;
        }
        if(n>size)n=size;
        memcpy(out,raw+within,n);out+=n;offset+=n;size-=n;
    }
    return 0;
}
static int valid_file_read(JgrDisc *d,const JgrDiscFile *file,uint32_t offset,size_t size)
{
    uint64_t limit;
    if(!d || !file)return -1;
    limit=(uint64_t)d->sectors*2048;
    if(file->offset>limit || file->size>limit-file->offset)return fail(d,"Source extent exceeds data track");
    if(offset>file->size || size>file->size-offset || file->offset>UINT64_MAX-offset)
        return fail(d,"Read exceeds source file");
    return 0;
}
int jgr_disc_read(JgrDisc *d,const JgrDiscFile *file,uint32_t offset,void *out,size_t size)
{
    if(valid_file_read(d,file,offset,size))return -1;
    if(size && !out)return fail(d,"Missing read destination");
    for(const JgrDiscView *v=d->view;v;v=v->previous)if(v->file.offset==file->offset && v->file.size==file->size){
        if(size)memcpy(out,v->data+offset,size);
#ifdef JGR_DISC_TESTING
        d->view_stats.reads++;d->view_stats.bytes+=size;
#endif
        return 0;
    }
    return read_logical(d,file->offset+offset,out,size);
}
int jgr_disc_view_begin(JgrDisc *d,JgrDiscView *view,const JgrDiscFile *file,const void *data,size_t bytes)
{
    if(!view || view->disc || !data || !file || bytes!=file->size || valid_file_read(d,file,0,bytes))return -1;
    view->disc=d;view->file=*file;view->data=data;view->previous=d->view;d->view=view;return 0;
}
int jgr_disc_view_end(JgrDiscView *view)
{
    if(!view)return -1;
    if(!view->disc)return 0;
    if(view->disc->view!=view)return -1;
    view->disc->view=view->previous;memset(view,0,sizeof *view);return 0;
}
int jgr_disc_read_sequential(JgrDisc *d,const JgrDiscFile *file,uint32_t offset,
                             void *buffer,size_t size,size_t capacity)
{
    if(valid_file_read(d,file,offset,size))return -1;
    if(size>capacity || (size && !buffer))return fail(d,"Sequential read buffer is too small");
    if(!size)return 0;
    if(size>JGR_DISC_SEQUENTIAL_BYTES)return jgr_disc_read(d,file,offset,buffer,size);
    uint64_t absolute=file->offset+offset;
    if(d->sector_size==2048) {
#ifdef JGR_DISC_TESTING
        d->read_stats.sequential_reads++;
#endif
        return read_stream(d,(uint64_t)d->track_offset+absolute,buffer,size);
    }
    uint32_t first=(uint32_t)(absolute/2048),within=(uint32_t)(absolute%2048);
    uint32_t count=(uint32_t)((within+size+2047)/2048);
    size_t raw_bytes=(size_t)count*2352;
    if(capacity<raw_bytes)return jgr_disc_read(d,file,offset,buffer,size);
#ifdef JGR_DISC_TESTING
    d->read_stats.sequential_reads++;
#endif
    if(read_physical(d,first,count,buffer))return -1;
    unsigned char *raw=(unsigned char *)buffer,*out=raw;
    /* Validate before compaction can overwrite headers from earlier sectors.
     * Only sectors contributing requested payload bytes are read/validated. */
    for(uint32_t i=0;i<count;i++)if(raw_sector_valid(d,raw+(size_t)i*2352))return -1;
    size_t left=size;
    for(uint32_t i=0;i<count;i++) {
        size_t n=2048-within;if(n>left)n=left;
        /* Source and destination overlap, including the first raw sector. */
        memmove(out,raw+(size_t)i*2352+16+within,n);
        out+=n;left-=n;within=0;
    }
    return 0;
}
int jgr_disc_sector_bounds(const JgrDisc *d,uint32_t *first,uint32_t *count)
{
    if(!d || !first || !count)return -1;
    *first=d->lba;*count=d->sectors;return 0;
}
int jgr_disc_sectors(JgrDisc *d,uint32_t lba,unsigned count,void *out,size_t capacity)
{
    uint64_t relative;size_t bytes;
    if(!d)return -1;
    if(lba<d->lba || count>32)return fail(d,"Sector request exceeds supported data track or 64 KiB bound");
    relative=(uint64_t)lba-d->lba;bytes=(size_t)count*2048;
    if(relative>d->sectors || count>d->sectors-relative || bytes>capacity || (bytes && !out))
        return fail(d,"Sector request exceeds data track or destination");
    return read_logical(d,relative*2048,out,bytes);
}
static int iso_extent(JgrDisc *d,const unsigned char *record,unsigned n,JgrDiscFile *file,unsigned *flags)
{
    uint32_t lba,size;
    if(n<34 || record[0]!=n || (unsigned)record[32]+33>n)return fail(d,"Malformed ISO directory record");
    if(record[1] || record[26] || record[27] || (record[25]&0x80))return fail(d,"Unsupported ISO extent encoding");
    lba=le32(record+2);size=le32(record+10);
    if(lba!=be32(record+6) || size!=be32(record+14))return fail(d,"ISO endian copies disagree");
    if(lba<d->lba || (uint64_t)(lba-d->lba)+(size+2047ull)/2048>d->sectors)
        return fail(d,"ISO extent exceeds data track");
    file->offset=(uint64_t)(lba-d->lba)*2048;file->size=size;*flags=record[25];return 0;
}

/* Returns one non-dot entry, zero at directory end, or -1 on malformed input. */
static int next_entry(JgrDisc *d,const JgrDiscFile *directory,uint32_t *position,
                      char name[256],JgrDiscFile *file,unsigned *flags)
{
    unsigned char record[256];
    if(directory->size>DISC_DIRECTORY_MAX)return fail(d,"ISO directory is too large");
    while(*position<directory->size) {
        unsigned n,len,i;uint32_t at=*position;
        if(jgr_disc_read(d,directory,at,record,1))return -1;
        n=record[0];
        if(!n){*position=(at/2048+1)*2048;continue;}
        if(n>2048-at%2048 || n>directory->size-at)return fail(d,"ISO record crosses a sector or directory");
        if(jgr_disc_read(d,directory,at,record,n) || iso_extent(d,record,n,file,flags))return -1;
        *position=at+n;len=record[32];
        if(len==1 && (record[33]==0 || record[33]==1))continue;
        for(i=0;i<len && record[33+i]!=';';i++) {
            if(record[33+i]<32 || record[33+i]>=127)return fail(d,"Non-ASCII or control byte in ISO name");
            name[i]=(char)record[33+i];
        }
        name[i]=0;
        if(!safe_path(name) || strchr(name,'/'))return fail(d,"Unsafe ISO file name");
        return 1;
    }
    return 0;
}

typedef struct {
    JgrDiscFile ancestors[DISC_DIRECTORY_DEPTH+1];
    char path[DISC_PATH_BYTES];
    JgrDiscVisit visit;void *user;
} Walk;
static int walk(JgrDisc *d,Walk *w,const JgrDiscFile *directory,unsigned depth,size_t prefix)
{
    uint32_t pos=0;JgrDiscFile child;unsigned flags,i;char name[256];int result;
    if(depth>DISC_DIRECTORY_DEPTH)return fail(d,"ISO directories are too deep");
    for(i=0;i<depth;i++)if(w->ancestors[i].offset==directory->offset && w->ancestors[i].size==directory->size)
        return fail(d,"Cyclic ISO directory");
    w->ancestors[depth]=*directory;
    while(1) {
        uint32_t before=pos,prior=0;JgrDiscFile ignored;unsigned prior_flags;char previous[256];
        result=next_entry(d,directory,&pos,name,&child,&flags);if(result<=0)return result;
        /* Reject case-insensitive duplicate names without a disc-sized index. */
        while(prior<before) {
            int found=next_entry(d,directory,&prior,previous,&ignored,&prior_flags);
            if(found<0)return -1;
            if(!found || prior>before)break;
            if(equal_name(name,previous))return fail(d,"Duplicate ISO file name");
        }
        if(prefix+strlen(name)+2>sizeof w->path)return fail(d,"ISO path is too long");
        strcpy(w->path+prefix,name);
        if(flags&2) {
            size_t n=strlen(w->path);w->path[n]='/';w->path[n+1]=0;
            if(walk(d,w,&child,depth+1,n+1))return -1;
        }else if(w->visit && w->visit(w->path,&child,w->user))return fail(d,"ISO visitor stopped");
        w->path[prefix]=0;
    }
}
int jgr_disc_visit(JgrDisc *d,JgrDiscVisit visit,void *user)
{
    Walk w;if(!d)return -1;memset(&w,0,sizeof w);w.visit=visit;w.user=user;
    return walk(d,&w,&d->root,0,0);
}
int jgr_disc_find(JgrDisc *d,const char *path,JgrDiscFile *out)
{
    JgrDiscFile directory;const char *part=path;
    if(!d || !out)return -1;
    if(!safe_path(path) || strlen(path)>=DISC_PATH_BYTES)return fail(d,"Unsafe source path");
    for(unsigned i=0;i<DISC_FIND_CACHE_WAYS;i++)if(!strcmp(path,d->files[i].path)) {
        d->files[i].age=metadata_touch(d);*out=d->files[i].file;
        DISC_COUNT(d,find_hits,1);return 0;
    }
    directory=d->root;
    while(*part) {
        const char *end=strchr(part,'/');size_t length=end ? (size_t)(end-part) : strlen(part);
        char wanted[256],name[256];uint32_t pos=0;JgrDiscFile found;unsigned flags=0;int result;
        if(length>=sizeof wanted)return fail(d,"Source name is too long");
        memcpy(wanted,part,length);wanted[length]=0;
        do {result=next_entry(d,&directory,&pos,name,&found,&flags);if(result<0)return -1;}
        while(result && !equal_name(wanted,name));
        if(!result)return fail(d,"Missing disc file: %.140s",path);
        if(end) {if(!(flags&2))return fail(d,"Source parent is not a directory");directory=found;part=end+1;}
        else {
            if(flags&2)return fail(d,"Source is a directory");
            *out=found;
            /* Long paths remain supported without growing the bounded cache. */
            if(strlen(path)<DISC_FIND_NAME_BYTES) {
                unsigned slot=0;
                for(unsigned i=0;i<DISC_FIND_CACHE_WAYS;i++) {
                    if(!d->files[i].path[0]){slot=i;break;}
                    if(d->files[i].age<d->files[slot].age)slot=i;
                }
                d->files[slot].file=found;strcpy(d->files[slot].path,path);
                d->files[slot].age=metadata_touch(d);
            }
            return 0;
        }
    }
    return fail(d,"Empty source path");
}

/* Disc contents are immutable for the lifetime of a handle, like the sector
 * cache. Indexes contain only validated offsets/sizes read from that disc.
 * Evict before allocation to bound even transient memory; allocation failure
 * or an oversized archive falls back to the original streaming validation. */
static unsigned char *afs_reserve(JgrDisc *d,uint32_t bytes,DiscAfsIndex **target)
{
    unsigned slot=0;
    if(bytes>DISC_AFS_CACHE_BYTES)return NULL;
    for(unsigned i=0;i<DISC_AFS_CACHE_WAYS;i++) {
        if(!d->archives[i].rows){slot=i;break;}
        if(d->archives[i].age<d->archives[slot].age)slot=i;
    }
    if(d->archives[slot].rows) {
        d->archive_bytes-=d->archives[slot].count*8;
        free(d->archives[slot].rows);memset(&d->archives[slot],0,sizeof d->archives[slot]);
    }
    while(d->archive_bytes+bytes>DISC_AFS_CACHE_BYTES) {
        unsigned oldest=DISC_AFS_CACHE_WAYS;
        for(unsigned i=0;i<DISC_AFS_CACHE_WAYS;i++)
            if(d->archives[i].rows && (oldest==DISC_AFS_CACHE_WAYS || d->archives[i].age<d->archives[oldest].age))oldest=i;
        d->archive_bytes-=d->archives[oldest].count*8;
        free(d->archives[oldest].rows);memset(&d->archives[oldest],0,sizeof d->archives[oldest]);
    }
    *target=&d->archives[slot];
    return (unsigned char *)malloc(bytes);
}
static int afs_entry(JgrDisc *d,const JgrDiscFile *archive,unsigned index,JgrDiscFile *out,char original[33])
{
    unsigned char header[8],row[8],*rows=NULL;uint32_t count,i,start,size;uint64_t table_end;
    DiscAfsIndex *cached=NULL,*target=NULL;JgrDiscFile selected={0,0};
    for(i=0;i<DISC_AFS_CACHE_WAYS;i++) {
        DiscAfsIndex *entry=&d->archives[i];
        if(entry->rows && entry->file.offset==archive->offset && entry->file.size==archive->size){cached=entry;break;}
    }
    if(cached) {
        count=cached->count;table_end=8+(uint64_t)count*8;
        if(index>=count)return fail(d,"Invalid AFS index/table");
        cached->age=metadata_touch(d);DISC_COUNT(d,afs_hits,1);
        selected.offset=archive->offset+le32(cached->rows+index*8);
        selected.size=le32(cached->rows+index*8+4);
    } else {
        if(jgr_disc_read(d,archive,0,header,8))return -1;
        if(memcmp(header,"AFS\0",4) && memcmp(header,"AFS ",4))return fail(d,"Invalid AFS header");
        count=le32(header+4);table_end=8+(uint64_t)count*8;
        if(count>100000 || table_end>archive->size || index>=count)return fail(d,"Invalid AFS index/table");
        rows=afs_reserve(d,count*8,&target);
        if(rows && jgr_disc_read(d,archive,8,rows,count*8)){free(rows);return -1;}
        DISC_COUNT(d,afs_validations,1);
        for(i=0;i<count;i++) {
            const unsigned char *entry=rows ? rows+i*8 : row;
            if(!rows && jgr_disc_read(d,archive,8+i*8,row,8))return -1;
            start=le32(entry);size=le32(entry+4);DISC_COUNT(d,afs_rows,1);
            if((uint64_t)start+size>archive->size){free(rows);return fail(d,"AFS extent exceeds archive");}
            if(i==index){selected.offset=archive->offset+start;selected.size=size;}
        }
        /* Never publish a partially validated archive, including when the
         * requested row precedes a malformed, otherwise unused extent. */
        if(rows) {
            target->file=*archive;target->rows=rows;target->count=count;
            target->age=metadata_touch(d);d->archive_bytes+=count*8;cached=target;
        }
    }
    if(original) {
        uint32_t name_offset=0;original[0]=0;
        if(cached && cached->name_known)name_offset=cached->name_offset;
        else {
            uint32_t positions[2]={(uint32_t)table_end,0x7fff8};
            for(i=0;i<2;i++) {
                if((uint64_t)positions[i]+8>archive->size)continue;
                if(jgr_disc_read(d,archive,positions[i],row,8))return -1;
                start=le32(row);size=le32(row+4);
                if(start && (uint64_t)start+size<=archive->size && size>=(uint64_t)count*48){name_offset=start;break;}
            }
            if(cached){cached->name_offset=name_offset;cached->name_known=1;}
        }
        if(name_offset) {
            unsigned char name[32];unsigned n;
            if(jgr_disc_read(d,archive,name_offset+index*48,name,32))return -1;
            for(n=0;n<32 && name[n];n++)original[n]=(char)name[n];
            original[n]=0;
            if(n && (!safe_path(original) || strchr(original,'/')))return fail(d,"Unsafe AFS file name");
        }
    }
    *out=selected;
    return 0;
}
int jgr_disc_afs(JgrDisc *d,const JgrDiscFile *archive,unsigned index,JgrDiscFile *out)
{if(!d || !archive || !out)return -1;return afs_entry(d,archive,index,out,NULL);}
int jgr_disc_afs_named(JgrDisc *d,const JgrDiscFile *archive,unsigned index,JgrDiscFile *out,char original[33])
{if(!d || !archive || !out || !original)return -1;return afs_entry(d,archive,index,out,original);}
int jgr_disc_source(JgrDisc *d,const char *source,JgrDiscFile *out)
{
    static const char extracted[]="decompile/extracted/",afs[]="decompile/afs/";
    const char *part,*slash,*name,*tail;char archive[DISC_PATH_BYTES],original[33];
    unsigned long index;JgrDiscFile file;size_t length;
    if(!d || !out || !source)return -1;
    if(!strncmp(source,extracted,sizeof extracted-1))return jgr_disc_find(d,source+sizeof extracted-1,out);
    if(strncmp(source,afs,sizeof afs-1) || !safe_path(source))return fail(d,"Unsupported source namespace");
    part=source+sizeof afs-1;slash=strchr(part,'/');
    if(!slash || strchr(slash+1,'/'))return fail(d,"Malformed AFS source path");
    length=(size_t)(slash-part);if(!length || length+16>=sizeof archive)return fail(d,"AFS source name is too long");
    snprintf(archive,sizeof archive,"JETRADIO/%.*s.AFS",(int)length,part);
    name=slash+1;if(!isdigit((unsigned char)*name))return fail(d,"Missing AFS source index");
    index=strtoul(name,(char **)&tail,10);
    if(index>=100000 || (tail-name)<3 || (*tail && *tail!='_'))return fail(d,"Malformed AFS source index");
    if(jgr_disc_find(d,archive,&file) || afs_entry(d,&file,(unsigned)index,out,original))return -1;
    if((*tail=='_' && strcmp(tail+1,original)) || (!*tail && *original))return fail(d,"AFS source name does not match index");
    return 0;
}

static int line(FILE *f,char *out,size_t capacity)
{
    size_t n;
    while(fgets(out,(int)capacity,f)) {
        n=strlen(out);if(n && out[n-1]!='\n' && !feof(f))return -1;
        while(n && isspace((unsigned char)out[n-1]))out[--n]=0;
        if(n)return 1;
    }
    return ferror(f) ? -1 : 0;
}
static char *token(char **input)
{
    char *p=*input,*start;int quote=0;
    while(isspace((unsigned char)*p))p++;
    if(!*p){*input=p;return NULL;}
    if(*p=='\'' || *p=='"')quote=*p++;
    start=p;while(*p && (quote ? *p!=quote : !isspace((unsigned char)*p)))p++;
    if(quote && *p!=quote)return NULL;
    if(*p){*p++=0;if(quote && *p && !isspace((unsigned char)*p))return NULL;}
    *input=p;return start;
}
static int number(const char *text,uint32_t *out)
{
    uint64_t n=0;const unsigned char *p=(const unsigned char *)text;
    if(!p || !*p)return -1;
    while(*p){if(*p<'0' || *p>'9')return -1;n=n*10+(*p++-'0');if(n>UINT32_MAX)return -1;}
    *out=(uint32_t)n;return 0;
}
static int track_path(const char *base,const char *relative,char out[DISC_PATH_BYTES])
{
    if(!safe_path(relative) || strlen(base)+strlen(relative)+1>DISC_PATH_BYTES)return -1;
    strcpy(out,base);strcat(out,relative);
#ifndef __PSP__
    /* PSP FAT has no symlinks; host tests reject aliases escaping the GDI's
     * directory just as the computer-side importer does. */
    {
        char *root=realpath(*base ? base : ".",NULL),*resolved=realpath(out,NULL);int bad=1;
        if(root && resolved){size_t n=strlen(root);bad=strncmp(root,resolved,n) || (n>1 && resolved[n]!='/');}
        free(root);free(resolved);if(bad)return -1;
    }
#endif
    return 0;
}
static int open_disc(JgrDisc **out,const char *gdi,char *error,size_t error_size,int verify)
{
    JgrDisc *d=NULL;FILE *input=NULL;char buffer[DISC_PATH_BYTES],base[DISC_PATH_BYTES],selected[DISC_PATH_BYTES];
    uint32_t count=0,i,last=0,chosen=0;unsigned char boot[256],pvd[2048];unsigned flags;
    int status=-1;
    if(out)*out=NULL;
    if(!out || !gdi || strlen(gdi)>=sizeof base){if(error && error_size)snprintf(error,error_size,"Invalid GDI path");return -1;}
    d=(JgrDisc *)calloc(1,sizeof *d);
    if(!d){if(error && error_size)snprintf(error,error_size,"Not enough memory for disc reader");return -1;}
    strcpy(base,gdi);{char *end=strrchr(base,'/');if(end)end[1]=0;else base[0]=0;}
    input=fopen(gdi,"rb");if(!input){fail(d,"Cannot open GDI");goto done;}
    if(line(input,buffer,sizeof buffer)!=1){fail(d,"Missing GDI track count");goto done;}
    {char *start=buffer;if(!memcmp(start,"\xef\xbb\xbf",3))start+=3;
     if(number(start,&count) || !count || count>99){fail(d,"Invalid GDI track count");goto done;}}
    for(i=0;i<count;i++) {
        char *fields[6],*cursor=buffer,path[DISC_PATH_BYTES];uint32_t n,lba,kind,sector,offset;struct stat st;unsigned j;
        if(line(input,buffer,sizeof buffer)!=1){fail(d,"Missing GDI track");goto done;}
        for(j=0;j<6;j++)fields[j]=token(&cursor);
        while(isspace((unsigned char)*cursor))cursor++;
        if(*cursor || number(fields[0],&n) || number(fields[1],&lba) || number(fields[2],&kind) ||
           number(fields[3],&sector) || !fields[4] || number(fields[5],&offset) || n!=i+1 ||
           (i && lba<=last) || (kind!=0 && kind!=4) || (sector!=2048 && sector!=2352)) {
            fail(d,"Unsupported GDI track metadata");goto done;
        }
        last=lba;
        if(track_path(base,fields[4],path) || stat(path,&st) || !S_ISREG(st.st_mode) ||
           st.st_size<0 || st.st_size>INT32_MAX || (uint64_t)st.st_size<=offset ||
           ((uint64_t)st.st_size-offset)%sector) {
            fail(d,"Missing, unsafe, oversized or unaligned GDI track");goto done;
        }
        if(kind==4 && lba>=45000) {
            chosen++;strcpy(selected,path);d->lba=lba;d->sector_size=sector;
            d->track_offset=offset;d->sectors=(uint32_t)((st.st_size-offset)/sector);
        }
    }
    if(line(input,buffer,sizeof buffer)!=0 || chosen!=1){fail(d,"Expected one high-density track and exact track count");goto done;}
    fclose(input);input=NULL;
    d->stream=fopen(selected,"rb");if(!d->stream){fail(d,"Cannot open data track");goto done;}
    strcpy(d->track_path,selected);
    if(fstat(fileno(d->stream),&d->track_identity)){fail(d,"Cannot identify opened data track");goto done;}
    /* The reader already batches physical sectors in its own 147 KiB cache.
     * Avoid newlib's small buffer and read-ahead seeks on the Memory Stick. */
    (void)setvbuf(d->stream,NULL,_IONBF,0);
    if(read_logical(d,0,boot,sizeof boot) || read_logical(d,16*2048,pvd,sizeof pvd))goto done;
    if(memcmp(pvd,"\1CD001\1",7) || pvd[128]!=0 || pvd[129]!=8 || pvd[130]!=8 || pvd[131]!=0) {
        fail(d,"Unsupported ISO9660 volume");goto done;
    }
    if(iso_extent(d,pvd+156,pvd[156],&d->root,&flags) || !(flags&2)) {fail(d,"Invalid ISO root directory");goto done;}
    if(jgr_disc_visit(d,NULL,NULL))goto done;
    if(verify) {
        static const char expected[]="8b294765225fc32020dd59010464a730b710d0bd79f69c79f16218cc4feac6a6";
        JgrDiscFile executable;JgrSha256 sha;unsigned char digest[32],*chunk;char hex[65];uint32_t position=0;
        if(memcmp(boot,"SEGA SEGAKATANA ",16) || memcmp(boot+64,"MK-51058  ",10) || memcmp(boot+74,"V1.005",6)) {
            fail(d,"Unsupported game revision; expected USA V1.005");goto done;
        }
        if(jgr_disc_find(d,"1ST_READ.BIN",&executable))goto done;
        if(executable.size!=3075648){fail(d,"Unsupported executable size");goto done;}
        chunk=(unsigned char *)malloc(32768);
        if(!chunk){fail(d,"Not enough memory to verify disc");goto done;}
        jgr_sha256_init(&sha);
        while(position<executable.size) {
            uint32_t amount=executable.size-position;if(amount>32768)amount=32768;
            if(jgr_disc_read(d,&executable,position,chunk,amount)){free(chunk);goto done;}
            jgr_sha256_update(&sha,chunk,amount);position+=amount;
        }
        free(chunk);jgr_sha256_final(&sha,digest);jgr_sha256_hex(digest,hex);
        if(strcmp(hex,expected)){fail(d,"Unsupported executable SHA-256");goto done;}
    }
    status=0;*out=d;
done:
    if(input)fclose(input);
    if(status){if(error && error_size)snprintf(error,error_size,"%s",d->error);jgr_disc_close(d);}
    else if(error && error_size)*error=0;
    return status;
}
int jgr_disc_open(JgrDisc **out,const char *gdi,char *error,size_t error_size)
{return open_disc(out,gdi,error,error_size,1);}
#ifdef JGR_DISC_TESTING
int jgr_disc_open_fixture(JgrDisc **out,const char *gdi,char *error,size_t error_size)
{return open_disc(out,gdi,error,error_size,0);}
#endif
