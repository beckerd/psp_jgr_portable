#ifndef JGR_CRASH_REPORT_H
#define JGR_CRASH_REPORT_H
#include "crash_shared.h"

/* No printf, stdio locks, heap allocation or reads through saved pointers.
   This same bounded formatter is exercised by the host fault-path tests. */
typedef struct {char *data;size_t size,used;} JgrCrashText;
static inline void jgr_crash_char(JgrCrashText *t,char c)
{if(t->used+1<t->size)t->data[t->used++]=c;}
static inline void jgr_crash_text(JgrCrashText *t,const char *s)
{while(*s)jgr_crash_char(t,*s++);}
static inline void jgr_crash_hex(JgrCrashText *t,uint32_t v)
{for(int s=28;s>=0;s-=4)jgr_crash_char(t,"0123456789abcdef"[(v>>s)&15]);}
static inline void jgr_crash_field(JgrCrashText *t,const char *name,uint32_t v)
{jgr_crash_text(t,name);jgr_crash_hex(t,v);jgr_crash_char(t,'\n');}
static inline size_t jgr_crash_format(char *text,size_t size,
                                    const volatile JgrCrashShared *s,const char *build)
{
    static const char *names[]={"zr","at","v0","v1","a0","a1","a2","a3",
        "t0","t1","t2","t3","t4","t5","t6","t7","s0","s1","s2","s3",
        "s4","s5","s6","s7","t8","t9","k0","k1","gp","sp","fp","ra"};
    JgrCrashText t={text,size,0};
    jgr_crash_text(&t,"\nJGR CRASH BEGIN v1\nbuild=");jgr_crash_text(&t,build);
    jgr_crash_text(&t,"\nAll numeric fields below are hexadecimal.\n");
    jgr_crash_field(&t,"anchor_crash_init=",s->anchor);
    jgr_crash_field(&t,"text_addr=",s->text_addr);
    jgr_crash_field(&t,"text_size=",s->text_size);
    jgr_crash_field(&t,"thread=",s->fault_thread);
    jgr_crash_field(&t,"exception_code=",(s->regs.cause>>2)&31);
    jgr_crash_field(&t,"epc=",s->regs.epc);
    jgr_crash_field(&t,"fault_pc=",s->regs.epc+((s->regs.cause&0x80000000u)?4u:0u));
    jgr_crash_field(&t,"cause=",s->regs.cause);
    jgr_crash_field(&t,"badvaddr=",s->regs.badvaddr);
    jgr_crash_field(&t,"status=",s->regs.status);
    jgr_crash_field(&t,"lo=",s->regs.lo);jgr_crash_field(&t,"hi=",s->regs.hi);
    for(unsigned i=0;i<32;i++) {
        jgr_crash_text(&t,names[i]);jgr_crash_char(&t,'=');
        jgr_crash_hex(&t,s->regs.gpr[i]);jgr_crash_char(&t,i%4==3?'\n':' ');
    }
    if(s->regs.status&0x20000000u) {
        jgr_crash_text(&t,"FPU registers (raw bits):\n");
        for(unsigned i=0;i<32;i++) {
            jgr_crash_hex(&t,s->regs.fpr[i]);jgr_crash_char(&t,i%8==7?'\n':' ');
        }
        jgr_crash_field(&t,"fsr=",s->regs.fsr);jgr_crash_field(&t,"fir=",s->regs.fir);
    }
    jgr_crash_text(&t,"Last periodic memory sample (not fault-time memory):\n");
    jgr_crash_field(&t,"sample_frame=",s->sample_frame);
    jgr_crash_field(&t,"sample_stage=",s->sample_stage);
    jgr_crash_field(&t,"sample_state=",s->sample_state);
    jgr_crash_field(&t,"heap_free=",s->heap_free);
    jgr_crash_field(&t,"system_free=",s->system_free);
    jgr_crash_field(&t,"system_largest=",s->system_largest);
    unsigned words=s->stack_count;
    if(words>JGR_CRASH_STACK_WORDS)words=JGR_CRASH_STACK_WORDS;
    jgr_crash_field(&t,"stack_words=",words);
    jgr_crash_text(&t,"Raw stack words, NOT a validated backtrace:\n");
    for(unsigned i=0;i<words;i++) {
        jgr_crash_hex(&t,s->stack[i]);jgr_crash_char(&t,i%8==7?'\n':' ');
    }
    jgr_crash_text(&t,"\nJGR CRASH END\n");
    if(size)text[t.used]=0;
    return t.used;
}

typedef int (*JgrCrashWrite)(int,const void *,unsigned);
static inline int jgr_crash_write_all(int fd,const char *text,unsigned size,JgrCrashWrite write_fn)
{
    while(size) {
        int n=write_fn(fd,text,size);
        if(n<=0 || (unsigned)n>size)return n<0?n:-1;
        text+=n;size-=(unsigned)n;
    }
    return 0;
}
#endif
