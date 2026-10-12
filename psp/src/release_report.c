#include <stdint.h>
#include <string.h>
#include "release_report.h"
#include "hang.h"
#include "crash_report.h"

#define RELEASE_RECORDS 28u
#define RELEASE_TEXT 112u
#define RELEASE_QUERY_DEPTH 8u
#define RELEASE_PROP_DEPTH 4u
/* A whole number of physical ring revolutions preserves newest retention
   across serial wrap; the low bit remains reserved for commit state. */
#define RELEASE_SERIAL_MAX ((0x7fffffffu/RELEASE_RECORDS)*RELEASE_RECORDS)
typedef struct {
    unsigned stamp,kind,a,b,c,frame,position[3],length;
    unsigned char text[RELEASE_TEXT];
} ReleaseRecord;
typedef struct {unsigned stamp,frame,heap,system,largest,stack_free,heap_min_sampled,stack_min_sampled;} ReleaseMemory;
typedef struct {unsigned stamp,kind,world,mask,capacity,mode,count,result,sequence;} ReleaseQuery;
typedef struct {unsigned stamp,stage,index,address,type,activity;} ReleaseProp;
static volatile struct {
    ReleaseRecord records[RELEASE_RECORDS];
    ReleaseMemory memory;
    ReleaseQuery query[RELEASE_QUERY_DEPTH],last_query;
    ReleaseProp prop[RELEASE_PROP_DEPTH],last_prop;
    unsigned latest,count,log_id,query_depth,prop_depth,query_overflow,prop_overflow,query_serial;
} release_state;
typedef char ReleaseStorageBound[(sizeof release_state<=5120u)?1:-1];

/* Producers and reporters run on the same PSP CPU, never on the ME. Aligned
   word stores are atomic; compiler barriers preserve publication order across
   a thread switch/exception. No expensive inter-CPU cache operation is needed. */
static inline void release_barrier(void){__asm__ volatile("" ::: "memory");}
static unsigned begin_update(volatile unsigned *stamp)
{unsigned odd=(*stamp+1u)|1u;*stamp=odd;release_barrier();return odd;}
static void end_update(volatile unsigned *stamp,unsigned odd)
{release_barrier();*stamp=odd==0xffffffffu ? 2u:odd+1u;}
/* One attempt, bounded even if the game has recovered and keeps mutating.
   Read volatile bytes so the compiler cannot move payload reads outside the
   two stamp reads. Uncommitted/overwritten data is explicitly skipped. */
