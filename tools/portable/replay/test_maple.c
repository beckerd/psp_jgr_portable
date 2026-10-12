/* Authored controller-bus packets; no original game payloads. */
#include "portable_replay_maple.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[4096];
static PortableReplayVM vm;
static void put(unsigned at,uint32_t x){assert(portable_replay_vm_write(&vm,0x8c000000+at,4,x));}
static uint32_t get(unsigned at){uint32_t x;assert(portable_replay_vm_read(&vm,0x8c000000+at,4,&x));return x;}
static void init(void){memset(memory,0,sizeof memory);portable_replay_vm_init(&vm);assert(portable_replay_vm_map(&vm,0x8c000000,memory,sizeof memory,3));}
int main(void)
{
    unsigned tests=0;
    init();put(0,0x80000000);put(4,0x8c000100);put(8,0x2001);
    assert(portable_replay_maple_dma(&vm,0x8c000000,1,0xffff));
    assert(get(256)==0x1c200005 && get(260)==0x01000000 && get(264)==0xfe060f00);tests++;
    init();put(0,0x80000001);put(4,0x8c000100);put(8,0x01002009);put(12,0x01000000);
    assert(portable_replay_maple_dma(&vm,0x8c000000,1,0xfffb));
    assert(get(256)==0x03200008 && get(260)==0x01000000 && get(264)==0xfffb && get(268)==0x80808080);tests++;
    init();put(0,0x80000001);put(4,0x8c000100);put(8,0x09200001);put(12,1);
    assert(portable_replay_maple_dma(&vm,0x8c000000,0,0xffff));assert(get(256)==0x08002003 && get(264)==0xffff0000);tests++;
    init();put(0,0x80010000);put(4,0x8c000100);put(8,0x6001);
    assert(portable_replay_maple_dma(&vm,0x8c000000,1,0xffff) && get(256)==0xffffffff);tests++;
    init();put(0,0x80000000);put(4,0x8c000100);put(8,0x01002009);
    assert(!portable_replay_maple_dma(&vm,0x8c000000,1,0xffff) && !get(256));tests++;
    init();put(0,0x80000000);put(4,0x0d000000);put(8,0x2001);
    assert(!portable_replay_maple_dma(&vm,0x8c000000,1,0xffff));tests++;
    init();for(unsigned i=0;i<256;i++)put(i*4,0x700);
    assert(!portable_replay_maple_dma(&vm,0x8c000000,1,0xffff));tests++;
    init();put(0,0x80000000);put(4,0x8c000100);put(8,0x2023);
    assert(portable_replay_maple_dma(&vm,0x8c000000,1,0xffff) && get(256)==0x002000fd);tests++;
    printf("{\"tests\":%u,\"passed\":true}\n",tests);return 0;
}
