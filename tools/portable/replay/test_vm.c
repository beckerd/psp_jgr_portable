/* Authored ISA fixtures, not extracted game instructions or game values. */
#include "portable_replay_vm.h"
#include "portable_replay_cues.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CODE 0x8c400000u
#define DATA 0x8c410000u
static PortableReplayVM v;
static unsigned char code[128],data[256];
static unsigned read_services,write_services;
static int memory_read_service(PortableReplayVM *vm,uint32_t a,unsigned n,uint32_t *value,void *ctx)
{(void)vm;(void)a;(void)n;(void)value;(void)ctx;read_services++;return 0;}
static int memory_write_service(PortableReplayVM *vm,uint32_t a,unsigned n,uint32_t value,void *ctx)
{(void)vm;(void)a;(void)n;(void)value;(void)ctx;write_services++;return 0;}
static void setup(const unsigned short *words,unsigned n)
{
    portable_replay_vm_init(&v);memset(code,0,sizeof code);memset(data,0,sizeof data);
    for(unsigned i=0;i<n;i++){code[2*i]=(unsigned char)words[i];code[2*i+1]=(unsigned char)(words[i]>>8);}
    assert(portable_replay_vm_map(&v,CODE,code,sizeof code,PRVM_READ|PRVM_EXEC));
    assert(portable_replay_vm_map(&v,DATA,data,sizeof data,PRVM_READ|PRVM_WRITE));
}
#define PROGRAM(...) do{const unsigned short words[]={__VA_ARGS__};setup(words,sizeof words/sizeof *words);}while(0)
int main(void)
{
    uint32_t x;unsigned tests=0;
    PROGRAM(0xe007,0x000b,0x7005); /* delay-slot add must execute */
    assert(portable_replay_vm_run(&v,CODE,0,16) && v.r[0]==12);tests++;
    PROGRAM(0x0018,0x8d01,0xe002,0xe009,0x000b,0x7001);
    assert(portable_replay_vm_run(&v,CODE,0,16) && v.r[0]==3);tests++;
    PROGRAM(0x0008,0x8d01,0xe002,0xe009,0x000b,0x7001);
    assert(portable_replay_vm_run(&v,CODE,0,16) && v.r[0]==10);tests++;
    PROGRAM(0x000b,0xa000); /* a control transfer in a delay slot is invalid */
    assert(!portable_replay_vm_run(&v,CODE,0,16) && v.fault==PRVM_DELAY_SLOT);tests++;
    PROGRAM(0xaffe,0x0009);
    assert(!portable_replay_vm_run(&v,CODE,0,9) && v.fault==PRVM_BUDGET && v.instructions==9);tests++;
    PROGRAM(0xffff);
    assert(!portable_replay_vm_run(&v,CODE,0,4) && v.fault==PRVM_OPCODE && v.fault_opcode==0xffff);tests++;
    PROGRAM(0x6012,0x000b,0x0009);v.r[1]=DATA+254;
    assert(!portable_replay_vm_run(&v,CODE,0,16) && v.fault==PRVM_ALIGNMENT);tests++;
    PROGRAM(0x2102,0x000b,0x0009);v.r[1]=CODE;v.r[0]=9;
    assert(!portable_replay_vm_run(&v,CODE,0,16) && v.fault==PRVM_MEMORY && code[0]==2);tests++;
    PROGRAM(0x6116,0x000b,0x0009);v.r[1]=DATA;
    assert(portable_replay_vm_write(&v,DATA,4,1234));
    assert(portable_replay_vm_run(&v,CODE,0,16) && v.r[1]==1234);tests++;
    PROGRAM(0x000b,0x0009);
    assert(portable_replay_vm_write(&v,DATA+252,4,0x87654321));
    assert(portable_replay_vm_read(&v,(DATA&0x1fffffff)+252,4,&x) && x==0x87654321);
    assert(!portable_replay_vm_read(&v,DATA+256,4,&x) && v.fault==PRVM_MEMORY);tests++;
    PROGRAM(0x000b,0x0009);
    assert(!portable_replay_vm_map(&v,DATA+4,data,4,PRVM_READ));
    assert(!portable_replay_vm_map(&v,0xfffffff0u,data,32,PRVM_READ));tests++;
    PROGRAM(0xf02d,0xf002,0x000b,0x0009);v.fpul=3;
    assert(portable_replay_vm_run(&v,CODE,0,16) && v.fr[0]==0x41100000u);tests++;
    PROGRAM(0xf010,0x000b,0x0009);v.fpscr=1;v.fr[0]=0x3f800000;v.fr[1]=0x33c00000;
    assert(portable_replay_vm_run(&v,CODE,0,16) && v.fr[0]==0x3f800000u);tests++;
    PROGRAM(0xf013,0x000b,0x0009);v.fpscr=1;v.fr[0]=0x3f800000;v.fr[1]=0x40400000;
    assert(portable_replay_vm_run(&v,CODE,0,16) && v.fr[0]==0x3eaaaaaau);tests++;
    PROGRAM(0xf02d,0x000b,0x0009);v.fpscr=1;v.fpul=16777219;
    assert(portable_replay_vm_run(&v,CODE,0,16) && v.fr[0]==0x4b800001u);tests++;
    PROGRAM(0xf06d,0x000b,0x0009);v.fpscr=1;v.fr[0]=0x40400000;
    assert(portable_replay_vm_run(&v,CODE,0,16) && v.fr[0]==0x3fddb3d7u);tests++;
    PROGRAM(0xf02d);v.fpscr=1u<<19;
    assert(!portable_replay_vm_run(&v,CODE,0,16) && v.fault==PRVM_FPU_MODE);tests++;
    PROGRAM(0x000b,0x0009);
    PortableReplayCue events[1];PortableReplayCues cues={events,0,1};
    assert(portable_replay_vm_write(&v,DATA+0x18,4,DATA));
    assert(portable_replay_vm_write(&v,DATA+0x98,4,37));
    v.pc=0x8c05d2c0;v.r[4]=DATA;v.r[5]=(6u<<16)|23;
    uint32_t registers[16];memcpy(registers,v.r,sizeof registers);
    assert(portable_replay_cues_observe(&v,&cues)==0 && cues.count==1);
    assert(events[0].tick==37 && events[0].kind==1 && events[0].bank==6 && events[0].program==23);
    assert(v.pc==0x8c05d2c0 && !memcmp(registers,v.r,sizeof registers));
    assert(portable_replay_cues_observe(&v,&cues)==-1 && cues.count==1);tests++;
    PROGRAM(0x000b,0x7005);v.r[0]=7;
    assert(!portable_replay_vm_run(&v,CODE,0,1) && v.fault==PRVM_BUDGET && v.delayed);
    assert(portable_replay_vm_continue(&v,0,4) && v.r[0]==12 && v.instructions==2);tests++;
    PROGRAM(0x002b,0x0009);v.pc=CODE+0x40;v.vbr=CODE-0x600;v.r[0]=3;v.bank[0]=7;
    assert(portable_replay_vm_interrupt(&v,6) && v.r[0]==7 && v.spc==CODE+0x40 && (v.sr&0x70000000)==0x70000000);
    assert(!portable_replay_vm_interrupt(&v,6));
    assert(portable_replay_vm_continue(&v,CODE+0x40,8) && v.r[0]==3 && !v.sr);tests++;
    PROGRAM(0x6012,0x000b,0x0009);v.r[1]=DATA;
    v.read_hook=memory_read_service;v.write_hook=memory_write_service;
    read_services=write_services=0;
    assert(portable_replay_vm_direct_memory(&v,DATA));
    assert(!portable_replay_vm_direct_memory(&v,DATA+4));
    assert(portable_replay_vm_write(&v,DATA,4,0x12345678));
    assert(portable_replay_vm_run(&v,CODE,0,16) && v.r[0]==0x12345678);
    assert(!read_services && !write_services);
    assert(portable_replay_vm_read(&v,CODE,2,&x) && read_services==1);tests++;
    PROGRAM(0x6012,0x000b,0x0009);v.r[1]=DATA+1;
    assert(portable_replay_vm_direct_memory(&v,DATA));
    assert(!portable_replay_vm_run(&v,CODE,0,16) && v.fault==PRVM_ALIGNMENT);tests++;
    PROGRAM(0x2102,0x000b,0x0009);v.r[1]=CODE;v.r[0]=9;
    assert(portable_replay_vm_direct_memory(&v,CODE));
    assert(!portable_replay_vm_run(&v,CODE,0,16) && v.fault==PRVM_MEMORY && code[0]==2);tests++;
    printf("{\"tests\":%u,\"passed\":true}\n",tests);return 0;
}
