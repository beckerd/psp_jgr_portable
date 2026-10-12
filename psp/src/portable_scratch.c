/* Setup-only, bounded RAM streams with a small cache of on-card spill handles.
 * No payload is retained after conversion or linked into game execution. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#include "portable_scratch.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef __PSP__
#include <malloc.h>
#endif
#ifdef __APPLE__
/* X/Open-only host test builds hide this BSD declaration, although the
 * function remains available. Match Darwin's public callback ABI. */
extern FILE *funopen(const void *,int (*)(void *,char *,int),
    int (*)(void *,const char *,int),fpos_t (*)(void *,fpos_t,int),int (*)(void *));
#endif
#if defined(__linux__) || defined(__EMSCRIPTEN__)
#include <sys/types.h>
/* Keep the cookie ABI available when a force-included portability header has
 * already included stdio before this file can set feature-test macros. */
typedef struct {
    ssize_t (*read)(void *,char *,size_t);
    ssize_t (*write)(void *,const char *,size_t);
    int (*seek)(void *,int64_t *,int);
    int (*close)(void *);
} JgrCookieFunctions;
extern FILE *jgr_cookie_open(void *,const char *,JgrCookieFunctions) __asm__("fopencookie");
#endif

typedef struct Scratch {
    JgrScratchPool *pool;
    struct Scratch *next;
    FILE *stream;
    unsigned char *data;
    size_t capacity,size,position,disk_position;
    int disk,backed,discard;
    unsigned views;
    char path[];
} Scratch;

#define SCRATCH_PAGE_BYTES 4096u
#define SCRATCH_PAGE_COUNT 16u
typedef struct {
    Scratch *owner;
    size_t at,count;
    uint32_t age;
    unsigned char data[SCRATCH_PAGE_BYTES];
} ScratchPage;
typedef struct {ScratchPage page[SCRATCH_PAGE_COUNT];uint32_t clock;} ScratchCache;

static void invalidate(Scratch *s,size_t begin,size_t end)
{
    ScratchCache *cache=s->pool->read_cache;if(!cache)return;
    for(unsigned i=0;i<SCRATCH_PAGE_COUNT;i++){
        ScratchPage *page=cache->page+i;
        if(page->owner==s && ((begin==0 && end==SIZE_MAX) ||
           (begin<page->at+page->count && end>page->at)))page->owner=NULL;
    }
}

void jgr_scratch_init(JgrScratchPool *p,size_t budget)
{memset(p,0,sizeof *p);p->budget=budget>INT_MAX?INT_MAX:budget;}
int jgr_scratch_reclaim_begin(JgrScratchPool *p,JgrScratchReclaim *scope,void (*release)(void *),void *context)
{
    if(!p || !scope || scope->pool || !release || p->reclaiming){errno=EINVAL;return -1;}
    scope->pool=p;scope->previous=p->reclaim;scope->release=release;scope->context=context;p->reclaim=scope;return 0;
}
int jgr_scratch_reclaim_end(JgrScratchReclaim *scope)
{
    if(!scope){errno=EINVAL;return -1;}
    if(!scope->pool)return 0;
    if(scope->pool->reclaim!=scope){errno=EBUSY;return -1;}
    scope->pool->reclaim=scope->previous;memset(scope,0,sizeof *scope);return 0;
}
static int reclaim_optional(JgrScratchPool *p)
{
    JgrScratchReclaim *scope=p->reclaim;if(!scope || p->reclaiming)return 0;
    void (*release)(void *)=scope->release;void *context=scope->context;
    int saved=errno;(void)jgr_scratch_reclaim_end(scope);p->reclaiming=1;
    release(context);p->reclaiming=0;errno=saved;return 1;
}
static JgrScratchPool shared_pool;
static unsigned shared_scopes;
static size_t shared_budget=JGR_SCRATCH_DEFAULT_BUDGET,shared_headroom;
#ifdef __PSP__
static size_t known_arena;
static int heap_probed;
/* Keep these calls opaque to allocation elision: the only observable result
 * of the temporary allocation is the allocator's subsequently queried arena. */
