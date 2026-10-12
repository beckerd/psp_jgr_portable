#include "portable_replay_controller.h"
#include <string.h>

/* Source callback identities/layout offsets, no original instruction bytes or
 * replay samples. The original task-kill callback executes in the VM too. */
#define EXEC_BASE 0x8c010000u
#define SET_BASE 0x8cde6000u
#define WORK_BASE 0x8cf00000u
#define CTRL WORK_BASE
#define STACK (WORK_BASE+4096u)
static int get(PortableReplayController *c,uint32_t p,uint32_t *x)
{return portable_replay_vm_read(&c->vm,p,4,x);}
static int put(PortableReplayController *c,uint32_t p,uint32_t x)
{return portable_replay_vm_write(&c->vm,p,4,x);}
static int invoke(PortableReplayController *c,uint32_t entry)
{c->vm.r[4]=CTRL;c->vm.r[15]=STACK;return portable_replay_vm_run(&c->vm,entry,0,4096);}
int portable_replay_controller_begin(PortableReplayController *c,
    const void *exe,uint32_t exe_bytes,const void *bank,uint32_t bank_bytes,unsigned subset)
{
    memset(c,0,sizeof *c);
    if(!exe || !bank || exe_bytes<0xb37f2u-0x10000u || exe_bytes>0x1000000u-0x10000u ||
       bank_bytes<0x54 || bank_bytes>WORK_BASE-SET_BASE || subset>=8)return 0;
    if(!portable_replay_vm_map(&c->vm,EXEC_BASE,(void *)exe,exe_bytes,PRVM_READ|PRVM_EXEC) ||
       !portable_replay_vm_map(&c->vm,SET_BASE,(void *)bank,bank_bytes,PRVM_READ) ||
       !portable_replay_vm_map(&c->vm,WORK_BASE,c->work,sizeof c->work,PRVM_READ|PRVM_WRITE))return 0;
    uint32_t set=SET_BASE,p=0,magic=0;
    for(unsigned i=0;i<=subset;i++){
        if(!get(c,set,&magic) || magic!=0x63534d4au)return 0;
        if(i<subset && (!get(c,set+0x50,&set) || !set))return 0;
    }
    if(!get(c,set+0x44,&p) || !get(c,p,&p) || !get(c,p,&c->stream))return 0;
    if(c->stream<SET_BASE || c->stream-SET_BASE>bank_bytes-36)return 0;
    p=c->stream+32;
    for(unsigned i=0;i<16384;i++) {
        uint32_t record;
        if(!get(c,p+i*4,&record))return 0;
        if(record&128u){c->samples=i+1;break;}
        c->maximum_ticks+=(record&127u)+1;
    }
    if(!c->samples || !c->maximum_ticks)return 0;
    uint32_t first=0;
    if(!get(c,p,&first) || !put(c,CTRL+0x7c,c->stream) || !put(c,CTRL+0x84,4) ||
       !put(c,CTRL+0x70,p) || !put(c,CTRL+0x78,first&127u) ||
       !put(c,CTRL+0xb8,1) || !put(c,CTRL+0xc8,CTRL+0xd0))return 0;
    return invoke(c,0x8c027c5cu); /* original input clear */
}
int portable_replay_controller_step(PortableReplayController *c,PortableReplayInput *s)
{
    if(!c || !s || !c->samples || c->tick>c->maximum_ticks)return 0;
    uint32_t flags=0;
    if(!get(c,CTRL,&flags) || (flags&1u))return 0;
    if(!invoke(c,0x8c0278c0u))return 0;
    memset(s,0,sizeof *s);s->tick=c->tick++;
    if(!get(c,CTRL+0x70,&s->record) || !get(c,CTRL+0x6c,&s->input) ||
       !get(c,CTRL+0x74,&s->previous) || !get(c,CTRL+0x78,&s->repeat) ||
       !get(c,CTRL,&s->ended))return 0;
    s->previous&=255;s->ended&=1;
    if(!invoke(c,0x8c027788u))return 0;
    s->direction=c->vm.r[0];
    if(!invoke(c,0x8c0277a0u))return 0;
    s->magnitude=c->vm.fr[0];
    return 1;
}
