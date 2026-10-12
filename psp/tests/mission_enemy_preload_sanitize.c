/* Original synthetic missions exercise the real loading-only adapter. */
#include "../src/mission_enemy_preload.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char image[32768],groups[32768],before[32768];
static unsigned group_bytes,next_image;
static MissionState state;
static void put(unsigned char *p,uint32_t value)
{for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(value>>(i*8));}
static void word(unsigned address,uint32_t value)
{assert(address>=state.base && address-state.base<=sizeof image-4);put(image+(address-state.base),value);}
static unsigned alloc_image(unsigned bytes)
{unsigned address=state.base+next_image;next_image+=bytes;assert(next_image<=sizeof image);return address;}
static void reset(void)
{
    memset(image,0,sizeof image);memset(groups,0,sizeof groups);state=(MissionState){0};
    state.img=image;state.len=sizeof image;state.base=0x8c100000;state.ra=state.base;
    state.active=1;state.task_parents=1;next_image=0x200;
    memcpy(groups,"GRP1",4);group_bytes=8;
}
static unsigned group(unsigned type,unsigned members,int parked)
{
    unsigned index=group_u32(groups+4);put(groups+4,index+1);
    unsigned char *p=groups+group_bytes;put(p,1);p+=4;put(p,type);put(p+40,members);
    for(unsigned i=0;i<members;i++)put(p+44+i*28+24,parked && i ? 1:0);
    group_bytes+=48+28*members;assert(group_bytes<=sizeof groups);return index;
}
static unsigned event(unsigned opcode,unsigned argument)
{unsigned p=alloc_image(64);word(p,opcode);word(p+4,argument);return p;}
static void registered(unsigned ev)
{unsigned p=alloc_image(8);word(p,ev);word(state.ra+0x60,p);}
static uint64_t mask(void)
{
    memcpy(before,image,sizeof image);
    uint64_t result=mission_enemy_preload_kinds(&state,groups,group_bytes);
    assert(!memcmp(before,image,sizeof image));return result;
}
static void test_current_record_and_delayed_events(void)
{
    reset();unsigned patrol=group(17,1,0),bomb=group(5,1,0);group(22,1,0);
    word(state.ra+0x5c,event(2,patrol));
    unsigned trigger=alloc_image(24);word(trigger,3);word(trigger+4,6);
    word(trigger+8,event(3,bomb));word(trigger+12,UINT32_MAX);word(state.ra+0x3c,trigger);
    assert(mask()==((UINT64_C(1)<<17)|(UINT64_C(1)<<5)));
    /* A second record uses the same image and bank but has no enemies. */
    state.ra=state.base+0x100;assert(mask()==0);
    state.ra=state.base;state.task_parents=0;assert(mask()==0);
}
static void test_all_trigger_roots(void)
{
    static const unsigned rows[][5]={
        {0x34,20,16,0,0},{0x38,24,20,0,0},{0x3c,12,8,1,0},
        {0x40,16,12,0,0},{0x44,8,4,1,0},{0x48,16,12,0,0},
        {0x4c,16,12,0,0},{0x50,12,8,0,0}};
    for(unsigned i=0;i<sizeof rows/sizeof *rows;i++) {
        reset();unsigned index=group(32,1,0),p=alloc_image(rows[i][1]*2);
        word(state.ra+rows[i][0],p);word(p,rows[i][3] ? 0:1);
        word(p+rows[i][2],event(3,index));
        if(rows[i][3])word(p+rows[i][1],UINT32_MAX);
        assert(mask()==(UINT64_C(1)<<32));
    }
    for(unsigned field=0x54;field<=0x60;field+=4) {
        reset();unsigned ev=event(2,group(8,1,0));
        if(field==0x60)registered(ev);else word(state.ra+field,ev);
        assert(mask()==(UINT64_C(1)<<8));
    }
}
static void test_member_filter_and_aliases(void)
{
    for(unsigned type=0;type<=32;type++) {
        reset();unsigned ev=event(2,group(type,type>=26 && type<=30 ? 2:1,0));
        registered(ev);assert(mask()==(UINT64_C(1)<<type));
    }
    for(unsigned type=26;type<=30;type++) {
        reset();registered(event(2,group(type,1,0)));assert(mask()==0);
        reset();registered(event(2,group(type,2,1)));
        assert(mask()==((type==26 || type==27 || type==29) ? 0:UINT64_C(1)<<type));
    }
    reset();unsigned index=group(29,3,1);
    /* A parked child followed by one live armed child still needs its bank. */
    put(groups+8+48+2*28+24,0);registered(event(2,index));assert(mask()==(UINT64_C(1)<<29));
    reset();group(17,1,0);unsigned formation=group(28,2,0);
    word(state.ra+0x5c,event(6,1000+formation));assert(mask()==(UINT64_C(1)<<28));
    word(state.ra+0x5c,event(6,1000));assert(mask()==0); /* EV6 cannot create patrol actor. */
}
static void test_waves_and_non_spawns(void)
{
    reset();group(17,1,0);registered(event(0x21,2));assert(mask()==0);
    word(state.ra+0x50,alloc_image(12));assert(mask()==(UINT64_C(1)<<44));
    registered(event(0x21,16));assert(mask()==0);
    const unsigned codes[]={1,4,5,7,9,0xc,0x14,0x15,0x17,0x1d,0x1f,0x22,0x23};
    for(unsigned i=0;i<sizeof codes/sizeof *codes;i++) {
        reset();group(17,1,0);registered(event(codes[i],0));assert(mask()==0);
    }
}
static void test_all_subrecords_and_invalid_bank(void)
{
    reset();group(17,1,0);
    /* Append another subrecord to this group, not another group index. */
    put(groups+8,2);unsigned char *p=groups+group_bytes;
    put(p,32);put(p+40,1);group_bytes+=44+28;
    registered(event(2,0));assert(mask()==((UINT64_C(1)<<17)|(UINT64_C(1)<<32)));
    put(p+40,257);assert(mask()==0); /* Reject entire malformed selected record. */
    reset();group(17,0,0);registered(event(2,0));assert(mask()==0);
    reset();group(17,1,0);registered(event(2,0));groups[0]='?';assert(mask()==0);
}
static void test_bounds_and_limits(void)
{
    reset();group(17,1,0);registered(event(2,64));assert(mask()==0);
    registered(event(3,UINT32_MAX));assert(mask()==0);
    word(state.ra+0x5c,state.base+sizeof image-31);assert(mask()==0);
    word(state.ra+0x5c,state.base-4);assert(mask()==0);
    unsigned ev=event(2,0);word(state.ra+0x5c,ev);assert(mask()==(UINT64_C(1)<<17));
    assert(!mission_enemy_preload_kinds(&state,groups,group_bytes-1));
    assert(!mission_enemy_preload_kinds(&state,NULL,0));
    state.active=0;assert(mask()==0);state.active=1;
    state.ra=UINT32_MAX-31;assert(mask()==0);
    state.base=state.ra;assert(mask()==0); /* No wrapped virtual header. */
    assert(!mission_enemy_preload_kinds(NULL,groups,group_bytes));
    reset();group(17,1,0);ev=alloc_image(129*32+32);
    for(unsigned i=0;i<129;i++)word(ev+i*32,0x2c);
    word(ev+128*32,2);word(state.ra+0x5c,ev);assert(mask()==0);
    word(ev+127*32,2);assert(mask()==(UINT64_C(1)<<17));
    reset();for(unsigned i=0;i<64;i++)group(i==63?32:0,1,0);
    registered(event(2,63));assert(mask()==(UINT64_C(1)<<32));
}
static void test_delayed_traffic(void)
{
    reset();state.task_parents|=4;
    unsigned trigger=alloc_image(24);word(trigger,30);word(trigger+8,event(9,2));
    word(trigger+12,UINT32_MAX);word(state.ra+0x3c,trigger);
    assert(mission_preload_traffic(&state));assert(mask()==0);
    state.task_parents&=~4u;assert(!mission_preload_traffic(&state));
    state.task_parents|=4;state.ra=state.base+0x100;
    assert(!mission_preload_traffic(&state)); /* Same image, unrelated record. */
    reset();state.task_parents|=4;registered(event(9,0));
    assert(mission_preload_traffic(&state));
    state.active=0;assert(!mission_preload_traffic(&state));
    assert(!mission_preload_traffic(NULL));
}
int main(void)
{
    test_current_record_and_delayed_events();test_all_trigger_roots();
    test_member_filter_and_aliases();test_waves_and_non_spawns();
    test_all_subrecords_and_invalid_bank();test_bounds_and_limits();
    test_delayed_traffic();
    puts("mission enemy preload coverage/bounds checks passed");return 0;
}
