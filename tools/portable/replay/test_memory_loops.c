/* Authored ISA differential fixtures. No original program bytes are inputs. */
#include "portable_replay_vm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BASE 0x8c400000u
static unsigned char ram[4096],expected[4096];
static unsigned random_state=0x5731821;
static unsigned sink_bytes;
static int sink(PortableReplayVM *v,uint32_t address,unsigned bytes,void *ctx)
{(void)v;(void)ctx;if(address!=0x05001000)return 0;sink_bytes+=bytes;return 1;}
static unsigned next_random(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static void prepare(PortableReplayVM *v,int floating,unsigned width)
{
    /* Deliberately different registers from any installer's selected call site. */
    unsigned words[]={floating?0xf289:0x6286,0x4310,floating?0xf92a:0x2922,0x8ffb,0x7900|width};
    for(unsigned i=0;i<sizeof ram;i++)ram[i]=(unsigned char)next_random();
    for(unsigned i=0;i<5;i++){ram[i*2]=(unsigned char)words[i];ram[i*2+1]=(unsigned char)(words[i]>>8);}
    portable_replay_vm_init(v);
    assert(portable_replay_vm_map(v,BASE,ram,sizeof ram,7));
    assert(portable_replay_vm_direct_memory(v,BASE));
    for(unsigned i=0;i<16;i++){v->r[i]=next_random();v->fr[i]=next_random();v->xf[i]=next_random();}
    v->pc=BASE;v->pr=0x12345678;v->sr=0x70000201;v->fpscr=width==8?1u<<20:0;
    v->r[8]=BASE+256;v->r[9]=BASE+2048;v->r[3]=40;
}
static void compare(const PortableReplayVM *a,const PortableReplayVM *b)
{
    assert(!memcmp(a->r,b->r,sizeof a->r));assert(!memcmp(a->fr,b->fr,sizeof a->fr));
    assert(!memcmp(a->xf,b->xf,sizeof a->xf));
    assert(a->pc==b->pc && a->pr==b->pr && a->sr==b->sr && a->fpscr==b->fpscr);
    assert(a->delay==b->delay && a->delayed==b->delayed && a->instructions==b->instructions);
    assert(a->fault_opcode==b->fault_opcode);assert(!memcmp(ram,expected,sizeof ram));
}
int main(void)
{
    unsigned tests=0;
    for(unsigned f=0;f<3;f++)for(unsigned trial=0;trial<256;trial++){
        PortableReplayVM fast,slow;unsigned width=f==2?8:4;
        prepare(&fast,f!=0,width);fast.r[3]=1+next_random()%100;
        unsigned overlap=trial%4;
        if(overlap==0)fast.r[9]=fast.r[8]+4;
        else if(overlap==1)fast.r[9]=fast.r[8]-4;
        else if(overlap==2)fast.r[9]=fast.r[8];
        unsigned budget=5*(1+next_random()%fast.r[3])+(trial%5);
        slow=fast;memcpy(expected,ram,sizeof ram);slow.region[0].data=expected;slow.direct_memory=slow.region;
        unsigned done=portable_replay_vm_copy_loop(&fast,budget);
        assert(done>=5 && done<=budget);
        assert(!portable_replay_vm_continue(&slow,0xffffffff,done) && slow.fault==PRVM_BUDGET);
        compare(&fast,&slow);tests++;
    }
    for(unsigned trial=0;trial<10;trial++){
        PortableReplayVM v,before;prepare(&v,0,4);
        switch(trial){
        case 0:v.r[3]=0;break;
        case 1:v.r[8]++;break;
        case 2:v.r[9]++;break;
        case 3:v.r[9]=BASE;break; /* self-modifying program */
        case 4:v.r[9]=BASE+4092;break;
        case 5:v.r[8]=0xa0000000;break; /* device/unmapped source */
        case 6:v.delayed=1;break;
        case 7:ram[0]=9;ram[1]=0;break; /* unsupported instruction */
        case 8:v.direct_memory=NULL;break;
        case 9:v.region[0].permissions=PRVM_READ|PRVM_EXEC;break;
        }
        before=v;memcpy(expected,ram,sizeof ram);
        assert(!portable_replay_vm_copy_loop(&v,1000));
        assert(!memcmp(&v,&before,sizeof v));assert(!memcmp(ram,expected,sizeof ram));tests++;
    }
    PortableReplayVM v,before;prepare(&v,0,4);before=v;
    assert(!portable_replay_vm_copy_loop(&v,4) && !memcmp(&v,&before,sizeof v));tests++;
    prepare(&v,0,4);v.r[9]=0x05001000;v.copy_output_hook=sink;sink_bytes=0;
    uint32_t last;assert(portable_replay_vm_read(&v,v.r[8]+39*4,4,&last));
    assert(portable_replay_vm_copy_loop(&v,200)==200 && sink_bytes==160);
    assert(v.r[2]==last && v.r[8]==BASE+416 && v.r[9]==0x050010a0 && v.r[3]==0);tests++;
    prepare(&v,0,4);v.r[9]=0x05001004;v.copy_output_hook=sink;before=v;
    assert(!portable_replay_vm_copy_loop(&v,200) && !memcmp(&v,&before,sizeof v));tests++;
    for(unsigned trial=0;trial<256;trial++){
        PortableReplayVM fast,slow;prepare(&fast,0,4);
        /* mov r8,r9; store r10 at base+r8; add base,r9; add 2,r11;
         * store r10 at r9+4; signed compare r11<r12; loop; add 8,r8. */
        unsigned words[]={0x6983,0x08a6,0x390c,0x7b02,0x19a1,0x3cb7,0x8df8,0x7808};
        for(unsigned i=0;i<8;i++){ram[i*2]=(unsigned char)words[i];ram[i*2+1]=(unsigned char)(words[i]>>8);}
        fast.r[0]=BASE;fast.r[8]=256;fast.r[10]=trial&1?next_random():0;
        fast.r[11]=next_random()%100;fast.r[12]=100+next_random()%100;
        unsigned budget=8*(1+next_random()%50)+(trial%8);
        slow=fast;memcpy(expected,ram,sizeof ram);slow.region[0].data=expected;slow.direct_memory=slow.region;
        unsigned done=portable_replay_vm_fill_loop(&fast,budget);
        assert(done>=8 && done<=budget);
        assert(!portable_replay_vm_continue(&slow,0xffffffff,done) && slow.fault==PRVM_BUDGET);
        compare(&fast,&slow);tests++;
    }
    printf("{\"tests\":%u,\"passed\":true}\n",tests);return 0;
}
