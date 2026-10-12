/* Synthetic route bytes plus the real threaded reader. No game assets. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_t main_thread;
static pthread_mutex_t read_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t read_condition=PTHREAD_COND_INITIALIZER;
static unsigned main_reads,worker_reads;
static int hold_reads,reader_held,fail_read,fail_calloc,fail_thread;
static size_t fail_allocation;
static size_t observed_read(void *p,size_t size,size_t count,FILE *f)
{
    int worker=!pthread_equal(pthread_self(),main_thread),fail=0;
    pthread_mutex_lock(&read_lock);
    if(worker) {
        worker_reads++;
        while(hold_reads){reader_held=1;pthread_cond_broadcast(&read_condition);pthread_cond_wait(&read_condition,&read_lock);}
        if(fail_read){fail=1;fail_read--;}
    } else main_reads++;
    pthread_mutex_unlock(&read_lock);
    return fail ? 0:fread(p,size,count,f);
}
static void *observed_calloc(size_t count,size_t size)
{if(fail_calloc){fail_calloc=0;return NULL;}return calloc(count,size);}
static void *observed_malloc(size_t size)
{if(fail_allocation==size){fail_allocation=0;return NULL;}return malloc(size);}
static int observed_thread(pthread_t *t,const pthread_attr_t *attr,void *(*fn)(void *),void *arg)
{if(fail_thread){fail_thread=0;return EAGAIN;}return pthread_create(t,attr,fn,arg);}
#define JGR_TITLE_ROUTE_TEST_ASYNC 1
#define fread observed_read
#define calloc observed_calloc
#define malloc observed_malloc
#define pthread_create observed_thread
#include "../src/title_route.c"
#undef fread
#undef calloc
#undef malloc
#undef pthread_create

int sync_title_route_load(TitleRoute *,const char *);
void sync_title_route_free(TitleRoute *);
int sync_title_route_sample(TitleRoute *,unsigned,float,TitleSample *);

static void put(unsigned char *p,unsigned value)
{for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(value>>(8*i));}
static void put_real(unsigned char *p,float value){memcpy(p,&value,4);}
static void write_route(const char *path,unsigned count,int water)
{
    FILE *f=fopen(path,"wb");assert(f);
    unsigned stride=428+8+(water?12:0);unsigned char header[520]={0},frame[448];
    memcpy(header,water?"TRT3":"TRT2",4);put(header+4,count);put(header+8,1);
    put(header+12,20);put(header+16,stride);put(header+20,2);
    put(header+24,count/3);put(header+28,count*2/3);
    for(unsigned i=0;i<120;i++)put_real(header+32+i*4,(float)i*.125f);
    put(header+512,0);put(header+516,119);assert(fwrite(header,1,sizeof header,f)==sizeof header);
    for(unsigned i=0;i<count;i++) {
        memset(frame,0,sizeof frame);
        for(unsigned k=0;k<3;k++)put_real(frame+k*4,(float)i*(k+1)*.25f);
        for(unsigned k=0;k<6;k++)put_real(frame+20+k*4,k<3?(float)i*.0625f:(k==5?1.f:0.f));
        frame[46]=45;put_real(frame+48,1.f);put_real(frame+52,1000.f);
        put(frame+56,i);frame[60]=(unsigned char)(i/(count/3+1));frame[62]=(unsigned char)(i%47==0);
        for(unsigned j=0;j<20;j++)for(unsigned k=0;k<4;k++) {
            unsigned a=(i*17+j*23+k*5)&65535;frame[64+j*8+k*2]=(unsigned char)a;frame[65+j*8+k*2]=(unsigned char)(a>>8);
        }
        put_real(frame+224,(float)i*.125f);put_real(frame+228,1.f+(float)i*.0001f);
        for(unsigned j=0;j<41;j++)put_real(frame+232+28+j*4,(float)j*.0625f);
        if(water)for(unsigned j=0;j<3;j++)put_real(frame+436+j*4,-(float)i*(j+1));
        assert(fwrite(frame,1,stride,f)==stride);
    }
    assert(!fclose(f));
}
static void await_prefetch(TitleRoute *r)
{
    assert(r->prefetch);
    for(unsigned i=0;i<5000;i++) {
        route_lock();int state=r->prefetch->state;route_unlock();
        if(state==ROUTE_READY || state==ROUTE_FAILED)return;
        route_pause();
    }
    assert(!"reader did not finish");
}
static void same_sample(TitleRoute *async,TitleRoute *sync,unsigned frame,float fraction,int no_main_io)
{
    TitleSample a,b;unsigned before=main_reads;
    assert(title_route_sample(async,frame,fraction,&a));
    if(no_main_io)assert(main_reads==before);
    assert(sync_title_route_sample(sync,frame,fraction,&b));assert(!memcmp(&a,&b,sizeof a));
}
static void set_hold(int hold)
{
    pthread_mutex_lock(&read_lock);hold_reads=hold;if(hold)reader_held=0;
    pthread_cond_broadcast(&read_condition);pthread_mutex_unlock(&read_lock);
}
static void wait_held(void)
{
    pthread_mutex_lock(&read_lock);while(!reader_held)pthread_cond_wait(&read_condition,&read_lock);
    pthread_mutex_unlock(&read_lock);
}
static void *release_read(void *unused)
{(void)unused;struct timespec pause={0,25000000};nanosleep(&pause,NULL);set_hold(0);return NULL;}
static void test_sequential(const char *path)
{
    TitleRoute r={0},reference={0};assert(!title_route_load(&r,path));assert(r.prefetch);
    assert(!sync_title_route_load(&reference,path));
    assert(r.cache_storage && r.prefetch->buffer==r.cache_storage+33*r.stride);
    for(unsigned pass=0;pass<2;pass++)for(unsigned frame=0;frame<r.count;frame++) {
        await_prefetch(&r);same_sample(&r,&reference,frame,0,1);
        same_sample(&r,&reference,frame,.5f,1);
    }
    assert(worker_reads>5);title_route_free(&r);sync_title_route_free(&reference);assert(!route_worker_active);
}
static void test_seek_resume_and_replacement(const char *path,const char *hidden)
{
    TitleRoute r={0},reference={0};assert(!title_route_load(&r,path));assert(!sync_title_route_load(&reference,path));
    const unsigned frames[]={179,12,224,65,200,0};
    for(unsigned i=0;i<sizeof frames/sizeof *frames;i++)same_sample(&r,&reference,frames[i],.25f,0);
    title_route_resume(&r);await_prefetch(&r);same_sample(&r,&reference,0,.5f,1);
    /* A failed replacement preserves the current worker/file/cache. */
    assert(title_route_load(&r,"/missing-route-test.bin")<0);same_sample(&r,&reference,0,0,1);
    assert(!rename(path,hidden));title_route_resume(&r);await_prefetch(&r);
    same_sample(&r,&reference,0,0,1);TitleSample sample;assert(!title_route_sample(&r,200,0,&sample));
    assert(!rename(hidden,path));same_sample(&r,&reference,200,0,0);
    title_route_free(&r);sync_title_route_free(&reference);
}
static void test_failure_fallbacks(const char *path)
{
    TitleRoute r={0},reference={0};assert(!sync_title_route_load(&reference,path));
    fail_allocation=66*reference.stride;assert(!title_route_load(&r,path));assert(!r.prefetch);
    same_sample(&r,&reference,150,.5f,0);title_route_free(&r);
    fail_calloc=1;assert(!title_route_load(&r,path));assert(!r.prefetch);
    same_sample(&r,&reference,150,.5f,0);title_route_free(&r);
    fail_thread=1;assert(!title_route_load(&r,path));assert(!r.prefetch && !route_worker_active);
    same_sample(&r,&reference,150,.5f,0);title_route_free(&r);
    pthread_mutex_lock(&read_lock);fail_read=1;pthread_mutex_unlock(&read_lock);
    assert(!title_route_load(&r,path));await_prefetch(&r);
    route_lock();assert(r.prefetch->state==ROUTE_FAILED);route_unlock();
    unsigned before=main_reads;same_sample(&r,&reference,33,0,0);assert(main_reads==before+1);
    await_prefetch(&r);same_sample(&r,&reference,65,0,1);
    title_route_free(&r);sync_title_route_free(&reference);
}
static void test_inflight_retirement_and_multiple(const char *path)
{
    TitleRoute routes[3]={{0}},reference={0};assert(!sync_title_route_load(&reference,path));
    for(unsigned i=0;i<3;i++)assert(!title_route_load(routes+i,path));
    for(unsigned frame=0;frame<200;frame+=16)for(unsigned i=0;i<3;i++) {
        await_prefetch(routes+i);same_sample(routes+i,&reference,frame,.5f,1);
    }
    title_route_free(routes+1);assert(route_worker_active);
    for(unsigned i=0;i<3;i+=2){await_prefetch(routes+i);same_sample(routes+i,&reference,193,0,1);}
    title_route_free(routes+2);title_route_free(routes);assert(!route_worker_active);
    /* A deliberately late read may wait, but must not reread on main or
       advance/drop source time. */
    set_hold(1);assert(!title_route_load(routes,path));wait_held();
    pthread_t releaser;assert(!pthread_create(&releaser,NULL,release_read,NULL));
    same_sample(routes,&reference,33,.5f,1);pthread_join(releaser,NULL);title_route_free(routes);
    set_hold(1);assert(!title_route_load(routes,path));wait_held();
    assert(!pthread_create(&releaser,NULL,release_read,NULL));
    title_route_free(routes);pthread_join(releaser,NULL);assert(!route_worker_active);
    /* Resume also retires an in-flight handle before queueing its replacement. */
    set_hold(1);assert(!title_route_load(routes,path));wait_held();
    assert(!pthread_create(&releaser,NULL,release_read,NULL));title_route_resume(routes);
    pthread_join(releaser,NULL);await_prefetch(routes);same_sample(routes,&reference,33,0,1);
    title_route_free(routes);sync_title_route_free(&reference);
}
int main(int argc,char **argv)
{
    assert(argc==2);main_thread=pthread_self();char path[512],hidden[512];
    assert(snprintf(path,sizeof path,"%s/route.bin",argv[1])<(int)sizeof path);
    assert(snprintf(hidden,sizeof hidden,"%s/route.hidden",argv[1])<(int)sizeof hidden);
    for(int water=0;water<=1;water++) {
        write_route(path,257,water);test_sequential(path);test_seek_resume_and_replacement(path,hidden);
        test_failure_fallbacks(path);test_inflight_retirement_and_multiple(path);
    }
    write_route(path,33,0);TitleRoute small={0};assert(!title_route_load(&small,path));assert(!small.prefetch);
    unsigned before=main_reads;TitleSample sample;for(unsigned i=0;i<100;i++)assert(title_route_sample(&small,i,0,&sample));
    assert(before==main_reads);title_route_free(&small);unlink(path);
    puts("route prefetch exact samples, no main-thread sequential reads, lifetime and fallback checks passed");return 0;
}