static void *(*volatile heap_allocate)(size_t)=malloc;
static void (*volatile heap_release)(void *)=free;
static void probe_heap(void)
{
    if(heap_probed)return;
    heap_probed=1;int saved=errno;
    /* _sbrk reserves PSP's program heap once, but mallinfo only reports the
     * portion already handed to malloc. Prove a reusable lower bound without
     * touching payload memory or depending on private _sbrk/kernel symbols.
     * The successful arena high-water mark remains valid after malloc trims. */
    for(size_t n=24u*1024u*1024u;n>=1024u*1024u;n-=1024u*1024u){
        struct mallinfo info=mallinfo();if(info.arena>known_arena)known_arena=info.arena;
        if(n<=info.fordblks)break;
        void *probe=heap_allocate(n);
        if(probe){info=mallinfo();if(info.arena>known_arena)known_arena=info.arena;heap_release(probe);break;}
    }
    errno=saved;
}
#endif
size_t jgr_scratch_heap_available(void)
{
#ifdef __PSP__
    struct mallinfo info=mallinfo();if(info.arena>known_arena)known_arena=info.arena;
    return info.fordblks+(known_arena-info.arena);
#else
    return SIZE_MAX;
#endif
}
int jgr_scratch_setup_phase(unsigned phase)
{
    if(shared_scopes || shared_pool.streams){errno=EBUSY;return -1;}
    if(phase>4){errno=EINVAL;return -1;}
    shared_budget=phase==3?JGR_SCRATCH_DEFAULT_BUDGET:18u*1024u*1024u;
    shared_headroom=phase==3?0:6u*1024u*1024u;return 0;
}
JgrScratchPool *jgr_scratch_begin(void)
{
    if(!shared_scopes){
        if(shared_pool.streams){errno=EBUSY;return NULL;}
        jgr_scratch_init(&shared_pool,shared_budget);shared_pool.headroom=shared_headroom;
#ifdef __PSP__
        if(shared_headroom)probe_heap();
#endif
    }
    shared_scopes++;return &shared_pool;
}
int jgr_scratch_end(void)
{
    if(!shared_scopes){errno=EINVAL;return -1;}
    if(--shared_scopes==0 && shared_pool.streams){errno=EBUSY;return -1;}
    return 0;
}
static int close_slot(JgrScratchPool *p,unsigned slot)
{
    FILE *file=p->file[slot];p->file[slot]=NULL;p->owner[slot]=NULL;p->age[slot]=0;
    return file?fclose(file):0;
}
static int disk_path(const char *path)
{
    struct stat st;
#ifdef __PSP__
    int result=stat(path,&st);
#else
    int result=lstat(path,&st);
#endif
    if(result)return errno==ENOENT?0:-1;
    if(!S_ISREG(st.st_mode)){errno=EINVAL;return -1;}
    return 0;
}
static FILE *disk_file(Scratch *s,int create)
{
    JgrScratchPool *p=s->pool;unsigned slot=JGR_SCRATCH_DISK_FILES;
    for(unsigned i=0;i<JGR_SCRATCH_DISK_FILES;i++)if(p->owner[i]==s){slot=i;break;}
    if(slot==JGR_SCRATCH_DISK_FILES){
        slot=0;
        for(unsigned i=0;i<JGR_SCRATCH_DISK_FILES;i++){
            if(!p->file[i]){slot=i;break;}
            if(p->age[i]<p->age[slot])slot=i;
        }
        if(close_slot(p,slot) || disk_path(s->path))return NULL;
        /* Firmware and other setup clients may leave fewer handles than the
         * cache capacity. Evict another idle stream on descriptor exhaustion
         * rather than turning the larger optional cache into a requirement. */
        for(;;){
            p->file[slot]=fopen(s->path,create?"w+b":"r+b");
            if(p->file[slot])break;
            if(errno!=EMFILE && errno!=ENFILE)return NULL;
            unsigned victim=JGR_SCRATCH_DISK_FILES;
            for(unsigned i=0;i<JGR_SCRATCH_DISK_FILES;i++)if(p->file[i] &&
                (victim==JGR_SCRATCH_DISK_FILES || p->age[i]<p->age[victim]))victim=i;
            if(victim==JGR_SCRATCH_DISK_FILES || close_slot(p,victim))return NULL;
        }
        s->backed=1;
        /* The public cookie stream already provides stdio buffering. This
         * inner stream must never hide writes when another cookie is read. */
        (void)setvbuf(p->file[slot],NULL,_IONBF,0);p->owner[slot]=s;p->opens++;s->disk_position=0;
    }
    if(++p->clock==0){memset(p->age,0,sizeof p->age);p->clock=1;}
    p->age[slot]=p->clock;return p->file[slot];
}
static int disk_seek(Scratch *s,FILE *file,size_t position)
{
    if(position==s->disk_position && !feof(file))return 0;
    if(fseek(file,(long)position,SEEK_SET)){s->disk_position=SIZE_MAX;return -1;}
    s->disk_position=position;return 0;
}
static size_t disk_read(Scratch *s,FILE *file,void *data,size_t count)
{
    size_t n=fread(data,1,count,file);s->disk_position+=n;return n;
}
static int disk_write(Scratch *s,FILE *file,const void *data,size_t count)
{
    size_t n=fwrite(data,1,count,file);s->disk_position+=n;return n==count?0:-1;
}
static int spill(Scratch *s)
{
    FILE *file=disk_file(s,1);if(!file || disk_seek(s,file,0))return -1;
    if(s->size && disk_write(s,file,s->data,s->size))return -1;
    s->pool->allocated-=s->capacity;free(s->data);s->data=NULL;s->capacity=0;
    s->disk=1;s->pool->spills++;return 0;
}
static int grow(Scratch *s,size_t needed)
{
    if(s->disk || needed<=s->capacity)return 0;
    JgrScratchPool *p=s->pool;size_t available;
    for(;;){
        available=p->budget-p->allocated+s->capacity;
#ifdef __PSP__
        /* Count both free malloc chunks and the proven reusable part of the
         * program heap. Kernel free-memory counters exclude that whole heap. */
        if(p->headroom){
            size_t free_bytes=jgr_scratch_heap_available();
            size_t growth=free_bytes>p->headroom?free_bytes-p->headroom:0;
            if(available>s->capacity+growth)available=s->capacity+growth;
        }
#endif
        if(needed<=available)break;
        /* Release optional source snapshots before sacrificing resident
         * output. This also covers growth within one large geometry row. */
        if(reclaim_optional(p))continue;
        /* Keep small, frequently indexed tables resident. Spill the largest
         * unpinned stream, which is normally a vertex/pixel/raw output bank. */
        Scratch *victim=s;
        for(Scratch *candidate=p->first_stream;candidate;candidate=candidate->next)
            if(!candidate->disk && !candidate->views && candidate->capacity>victim->capacity)victim=candidate;
        if(victim==s || !victim->capacity)return spill(s);
        if(spill(victim))return -1;
    }
    size_t capacity=s->capacity?s->capacity:4096;
    /* Large banks grow by 64 KiB instead of doubling into a mostly empty
     * multi-megabyte allocation. Small indexed tables retain geometric growth. */
    if(needed>1024u*1024u)capacity=(needed+65535u)&~(size_t)65535u;
    else while(capacity<needed && capacity<=(size_t)INT_MAX/2)capacity*=2;
    if(capacity<needed)capacity=needed;
    if(capacity>available)capacity=available;
    unsigned char *data=realloc(s->data,capacity);
    while(!data && reclaim_optional(p))data=realloc(s->data,capacity);
    if(!data)return spill(s);
    p->allocated+=capacity-s->capacity;if(p->allocated>p->peak)p->peak=p->allocated;
    s->data=data;s->capacity=capacity;return 0;
}
static int cached_read(Scratch *s,char *data,size_t count,ScratchCache *cache)
{
    ScratchPage *page=NULL;
    for(unsigned i=0;i<SCRATCH_PAGE_COUNT;i++){
        ScratchPage *candidate=cache->page+i;
        if(candidate->owner==s && s->position>=candidate->at &&
           s->position-candidate->at<candidate->count){page=candidate;break;}
    }
    if(!page){
        page=cache->page;
        for(unsigned i=0;i<SCRATCH_PAGE_COUNT;i++){
            ScratchPage *candidate=cache->page+i;
            if(!candidate->owner){page=candidate;break;}
            if(candidate->age<page->age)page=candidate;
        }
        page->owner=NULL;
        size_t at=s->position&~(size_t)(SCRATCH_PAGE_BYTES-1u),n=s->size-at;
        if(n>SCRATCH_PAGE_BYTES)n=SCRATCH_PAGE_BYTES;
        FILE *file=disk_file(s,0);if(!file || disk_seek(s,file,at))return -1;
        size_t got=disk_read(s,file,page->data,n);
        page->at=at;page->count=got;page->owner=s;
        if(got<=s->position-at)return ferror(file)?-1:0;
    }
    size_t available=page->count-(s->position-page->at);if(count>available)count=available;
    memcpy(data,page->data+(s->position-page->at),count);s->position+=count;
    if(++cache->clock==0){for(unsigned i=0;i<SCRATCH_PAGE_COUNT;i++)cache->page[i].age=0;cache->clock=1;}
    page->age=cache->clock;return (int)count;
}
static int scratch_read(void *cookie,char *data,int bytes)
{
    Scratch *s=cookie;if(bytes<0){errno=EINVAL;return -1;}
    size_t count=(size_t)bytes;if(s->position>=s->size)return 0;
    if(!count)return 0;
    if(count>s->size-s->position)count=s->size-s->position;
    if(s->disk){
        /* Small indexed reads otherwise reread a stdio buffer after every
         * seek. Cache their backing pages across all streams, independently
         * of the four open spill handles. Bulk reads bypass this cache. */
        if(count<=SCRATCH_PAGE_BYTES){
            if(!s->pool->read_cache && !s->pool->read_cache_failed){
                int saved=errno;s->pool->read_cache=calloc(1,sizeof(ScratchCache));
                if(!s->pool->read_cache)s->pool->read_cache_failed=1;
                errno=saved;
            }
            if(s->pool->read_cache)return cached_read(s,data,count,s->pool->read_cache);
        }
        FILE *file=disk_file(s,0);if(!file || disk_seek(s,file,s->position))return -1;
        size_t read=disk_read(s,file,data,count);s->position+=read;
        return read?(int)read:ferror(file)?-1:0;
    }
    if(count)memcpy(data,s->data+s->position,count);
    s->position+=count;return (int)count;
}
static int scratch_write(void *cookie,const char *data,int bytes)
{
    Scratch *s=cookie;if(bytes<0 || (size_t)bytes>(size_t)INT_MAX-s->position){errno=EFBIG;return -1;}
    if(s->views){errno=EBUSY;return -1;}
    size_t count=(size_t)bytes,end=s->position+count;if(!count)return 0;
    if(grow(s,end))return -1;
    invalidate(s,s->position,end);
    if(s->disk){
        FILE *file=disk_file(s,0);if(!file)return -1;
        if(s->position>s->size){
            unsigned char zero[1024]={0};size_t gap=s->position-s->size;
            if(disk_seek(s,file,s->size))return -1;
            while(gap){size_t n=gap<sizeof zero?gap:sizeof zero;if(disk_write(s,file,zero,n))return -1;gap-=n;}
        }else if(disk_seek(s,file,s->position))return -1;
        if(disk_write(s,file,data,count))return -1;
    }else{
        if(s->position>s->size)memset(s->data+s->size,0,s->position-s->size);
        memcpy(s->data+s->position,data,count);
    }
    s->position=end;if(end>s->size)s->size=end;return bytes;
}
static int64_t scratch_seek(void *cookie,int64_t offset,int whence)
{
    Scratch *s=cookie;int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?(int64_t)s->position:whence==SEEK_END?(int64_t)s->size:-1;
    if(base<0 || offset<-base || offset>INT_MAX-base){errno=EINVAL;return -1;}
    s->position=(size_t)(base+offset);return (int64_t)s->position;
}
static int scratch_close(void *cookie)
{
    Scratch *s=cookie;int result=0,error=0;
    invalidate(s,0,SIZE_MAX);
    for(unsigned i=0;i<JGR_SCRATCH_DISK_FILES;i++)if(s->pool->owner[i]==s){result=close_slot(s->pool,i);error=errno;break;}
    if(s->discard && s->backed && remove(s->path) && errno!=ENOENT && !result){result=-1;error=errno;}
    Scratch *previous=NULL,*current=s->pool->first_stream;
    while(current && current!=s){previous=current;current=current->next;}
    if(current){if(previous)previous->next=s->next;else s->pool->first_stream=s->next;}
    s->pool->allocated-=s->capacity;
    if(--s->pool->streams==0){free(s->pool->read_cache);s->pool->read_cache=NULL;s->pool->read_cache_failed=0;}
    free(s->data);free(s);if(result)errno=error;return result;
}
static Scratch *find_stream(JgrScratchPool *pool,FILE *file)
{
    if(pool && file)for(Scratch *s=pool->first_stream;s;s=s->next)if(s->stream==file)return s;
    errno=EINVAL;return NULL;
}
int jgr_scratch_discard(JgrScratchPool *pool,FILE *file)
{
    Scratch *s=find_stream(pool,file);if(!s)return -1;
    if(s->views){errno=EBUSY;return -1;}
    /* fclose can perform the first spill while flushing its public buffer.
     * Let the close callback inspect ownership after the last flush. */
    s->discard=1;
    int result=fflush(file),error=errno;
    if(fclose(file) && !result){result=-1;error=errno;}
    if(result)errno=error;
    return result;
}
int jgr_scratch_reset(JgrScratchPool *pool,FILE *file)
{
    Scratch *s=find_stream(pool,file);if(!s)return -1;
    if(s->views){errno=EBUSY;return -1;}
    if(fflush(file) || fseek(file,0,SEEK_SET))return -1;
    invalidate(s,0,SIZE_MAX);
    for(unsigned i=0;i<JGR_SCRATCH_DISK_FILES;i++)if(pool->owner[i]==s && close_slot(pool,i))return -1;
    pool->allocated-=s->capacity;free(s->data);s->data=NULL;
    s->capacity=s->size=s->position=s->disk_position=0;s->disk=0;clearerr(file);return 0;
}
const void *jgr_scratch_view(JgrScratchPool *pool,FILE *file,size_t *size)
{
    Scratch *s=find_stream(pool,file);if(!s || !size){errno=EINVAL;return NULL;}
    *size=0;if(fflush(file))return NULL;
    if(s->disk || !s->data){errno=0;return NULL;}
    s->views++;*size=s->size;return s->data;
}
void jgr_scratch_unview(JgrScratchPool *pool,FILE *file)
{Scratch *s=find_stream(pool,file);if(s && s->views)s->views--;}
#if defined(__linux__) || defined(__EMSCRIPTEN__)
static ssize_t cookie_read(void *s,char *p,size_t n){return scratch_read(s,p,n>INT_MAX?INT_MAX:(int)n);}
static ssize_t cookie_write(void *s,const char *p,size_t n)
{
    int result=scratch_write(s,p,n>INT_MAX?INT_MAX:(int)n);
#ifdef __EMSCRIPTEN__
    /* Musl marks a cookie stream failed only for a negative write result. */
    return result;
#else
    return result<0?0:result;
#endif
}
static int cookie_seek(void *s,int64_t *offset,int whence)
{int64_t result=scratch_seek(s,*offset,whence);if(result<0)return -1;*offset=result;return 0;}
#else
static fpos_t cookie_seek(void *s,fpos_t offset,int whence)
{return (fpos_t)scratch_seek(s,(int64_t)offset,whence);}
#endif
FILE *jgr_scratch_open(JgrScratchPool *pool,const char *path)
{
    if(!pool || !path || !*path || strlen(path)>1023){errno=EINVAL;return NULL;}
    Scratch *s=calloc(1,sizeof *s+strlen(path)+1);if(!s)return NULL;
    s->pool=pool;strcpy(s->path,path);
#if defined(__linux__) || defined(__EMSCRIPTEN__)
    JgrCookieFunctions functions={cookie_read,cookie_write,cookie_seek,scratch_close};
    FILE *file=jgr_cookie_open(s,"w+",functions);
#else
    FILE *file=funopen(s,scratch_read,scratch_write,cookie_seek,scratch_close);
#endif
    if(!file)free(s);else {s->stream=file;s->next=pool->first_stream;pool->first_stream=s;pool->streams++;}
    return file;
}
