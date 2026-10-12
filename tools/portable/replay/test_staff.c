/* Authored observer fixtures. No extracted game values, poses, or programs. */
#include "portable_replay_staff.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned selected_part,tick,complete,reads;
static void put(unsigned char*p,unsigned off,unsigned v)
{p[off]=v;p[off+1]=v>>8;p[off+2]=v>>16;p[off+3]=v>>24;}
static unsigned get(const unsigned char*p,unsigned off)
{return (unsigned)p[off]|(unsigned)p[off+1]<<8|(unsigned)p[off+2]<<16|(unsigned)p[off+3]<<24;}
int portable_replay_bootstrap_read(const PortableReplayBootstrap*b,uint32_t at,void*out,unsigned bytes)
{
    (void)b;if(bytes!=4)return 0;unsigned value=0;reads++;
    if(at==0x8c2e9c9c)value=0x8c500000;
    else if(at==0x8c500004)value=0x1b;
    else if(at==0x8c500024)value=0x8c500100;
    else if(at==0x8c50006c)value=0x8cfe0000+(selected_part+3)*20;
    else if(at==0x8c500108)value=0x8c03e6f8;
    else if(at==0x8c50016c)value=0x8c500200;
    else if(at==0x8c5002b8)value=0x8c510100;
    else if(at==0x8c500344 || at==0x8c500348)value=tick>=100?1:0;
    else if(at==0x8cddf04c)value=0x8c510000;
    else if(at==0x8c510000)value=0x8c510100;
    else if(at==0x8cfe0000+(selected_part+2)*20)value=0x8cfe0400;
    else if(at==0x8cfe0420)value=1;
    put(out,0,value);return 1;
}
void portable_replay_bootstrap_status(const PortableReplayBootstrap*b,PortableReplayStatus*s)
{(void)b;memset(s,0,sizeof *s);s->completed=complete;}
int main(void)
{
    PortableReplayBootstrap*b=(PortableReplayBootstrap*)(void*)&reads;
    unsigned char actor[0x900]={0},camera[0x290]={0},controller[0xd0]={0};
    unsigned part,cut,variant,tests=0;
    assert(!jgr_replay_staff_create(NULL,0) && !jgr_replay_staff_create(b,16));tests++;
    JgrReplayStaff*s=jgr_replay_staff_create(b,2);assert(s);
    put(actor,0x7c,2);put(actor,0x560,0x8c520000);put(camera,0x18,0x8c530000);put(camera,0x274,0x8cfe0500);
    put(controller,0x70,0x8cfe0420);put(controller,0x78,0);put(controller,0x6c,1);
    assert(!jgr_replay_staff_sample(s,actor,camera,controller,&part,&cut,&variant));tests++;
    for(selected_part=0;selected_part<9;selected_part++)for(tick=1;tick<=900;tick++){
        put(actor,0x98,tick);put(camera,0x78,tick);
        assert(jgr_replay_staff_sample(s,actor,camera,controller,&part,&cut,&variant));
        assert(part==selected_part && cut==(tick==1) && variant==0);
        assert(jgr_replay_staff_frame(s));
    }
    FILE*f=tmpfile();assert(f && !jgr_replay_staff_finish(s,f));complete=1;
    assert(jgr_replay_staff_finish(s,f));tests++;
    unsigned char result[4096];rewind(f);size_t bytes=fread(result,1,sizeof result,f);fclose(f);
    assert(bytes==32+9*24+9*32+18*32 && !memcmp(result,"SRT1",4));
    assert(get(result,4)==1 && get(result,8)==2 && get(result,12)==8100 && get(result,16)==9 && get(result,20)==9 && get(result,24)==18);
    for(unsigned i=0;i<9;i++){
        assert(get(result,32+i*24)==i+2 && get(result,36+i*24)==i*900 && get(result,40+i*24)==900);
    }
    unsigned walls=32+9*24+9*32;
    assert(get(result,walls)==1 && get(result,walls+32)==99);tests++;
    jgr_replay_staff_destroy(s);
    s=jgr_replay_staff_create(b,2);selected_part=0;tick=1;put(actor,0x98,1);put(camera,0x78,1);
    assert(jgr_replay_staff_sample(s,actor,camera,controller,&part,&cut,&variant));
    tick=3;put(actor,0x98,3);put(camera,0x78,3);
    assert(!jgr_replay_staff_sample(s,actor,camera,controller,&part,&cut,&variant));tests++;
    jgr_replay_staff_destroy(s);
    printf("{\"tests\":%u,\"passed\":true}\n",tests);return 0;
}
