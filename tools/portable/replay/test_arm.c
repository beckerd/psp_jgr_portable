/* Authored ARM7 ISA fixtures; no original driver instructions or tables. */
#include "portable_replay_arm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static PortableReplayArm a;
static unsigned char ram[1024],registers[64];
static void put(unsigned p,uint32_t w){for(unsigned i=0;i<4;i++)ram[p+i]=(unsigned char)(w>>(i*8));}
static void init(void){memset(ram,0,sizeof ram);memset(registers,0,sizeof registers);portable_replay_arm_init(&a,ram,sizeof ram,registers,sizeof registers);}
int main(void)
{
    unsigned tests=0;
    init();put(0,0xe3a00007);put(4,0xe2901005); /* mov r0,#7; adds r1,r0,#5 */
    assert(portable_replay_arm_run(&a,8,16) && a.r[1]==12 && !(a.cpsr&0xf0000000));tests++;
    init();a.r[0]=0xffffffff;put(0,0xe2901001);put(4,0x13a02004);put(8,0x03a02009);
    assert(portable_replay_arm_run(&a,12,16) && !a.r[1] && a.r[2]==9 && (a.cpsr&0xf0000000)==0x60000000);tests++;
    init();a.r[1]=0x101;put(0,0xe5910000);put(0x100,0x12345678);
    assert(portable_replay_arm_run(&a,4,16) && a.r[0]==0x78123456);tests++;
    init();a.r[1]=0x800000;a.r[0]=0x89abcdef;put(0,0xe5810000);put(4,0xe5d12001);
    assert(portable_replay_arm_run(&a,8,16) && a.r[2]==0xcd && registers[3]==0x89);tests++;
    init();a.r[0]=1;a.r[1]=2;a.r[2]=3;a.r[13]=0x200;put(0,0xe92d0007);put(4,0xe8bd0038);
    assert(portable_replay_arm_run(&a,8,16) && a.r[13]==0x200 && a.r[3]==1 && a.r[4]==2 && a.r[5]==3);tests++;
    init();put(0,0xeb000000);put(8,0xe1a0f00e);
    assert(portable_replay_arm_run(&a,4,16) && a.r[14]==4);tests++;
    init();a.r[1]=1024;put(0,0xe5910000);assert(!portable_replay_arm_run(&a,4,16) && a.fault==1);tests++;
    init();put(0,0xeafffffe);assert(!portable_replay_arm_run(&a,4,17) && a.fault==4 && a.instructions==17);tests++;
    init();a.cpsr=0x13;a.r[15]=0x80;a.r[8]=99;a.bank[5]=0x200;
    /* FIQ stores interrupted PC, subtracts architectural four, then restores
     * PC/CPSR using the banked stack. */
    put(0x1c,0xe24ee004);put(0x20,0xe92d4000);put(0x24,0xe3a08007);put(0x28,0xe8fd8000);
    assert(portable_replay_arm_fiq(&a,32) && a.r[15]==0x80 && a.r[8]==99 && a.bank[0]==7 && a.cpsr==0x13);tests++;
    init();put(0,0xef000000);assert(!portable_replay_arm_run(&a,4,16) && a.fault==3);tests++;
    printf("{\"tests\":%u,\"passed\":true}\n",tests);return 0;
}
