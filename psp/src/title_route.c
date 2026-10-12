#include "title_route.h"
#include "hang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#if defined(__PSP__) || defined(JGR_TITLE_ROUTE_TEST_ASYNC)
#define TITLE_ROUTE_ASYNC 1
#include <unistd.h>
#ifdef __PSP__
#include <pspkernel.h>
#else
#include <pthread.h>
#include <time.h>
#endif
#else
#define TITLE_ROUTE_ASYNC 0
#endif
static unsigned word(const unsigned char *p){unsigned x;memcpy(&x,p,4);return x;}
static float real(const unsigned char *p){float x;memcpy(&x,p,4);return x;}
static int angle(const unsigned char *p){int16_t x;memcpy(&x,p,2);return x;}
/* Synchronous fallback retains the original 65-frame cache. PSP playback
   instead splits 66 frames into two 33-frame buffers: one second of samples
   plus interpolation overlap each. Only ONE extra frame per actor, not a
   second 30 KiB allocation. Validate once on entry; never allocate per tick. */
#define CACHE_STEP 64u
#define CACHE_FRAMES (CACHE_STEP+1u)
#define PREFETCH_STEP 32u
#define PREFETCH_FRAMES (PREFETCH_STEP+1u)
#if TITLE_ROUTE_ASYNC
enum { ROUTE_IDLE,ROUTE_QUEUED,ROUTE_READING,ROUTE_READY,ROUTE_FAILED };
struct TitleRoutePrefetch {
    struct TitleRoutePrefetch *next;
    FILE *file;
    unsigned char *buffer;
    char path[512];
    unsigned first,frames,stride,offset;
    int state;
};
static struct TitleRoutePrefetch *route_jobs;
static int route_quit,route_worker_active;
#ifdef __PSP__
static SceUID route_lock_id=-1,route_work_id=-1,route_thread_id=-1;
static void route_lock(void){sceKernelWaitSema(route_lock_id,1,NULL);}
static void route_unlock(void){sceKernelSignalSema(route_lock_id,1);}
static void route_wake(void){sceKernelSignalSema(route_work_id,1);}
static void route_pause(void){sceKernelDelayThread(1000);}
#else
static pthread_mutex_t route_mutex;
static pthread_cond_t route_condition;
static pthread_t route_thread;
static unsigned route_wakes;
static void route_lock(void){pthread_mutex_lock(&route_mutex);}
static void route_unlock(void){pthread_mutex_unlock(&route_mutex);}
static void route_wake(void){route_wakes++;pthread_cond_signal(&route_condition);}
static void route_pause(void){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}
#endif
static int route_read(FILE **file,const char *path,unsigned offset,unsigned stride,
                      unsigned first,unsigned frames,unsigned char *buffer)
{
    if(!*file){*file=fopen(path,"rb");if(*file)setvbuf(*file,NULL,_IONBF,0);}
    if(*file && !fseek(*file,(long)offset+(long)first*stride,SEEK_SET) &&
       fread(buffer,stride,frames,*file)==frames)return 1;
    if(*file){fclose(*file);*file=NULL;}return 0;
}
#ifdef __PSP__
static int route_worker(SceSize args,void *argp)
#else
static void *route_worker(void *argp)
#endif
{
    (void)argp;
#ifdef __PSP__
    (void)args;
#endif
    for(;;) {
#ifdef __PSP__
        sceKernelWaitSema(route_work_id,1,NULL);route_lock();
#else
        route_lock();while(!route_wakes && !route_quit)pthread_cond_wait(&route_condition,&route_mutex);
        if(route_wakes)route_wakes--;
#endif
        if(route_quit){route_unlock();break;}
        for(;;) {
            struct TitleRoutePrefetch *job=route_jobs;
            while(job && job->state!=ROUTE_QUEUED)job=job->next;
            if(!job){route_unlock();break;}
            job->state=ROUTE_READING;route_unlock();
            int ok=route_read(&job->file,job->path,job->offset,job->stride,
                              job->first,job->frames,job->buffer);
            route_lock();job->state=ok ? ROUTE_READY:ROUTE_FAILED;
        }
    }
#ifdef __PSP__
    return 0;
#else
    return NULL;
#endif
}
static int route_worker_start(void)
{
    if(route_worker_active)return 1;
    route_quit=0;
#ifdef __PSP__
    route_lock_id=sceKernelCreateSema("route_lock",0,1,1,NULL);
    if(route_lock_id<0)return 0;
    route_work_id=sceKernelCreateSema("route_work",0,0,64,NULL);
    if(route_work_id<0){sceKernelDeleteSema(route_lock_id);route_lock_id=-1;return 0;}
    /* Below the music reader (0x18), above gameplay (0x20). No decoding,
       mixer locks, or ME work here; blocking I/O yields the CPU to main. */
    route_thread_id=sceKernelCreateThread("route_reader",route_worker,0x1c,16384,0,NULL);
    if(route_thread_id<0 || sceKernelStartThread(route_thread_id,0,NULL)<0) {
        if(route_thread_id>=0)sceKernelDeleteThread(route_thread_id);
        sceKernelDeleteSema(route_work_id);sceKernelDeleteSema(route_lock_id);
        route_thread_id=route_work_id=route_lock_id=-1;return 0;
    }
#else
    route_wakes=0;
    if(pthread_mutex_init(&route_mutex,NULL))return 0;
    if(pthread_cond_init(&route_condition,NULL)){pthread_mutex_destroy(&route_mutex);return 0;}
    if(pthread_create(&route_thread,NULL,route_worker,NULL)) {
        pthread_cond_destroy(&route_condition);pthread_mutex_destroy(&route_mutex);return 0;
    }
#endif
    route_worker_active=1;return 1;
}
static void route_worker_stop(void)
{
    route_lock();route_quit=1;route_wake();route_unlock();
#ifdef __PSP__
    sceKernelWaitThreadEnd(route_thread_id,NULL);sceKernelDeleteThread(route_thread_id);
    sceKernelDeleteSema(route_work_id);sceKernelDeleteSema(route_lock_id);
    route_thread_id=route_work_id=route_lock_id=-1;
#else
    pthread_join(route_thread,NULL);pthread_cond_destroy(&route_condition);pthread_mutex_destroy(&route_mutex);
#endif
    route_worker_active=0;
}
/* Returns with the queue lock held. No ownership can be retired/replaced
   until an in-flight read publishes completion. Never hold a lock over I/O. */