static int snapshot(void *out,const volatile void *source,unsigned bytes)
{
    const volatile unsigned *stamp=source;
    unsigned before=*stamp;if(!before || (before&1u))return 0;
    release_barrier();
    const volatile unsigned char *src=source;unsigned char *dst=out;
    for(unsigned i=0;i<bytes;i++)dst[i]=src[i];
    release_barrier();return before==*stamp;
}
static volatile ReleaseRecord *record_begin(unsigned kind,unsigned a,unsigned b,unsigned c)
{
    unsigned id=release_state.latest+1u;if(id>RELEASE_SERIAL_MAX)id=1u;
    volatile ReleaseRecord *r=release_state.records+id%RELEASE_RECORDS;
    r->stamp=id*2u-1u;release_barrier();
    r->kind=kind;r->a=a;r->b=b;r->c=c;r->frame=hang_progress.frame;
    for(unsigned i=0;i<3;i++)r->position[i]=hang_progress.player_position[i];
    r->length=0;return r;
}
static void record_commit(volatile ReleaseRecord *r)
{
    unsigned id=(r->stamp+1u)/2u;
    release_barrier();r->stamp=id*2u;release_barrier();
    release_state.latest=id;
    if(release_state.count<RELEASE_RECORDS)release_state.count++;
}
void release_report_event(unsigned kind,unsigned a,unsigned b,unsigned c)
{volatile ReleaseRecord *r=record_begin(kind,a,b,c);record_commit(r);}
void release_report_log(const void *data,unsigned bytes)
{
    if(!data || !bytes)return;
    const unsigned char *text=data;unsigned id=++release_state.log_id;
    /* An unexpectedly huge log request still does bounded work, retaining
       its newest suffix. a=message ID, b=byte offset, c=original byte count. */
    unsigned offset=bytes>RELEASE_RECORDS*RELEASE_TEXT ? bytes-RELEASE_RECORDS*RELEASE_TEXT:0;
    while(offset<bytes) {
        unsigned length=bytes-offset;if(length>RELEASE_TEXT)length=RELEASE_TEXT;
        volatile ReleaseRecord *r=record_begin(0,id,offset,bytes);
        for(unsigned i=0;i<length;i++)r->text[i]=text[offset+i];
        r->length=length;record_commit(r);offset+=length;
    }
}
void release_report_memory(unsigned frame,unsigned heap,unsigned system,unsigned largest,int stack_free)
{
    volatile ReleaseMemory *m=&release_state.memory;unsigned valid=m->stamp;
    unsigned odd=begin_update(&m->stamp);
    m->frame=frame;m->heap=heap;m->system=system;m->largest=largest;m->stack_free=(unsigned)stack_free;
    if(!valid || heap<m->heap_min_sampled)m->heap_min_sampled=heap;
    if(!valid)m->stack_min_sampled=0xffffffffu;
    if(stack_free>=0 && (unsigned)stack_free<m->stack_min_sampled)m->stack_min_sampled=(unsigned)stack_free;
    end_update(&m->stamp,odd);
}
unsigned release_report_query_begin(unsigned kind,const void *world,unsigned mask,unsigned capacity)
{
    unsigned depth=release_state.query_depth;
    if(depth<RELEASE_QUERY_DEPTH) {
        volatile ReleaseQuery *q=release_state.query+depth;unsigned odd=begin_update(&q->stamp);
        q->kind=kind;q->world=(unsigned)(uintptr_t)world;q->mask=mask;q->capacity=capacity;
        q->mode=RELEASE_QUERY_UNKNOWN;q->count=0;q->result=0;q->sequence=++release_state.query_serial;
        end_update(&q->stamp,odd);
    } else release_state.query_overflow++;
    release_barrier();release_state.query_depth=depth+1u;return depth;
}
void release_report_query_candidates(unsigned mode,unsigned count)
{
    unsigned depth=release_state.query_depth;
    if(!depth || depth>RELEASE_QUERY_DEPTH)return;
    volatile ReleaseQuery *q=release_state.query+depth-1u;unsigned odd=begin_update(&q->stamp);
    q->mode=mode;q->count=count;end_update(&q->stamp,odd);
}
void release_report_query_end(unsigned token,int result)
{
    if(token<RELEASE_QUERY_DEPTH && release_state.query_depth==token+1u) {
        const volatile ReleaseQuery *q=release_state.query+token;
        volatile ReleaseQuery *last=&release_state.last_query;unsigned odd=begin_update(&last->stamp);
        last->kind=q->kind;last->world=q->world;last->mask=q->mask;last->capacity=q->capacity;
        last->mode=q->mode;last->count=q->count;last->result=(unsigned)result;last->sequence=q->sequence;
        end_update(&last->stamp,odd);
    }
    release_barrier();release_state.query_depth=token;
}
unsigned release_report_prop_begin(unsigned stage,unsigned index,unsigned address,unsigned type,unsigned activity)
{
    unsigned depth=release_state.prop_depth;
    if(depth<RELEASE_PROP_DEPTH) {
        volatile ReleaseProp *p=release_state.prop+depth;unsigned odd=begin_update(&p->stamp);
        p->stage=stage;p->index=index;p->address=address;p->type=type;p->activity=activity;
        end_update(&p->stamp,odd);
    } else release_state.prop_overflow++;
    release_barrier();release_state.prop_depth=depth+1u;return depth;
}
void release_report_prop_end(unsigned token)
{
    if(token<RELEASE_PROP_DEPTH && release_state.prop_depth==token+1u) {
        const volatile ReleaseProp *p=release_state.prop+token;
        volatile ReleaseProp *last=&release_state.last_prop;unsigned odd=begin_update(&last->stamp);
        last->stage=p->stage;last->index=p->index;last->address=p->address;last->type=p->type;last->activity=p->activity;
        end_update(&last->stamp,odd);
    }
    release_barrier();release_state.prop_depth=token;
}
static int write_fields(int fd,ReleaseReportWriter writer,const char *title,
                        const char *const *names,const unsigned *values,unsigned count)
{
    char text[768];JgrCrashText t={text,sizeof text,0};jgr_crash_text(&t,title);
    for(unsigned i=0;i<count;i++)jgr_crash_field(&t,names[i],values[i]);
    return jgr_crash_write_all(fd,text,(unsigned)t.used,writer);
}
static int write_query(int fd,ReleaseReportWriter writer,const char *title,const ReleaseQuery *q)
{
    static const char *const names[]={"kind=","world=","mask=","capacity=","candidate_mode=","candidate_count=","result=","sequence="};
    unsigned values[]={q->kind,q->world,q->mask,q->capacity,q->mode,q->count,q->result,q->sequence};
    return write_fields(fd,writer,title,names,values,8);
}
static int write_prop(int fd,ReleaseReportWriter writer,const char *title,const ReleaseProp *p)
{
    static const char *const names[]={"stage=","index=","source_address=","type=","activity="};
    unsigned values[]={p->stage,p->index,p->address,p->type,p->activity};
    return write_fields(fd,writer,title,names,values,5);
}
int release_report_write(int fd,ReleaseReportWriter writer)
{
    if(!writer)return -1;
    static const char *const context_names[]={"frame=","stage=","state=","phase=","player_tick=","player_zone=","player_movement=","player_x_f32=","player_y_f32=","player_z_f32=","query_depth=","prop_depth=","query_overflow=","prop_overflow="};
    unsigned context[]={hang_progress.frame,hang_progress.stage,hang_progress.state,hang_progress.phase,
        hang_progress.player_tick,hang_progress.player_zone,hang_progress.player_movement,
        hang_progress.player_position[0],hang_progress.player_position[1],hang_progress.player_position[2],
        release_state.query_depth,release_state.prop_depth,release_state.query_overflow,release_state.prop_overflow};
    int result=write_fields(fd,writer,"\nJGR RELEASE CONTEXT BEGIN v1\nNumeric fields hexadecimal. Context is last observed main-thread progress, not a CPU backtrace.\n",context_names,context,14);
    if(result)return result;
    ReleaseMemory memory;
    if(snapshot(&memory,&release_state.memory,sizeof memory)) {
        static const char *const names[]={"sample_frame=","heap_free=","system_free=","system_largest=","main_stack_free=","heap_min_sampled=","main_stack_min_sampled="};
        unsigned values[]={memory.frame,memory.heap,memory.system,memory.largest,memory.stack_free,memory.heap_min_sampled,memory.stack_min_sampled};
        result=write_fields(fd,writer,"HEALTH last safe sample; minima cover samples, not every allocation/call. ffffffff stack minimum means unavailable.\n",names,values,7);
    } else result=jgr_crash_write_all(fd,"HEALTH unavailable\n",19,writer);
    if(result)return result;
    static const char legend[]="QUERY count: mode1=reused contacts, mode2=cached source entries, mode0/3/4=unavailable (zero is not an empty query).\n";
    result=jgr_crash_write_all(fd,legend,sizeof legend-1u,writer);if(result)return result;
    ReleaseQuery q;unsigned depth=context[10];
    if(depth && depth<=RELEASE_QUERY_DEPTH && snapshot(&q,release_state.query+depth-1u,sizeof q) && depth==release_state.query_depth) {
        result=write_query(fd,writer,"QUERY active snapshot\n",&q);if(result)return result;
    }
    if(snapshot(&q,&release_state.last_query,sizeof q)) {
        result=write_query(fd,writer,"QUERY last completed snapshot\n",&q);if(result)return result;
    }
    ReleaseProp p;depth=context[11];
    if(depth && depth<=RELEASE_PROP_DEPTH && snapshot(&p,release_state.prop+depth-1u,sizeof p) && depth==release_state.prop_depth) {
        result=write_prop(fd,writer,"PROP active snapshot\n",&p);if(result)return result;
    }
    if(snapshot(&p,&release_state.last_prop,sizeof p)) {
        result=write_prop(fd,writer,"PROP last completed snapshot\n",&p);if(result)return result;
    }
    unsigned latest=release_state.latest,count=release_state.count,skipped=0;
    if(count>RELEASE_RECORDS)count=RELEASE_RECORDS;
    for(unsigned age=count;age>0;age--) {
        unsigned back=age-1u,id=latest>back ? latest-back:RELEASE_SERIAL_MAX-(back-latest);
        ReleaseRecord r;
        if(!snapshot(&r,release_state.records+id%RELEASE_RECORDS,sizeof r) || r.stamp!=id*2u || r.length>RELEASE_TEXT){skipped++;continue;}
        static const char *const names[]={"kind=","a=","b=","c=","frame=","player_x_f32=","player_y_f32=","player_z_f32=","text_bytes="};
        unsigned values[]={r.kind,r.a,r.b,r.c,r.frame,r.position[0],r.position[1],r.position[2],r.length};
        result=write_fields(fd,writer,"RECORD\n",names,values,9);if(result)return result;
        if(r.length){result=jgr_crash_write_all(fd,(const char *)r.text,r.length,writer);if(result)return result;
            result=jgr_crash_write_all(fd,"\n",1,writer);if(result)return result;}
    }
    static const char *const tail_names[]={"records_requested=","records_unavailable="};
    unsigned tail[]={count,skipped};result=write_fields(fd,writer,"HISTORY bounded snapshot\n",tail_names,tail,2);
    if(result)return result;
    return jgr_crash_write_all(fd,"JGR RELEASE CONTEXT END\n",24,writer);
}
