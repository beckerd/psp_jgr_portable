#ifndef JGR_RELEASE_REPORT_H
#define JGR_RELEASE_REPORT_H

/* Always-on failure context, independent of performance diagnostics. Producers
   belong to the main thread. No producer allocates, formats text, or does I/O. */
enum { RELEASE_EVENT_HEARTBEAT=1, RELEASE_EVENT_STATE,
       RELEASE_EVENT_PROP_BEGIN, RELEASE_EVENT_PROP_END, RELEASE_EVENT_PROP_IMPACT,
       RELEASE_EVENT_QUERY_BEGIN, RELEASE_EVENT_QUERY_END, RELEASE_EVENT_SAMPLE_ERROR };
enum { RELEASE_QUERY_RAY=1, RELEASE_QUERY_BODY, RELEASE_QUERY_SPHERE };
enum { RELEASE_QUERY_UNKNOWN, RELEASE_QUERY_RESULT_REUSE, RELEASE_QUERY_CACHED_SOURCE,
       RELEASE_QUERY_STREAMING_SOURCE, RELEASE_QUERY_GRID_ONLY };
enum { RELEASE_PROP_MOTION=1, RELEASE_PROP_CONTACT, RELEASE_PROP_DRAW, RELEASE_PROP_RESET };
typedef int (*ReleaseReportWriter)(int,const void *,unsigned);

#if defined(__PSP__) || defined(JGR_RELEASE_REPORT_HOST_TEST)
void release_report_log(const void *data,unsigned bytes);
void release_report_event(unsigned kind,unsigned a,unsigned b,unsigned c);
void release_report_memory(unsigned frame,unsigned heap,unsigned system,unsigned largest,int stack_free);
unsigned release_report_query_begin(unsigned kind,const void *world,unsigned mask,unsigned capacity);
void release_report_query_candidates(unsigned mode,unsigned count);
void release_report_query_end(unsigned token,int result);
unsigned release_report_prop_begin(unsigned stage,unsigned index,unsigned address,unsigned type,unsigned activity);
void release_report_prop_end(unsigned token);
/* A failure does not consume history. Handles positive short writes. Never
   retries a changing context indefinitely or takes main-thread locks. */
int release_report_write(int fd,ReleaseReportWriter writer);
#else
static inline void release_report_log(const void *p,unsigned n){(void)p;(void)n;}
static inline void release_report_event(unsigned k,unsigned a,unsigned b,unsigned c){(void)k;(void)a;(void)b;(void)c;}
static inline void release_report_memory(unsigned f,unsigned h,unsigned s,unsigned l,int t){(void)f;(void)h;(void)s;(void)l;(void)t;}
static inline unsigned release_report_query_begin(unsigned k,const void *w,unsigned m,unsigned c){(void)k;(void)w;(void)m;(void)c;return 0;}
static inline void release_report_query_candidates(unsigned m,unsigned c){(void)m;(void)c;}
static inline void release_report_query_end(unsigned t,int r){(void)t;(void)r;}
static inline unsigned release_report_prop_begin(unsigned s,unsigned i,unsigned a,unsigned t,unsigned v){(void)s;(void)i;(void)a;(void)t;(void)v;return 0;}
static inline void release_report_prop_end(unsigned t){(void)t;}
static inline int release_report_write(int fd,ReleaseReportWriter w){(void)fd;(void)w;return 0;}
#endif
#endif