static void route_quiesce(struct TitleRoutePrefetch *job)
{
    route_lock();
    while(job->state==ROUTE_READING){route_unlock();route_pause();route_lock();}
    if(job->state==ROUTE_QUEUED)job->state=ROUTE_IDLE;
}
static void route_prefetch(TitleRoute *r)
{
    struct TitleRoutePrefetch *job=r->prefetch;
    unsigned first=r->cache_first+PREFETCH_STEP;
    /* The last overlap sample is already resident. If it is also EOF,
       prepare the loop start instead of a redundant one-frame block. */
    if(first>=r->count-1u)first=0;
    route_lock();
    if(job->state==ROUTE_IDLE) {
        job->first=first;job->frames=r->count-first;
        if(job->frames>PREFETCH_FRAMES)job->frames=PREFETCH_FRAMES;
        job->state=ROUTE_QUEUED;route_wake();
    }
    route_unlock();
}
static void route_async_begin(TitleRoute *r)
{
    struct TitleRoutePrefetch *job=calloc(1,sizeof *job);if(!job)return;
    if(r->path[0]=='/' || strchr(r->path,':')) {
        memcpy(job->path,r->path,strlen(r->path)+1);
    } else {
        /* Kernel cwd is per-thread. Capture the main thread's absolute
           spelling; portable fopen still resolves its logical asset path. */
        if(!getcwd(job->path,sizeof job->path)){free(job);return;}
        size_t length=strlen(job->path),n=strlen(r->path);
        if(length+n+2>sizeof job->path){free(job);return;}
        job->path[length++]='/';memcpy(job->path+length,r->path,n+1);
    }
    if(!route_worker_start()){free(job);return;}
    job->file=r->file;r->file=NULL;job->buffer=r->cache_storage+PREFETCH_FRAMES*r->stride;
    job->stride=r->stride;job->offset=512+r->nchannels*4;
    route_lock();job->next=route_jobs;route_jobs=job;route_unlock();
    r->prefetch=job;if(r->cache_count>PREFETCH_FRAMES)r->cache_count=PREFETCH_FRAMES;
    route_prefetch(r);
}
static void route_async_free(TitleRoute *r)
{
    struct TitleRoutePrefetch *job=r->prefetch;if(!job)return;
    route_quiesce(job);
    struct TitleRoutePrefetch **p=&route_jobs;while(*p && *p!=job)p=&(*p)->next;
    if(*p)*p=job->next;
    int last=route_jobs==NULL;route_unlock();
    if(job->file)fclose(job->file);
    free(job);r->prefetch=NULL;
    if(last)route_worker_stop();
}
static int route_async_fill(TitleRoute *r,unsigned frame)
{
    struct TitleRoutePrefetch *job=r->prefetch;
    /* Usually already READY: just exchange pointers and queue the following
       second. Late reads/seeks may wait, but never skip or invent samples. */
    route_quiesce(job);
    int ready=job->state==ROUTE_READY && frame>=job->first && frame-job->first<job->frames;
    if(ready) {
        unsigned char *old=r->cache;r->cache=job->buffer;job->buffer=old;
        r->cache_first=job->first;r->cache_count=job->frames;
    }
    job->state=ROUTE_IDLE;route_unlock();
    if(!ready) {
        unsigned first=frame/PREFETCH_STEP*PREFETCH_STEP,count=r->count-first;
        if(count>PREFETCH_FRAMES)count=PREFETCH_FRAMES;
        r->cache_count=0;
        if(!route_read(&job->file,job->path,job->offset,job->stride,first,count,r->cache))return 0;
        r->cache_first=first;r->cache_count=count;
    }
    route_prefetch(r);return 1;
}
#endif
void title_route_free(TitleRoute *r)
{
#if TITLE_ROUTE_ASYNC
    route_async_free(r);
#endif
    if(r->file)fclose(r->file);
    free(r->cache_storage);free(r->image);memset(r,0,sizeof *r);
}
void title_route_resume(TitleRoute *r)
{
    /* Attract/recruit demos keep a file open between cache refills. Retain
       the current samples, but replace the pre-suspend handle on demand. */
#if TITLE_ROUTE_ASYNC
    if(r->prefetch) {
        struct TitleRoutePrefetch *job=r->prefetch;
        route_quiesce(job);job->state=ROUTE_IDLE;route_unlock();
        if(job->file){fclose(job->file);job->file=NULL;}
        route_prefetch(r);return;
    }
#endif
    if(r->file){fclose(r->file);r->file=NULL;}
}
static int valid_frame(const unsigned char *q,unsigned nc,int water)
{
    for(unsigned k=0;k<3;k++)if(!isfinite(real(q+k*4)))return 0;
    for(unsigned k=0;k<6;k++)if(!isfinite(real(q+20+k*4)))return 0;
    float near=real(q+48),far=real(q+52);
    if(angle(q+46)<=0 || near<=0 || !isfinite(near) || !isfinite(far) || far<=near)return 0;
    for(unsigned k=0;k<nc;k++)if(!isfinite(real(q+224+k*4)))return 0;
    const unsigned char *e=q+224+nc*4;
    for(unsigned k=0;k<41;k++)if(!isfinite(real(e+28+k*4)))return 0;
    if(word(e+12)>=6 || word(e+4)>255)return 0;
    if(water)for(unsigned k=0;k<3;k++)if(!isfinite(real(e+sizeof(SkateInput)+k*4)))return 0;
    return 1;
}
static const unsigned char *frame_data(TitleRoute *r,unsigned frame)
{
    if(frame<r->cache_first || frame-r->cache_first>=r->cache_count){
        unsigned phase=hang_mark(HANG_ROUTE_IO);
#if TITLE_ROUTE_ASYNC
        if(r->prefetch) {
            int ok=route_async_fill(r,frame);hang_mark(phase);
            return ok ? r->cache+(frame-r->cache_first)*r->stride:NULL;
        }
#endif
        unsigned first=frame/CACHE_STEP*CACHE_STEP,count=r->count-first;
        if(count>CACHE_FRAMES)count=CACHE_FRAMES;
        r->cache_count=0;
        if(!r->file)r->file=fopen(r->path,"rb");
        int error=!r->file || fseek(r->file,512+r->nchannels*4+(long)first*r->stride,SEEK_SET) ||
           fread(r->cache,r->stride,count,r->file)!=count;
        hang_mark(phase);if(error)return NULL;
        r->cache_first=first;r->cache_count=count;
    }
    return r->cache+(frame-r->cache_first)*r->stride;
}
int title_route_load(TitleRoute *r,const char *path)
{
    if(!path || strlen(path)>=sizeof r->path)return -1;
    TitleRoute candidate={0};FILE *f=fopen(path,"rb");if(!f)return -1;
    /* Read directly into the caller's block. Avoid a second stdio buffer
       per route and make each worker refill a single bulk read. */
    setvbuf(f,NULL,_IONBF,0);
    strcpy(candidate.path,path);
    candidate.file=f;unsigned char h[32];int err=-2;
    if(fread(h,1,32,f)!=32 || (memcmp(h,"TRT2",4) && memcmp(h,"TRT3",4)))goto done;
    candidate.has_water_point=!memcmp(h,"TRT3",4);
    unsigned n=word(h+4),chr=word(h+8),stride=word(h+16),nc=word(h+20);
    if(!n || n>30000 || chr>=16 || word(h+12)!=20 || nc>120 || stride!=428+nc*4+candidate.has_water_point*12)goto done;
    candidate.section[1]=word(h+24);candidate.section[2]=word(h+28);
    if(candidate.section[1]>candidate.section[2] || candidate.section[2]>=n)goto done;
    size_t size=512+nc*4;
    unsigned cache_frames=TITLE_ROUTE_ASYNC && n>CACHE_FRAMES ? 2*PREFETCH_FRAMES:CACHE_FRAMES;
    candidate.image=malloc(size);candidate.cache_storage=malloc(cache_frames*stride);
    if(!candidate.cache_storage && cache_frames>CACHE_FRAMES) {
        cache_frames=CACHE_FRAMES;candidate.cache_storage=malloc(cache_frames*stride);
    }
    candidate.cache=candidate.cache_storage;
    if(!candidate.image || !candidate.cache){err=-3;goto done;}
    unsigned char *p=candidate.image;memcpy(p,h,32);
    if(fread(p+32,1,size-32,f)!=size-32)goto done;
    unsigned seen[120]={0};
    for(unsigned i=0;i<nc;i++){unsigned ch=word(p+512+i*4);if(ch>=120 || seen[ch]++)goto done;}
    for(unsigned i=0;i<120;i++)if(!isfinite(real(p+32+i*4)))goto done;
    for(unsigned i=0;i<n;){
        unsigned count=n-i;if(count>CACHE_FRAMES)count=CACHE_FRAMES;
        if(fread(candidate.cache,stride,count,f)!=count)goto done;
        for(unsigned j=0;j<count;j++)if(!valid_frame(candidate.cache+j*stride,nc,candidate.has_water_point))goto done;
        i+=count;
    }
    if(fgetc(f)!=EOF)goto done;
    candidate.count=n;candidate.character=chr;candidate.stride=stride;candidate.nchannels=nc;
    candidate.base=(const float *)(p+32);candidate.channels=(const unsigned *)(p+512);
    if(!frame_data(&candidate,0))goto done;
    title_route_free(r);*r=candidate;
#if TITLE_ROUTE_ASYNC
    if(cache_frames>CACHE_FRAMES)route_async_begin(r);
#endif
    return 0;
done:title_route_free(&candidate);return err;
}
static void decode(const TitleRoute *r,const unsigned char *p,TitleSample *s)
{
    memset(s,0,sizeof *s);memcpy(s->pos,p,12);
    for(int k=0;k<3;k++)s->angle[k]=angle(p+12+k*2);
    s->state=angle(p+18);memcpy(s->camera.pos,p+20,12);memcpy(s->camera.forward,p+32,12);
    s->camera.roll=angle(p+44);s->camera.fov=angle(p+46);s->camera.near_clip=real(p+48);s->camera.far_clip=real(p+52);s->camera.active=1;
    memcpy(&s->effects,p+224+r->nchannels*4,sizeof s->effects);
    if(r->has_water_point)memcpy(s->water_point,p+428+r->nchannels*4,12);
    s->tick=word(p+56);s->part=(unsigned short)angle(p+60);
    for(int j=0;j<20;j++){
        memcpy(s->pose[j].pos,r->base+j*6,12);memcpy(s->pose[j].scl,r->base+j*6+3,12);
        for(int k=0;k<3;k++)s->pose[j].ang[k]=angle(p+64+j*8+k*2);
        unsigned ev=(unsigned short)angle(p+70+j*8);
        s->pose[j].ev=r->has_water_point ? ev&8191:ev;
        s->pose[j].keyed=r->has_water_point ? ev>>13:7;
    }
    for(unsigned i=0;i<r->nchannels;i++){
        unsigned c=r->channels[i],j=c/6,k=c%6;float v=real(p+224+i*4);
        if(k<3)s->pose[j].pos[k]=v;else s->pose[j].scl[k-3]=v;
    }
}
static int blend_angle(int a,int b,float t){return a+(int)((int16_t)(b-a)*t);}
int title_route_sample(TitleRoute *r,unsigned frame,float t,TitleSample *s)
{
    if(!r->image || !s || !isfinite(t))return 0;
    frame%=r->count;t=fmaxf(0,fminf(1,t));const unsigned char *p=frame_data(r,frame);if(!p)return 0;
    decode(r,p,s);if(!t || frame+1>=r->count)return 1;
    TitleSample next;const unsigned char *q=frame_data(r,frame+1);if(!q)return 0;decode(r,q,&next);
    if(next.part!=s->part)return 1;
    for(int k=0;k<3;k++){
        s->pos[k]+=(next.pos[k]-s->pos[k])*t;s->angle[k]=blend_angle(s->angle[k],next.angle[k],t);
        for(int j=0;j<20;j++){
            s->pose[j].pos[k]+=(next.pose[j].pos[k]-s->pose[j].pos[k])*t;
            s->pose[j].scl[k]+=(next.pose[j].scl[k]-s->pose[j].scl[k])*t;
            s->pose[j].ang[k]=blend_angle(s->pose[j].ang[k],next.pose[j].ang[k],t);
        }
    }
    /* Preserve authored cuts; never fly the camera between viewpoints. */
    if(!angle(q+62)){
        for(int k=0;k<3;k++){
            s->camera.pos[k]+=(next.camera.pos[k]-s->camera.pos[k])*t;
            s->camera.forward[k]+=(next.camera.forward[k]-s->camera.forward[k])*t;
        }
        float n=0;for(int k=0;k<3;k++)n+=s->camera.forward[k]*s->camera.forward[k];
        if(n>0){n=1.f/sqrtf(n);for(int k=0;k<3;k++)s->camera.forward[k]*=n;}
        s->camera.roll=blend_angle(s->camera.roll,next.camera.roll,t);
        s->camera.fov=blend_angle(s->camera.fov,next.camera.fov,t);
    }
    return 1;
}
