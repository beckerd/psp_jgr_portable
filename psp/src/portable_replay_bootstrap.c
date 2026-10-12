/* Install-only original execution, bootstrapped from the validated user disc. */
#include "portable_replay_bootstrap.h"
#include "portable_replay_staff_reuse.h"
#include "portable_disc.h"
#include "portable_replay_gdrom.h"
#include "portable_replay_arm.h"
#include "portable_replay_maple.h"
#include "portable_replay_cues.h"
#include "portable_replay_vm.h"
#include "portable_replay_scanline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


typedef PortableReplaySelection Selection;
struct PortableReplayBootstrap {
    PortableReplayVM vm;
    struct StaffReuse *staff_reuse;
    unsigned staff_watch;
    PortableReplayFrame frame;
    void *frame_context;
    PortableReplaySample sample;
    void *sample_context;
    Selection selection;
    unsigned active,force_cleanup,stopped_pc,reuse,ending_call;
    unsigned spectator,rival,rival_controller,rival_phase;
    unsigned rival_started,rival_segment,rival_observed,relocate_call;
    PortableReplayGangFixture gang_fixture;
    unsigned gang_fixture_valid,gang_actor[3],gang_controller[3],gang_started[3],gang_segment[3],gang_slot;
    unsigned model_sink,model_calls;
    uint32_t hook_until;
    unsigned hook_urgent;
    uint64_t clock,last_instruction,skipped;
    PortableReplayCue cue_events[700];
    PortableReplayCues cues;
    unsigned camera_return,camera_stack,samples,complete;
    PortableReplayGdrom gd;
    PortableReplayArm arm;
    unsigned frames,main_task,cleanup,requested,selected,player,age,arm_ready;
    uint64_t arm_next;
    unsigned char aica[0x8000];
    uint32_t ta[8],ta_list,intevt,irqs,ccr,mmucr,qacr[2],sq[16];
    uint32_t tmu[12],dmac[17];
    uint64_t tmu_at[3],next_vblank,spg_lines;
    unsigned reads,writes;
    uint64_t discarded_vram_bytes;
    uint32_t intc[4],sb[1536],pvr[1024];
    unsigned char sb_set[1536],pvr_set[1024];
};
typedef struct PortableReplayBootstrap Devices;
static int staff_reuse_hook(Devices *,PortableReplayVM *);
static int staff_record_sample(Devices *,const unsigned char *,const unsigned char *,const unsigned char *);
static uint32_t guest(Devices*d,uint32_t p)
{
    p&=0x1fffffffu;if(p<0x0c000000 || p>0x0cfffffc || (p&3))return 0;
    unsigned char*b=d->gd.ram+(p-0x0c000000);return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
}
static uint32_t task(Devices*d,unsigned id,uint32_t exec)
{
    uint32_t stack[2000];unsigned n=0,visits=0;stack[n++]=guest(d,0x8c2e9c9c);
    while(n && visits++<2000){uint32_t p=stack[--n];if(p<0x8c000000 || p>0x8cffffd8 || (p&3))continue;
        if((id && (guest(d,p+4)&65535)==id) || (exec && guest(d,p+8)==exec))return p;
        if(n>1997)return 0;
        stack[n++]=guest(d,p+0x24);stack[n++]=guest(d,p+0x1c);
    }return 0;
}
static int spectator_relocate(Devices*d,PortableReplayVM*v,const float xyz[3])
{
    uint32_t bits;
    uint32_t record=d->selection.kind==PORTABLE_REPLAY_GANG?0x8cfe7800:0x8cfdf800;
    if(d->relocate_call)return 0;
    for(unsigned i=0;i<=0x30;i+=4)if(!portable_replay_vm_write(v,record+i,4,0))return 0;
    for(unsigned i=0;i<3;i++){memcpy(&bits,&xyz[i],4);if(!portable_replay_vm_write(v,record+8+i*4,4,bits))return 0;}
    return portable_replay_vm_write(v,d->spectator+8,4,0x8cfe0040);
}
static int spectator_follow(Devices*d,PortableReplayVM*v,uint32_t position,unsigned yaw,double distance,int initial)
{
    float xyz[3];uint32_t bits;
    for(unsigned i=0;i<3;i++){bits=guest(d,position+i*4);memcpy(&xyz[i],&bits,4);}
    double angle=(double)yaw*(6.283185307179586476925286766559/65536.);
    xyz[0]=(float)(xyz[0]+sin(angle)*distance);xyz[2]=(float)(xyz[2]+cos(angle)*distance);
    if(initial)return spectator_relocate(d,v,xyz);
    for(unsigned i=0;i<3;i++){
        memcpy(&bits,&xyz[i],4);
        if(!portable_replay_vm_write(v,d->spectator+0xb8+i*4,4,bits) ||
           !portable_replay_vm_write(v,d->spectator+0xc4+i*4,4,bits))return 0;
    }
    return portable_replay_vm_write(v,d->spectator+0x168,4,yaw);
}
static int select_rival(Devices*d,PortableReplayVM*v)
{
    if(d->player)d->spectator=task(d,0x6a,0);
    if(!d->spectator)return 1;
    if(!d->rival){
        uint32_t actor=task(d,107,0),controller=guest(d,actor+0x18);
        if(controller && guest(d,controller+8)==0x8c0278c0 && guest(d,controller+0x84)==3 && guest(d,controller+0x90)==107 && actor &&
           guest(d,guest(d,actor+0x850)+8)==0x8c01dc40){
            d->rival=d->player=actor;d->rival_controller=controller;
        }
    }
    if(d->rival && d->samples){
        unsigned segment=d->rival_segment,started=d->rival_started;
        if(started){
            if(segment>=2)return 0;
            d->rival_phase=segment;
            if(d->samples>2 && d->rival_observed!=d->samples){
                if(!spectator_follow(d,v,d->rival+0xb8,guest(d,d->rival+0x168),60,0))return 0;
                d->rival_observed=d->samples;
            }
        }else if(d->rival_phase==1 && segment==0)d->complete=1;
        else if(d->age>250 && (d->rival_phase==UINT32_MAX || segment==1) && d->rival_phase!=segment){
            uint32_t table=guest(d,d->rival_controller+0xbc),stream=guest(d,table+segment*12);
            if(!spectator_follow(d,v,stream+8,guest(d,stream+24),30,1))return 0;
            d->rival_phase=segment;
        }
    }
    return 1;
}
static int select_gang(Devices*d,PortableReplayVM*v)
{
    if(d->player)d->spectator=task(d,0x6a,0);
    if(!d->spectator)return 1;
    if(d->age>=2100)for(unsigned i=0;i<3;i++)if(!d->gang_actor[i]){
        uint32_t actor=task(d,107+i,0),controller=guest(d,actor+0x18);
        if(actor && guest(d,controller+8)==0x8c0278c0 && guest(d,controller+0x84)==3 &&
           guest(d,controller+0x90)==107+i && guest(d,guest(d,actor+0x850)+8)==0x8c01dc40){
            d->gang_actor[i]=actor;d->gang_controller[i]=controller;
        }
    }
    if(d->age==2200){
        if(!d->gang_actor[0] || !d->gang_actor[1] || !d->gang_actor[2] ||
           !spectator_relocate(d,v,d->gang_fixture.approach[0]))return 0;
        d->rival_phase=0;
    }
    if(d->rival_phase!=UINT32_MAX){
        if((d->selection.stage==1 || d->selection.chapter==4) && d->age%240==0){
            for(unsigned i=0;i<3;i++)if(guest(d,d->gang_controller[i]+0xc0)==d->rival_phase && !guest(d,d->gang_controller[i]+0xb8)){
                if(!spectator_follow(d,v,d->gang_actor[i]+0xb8,0,40,1))return 0;
                break;
            }
        }
        unsigned next=(d->rival_phase+1)%3,ready=1;
        for(unsigned i=0;i<3;i++)if(guest(d,d->gang_controller[i]+0xc0)!=next || guest(d,d->gang_controller[i]+0xb8))ready=0;
        if(ready){
            if(d->rival_phase==2){d->complete=1;return 1;}
            d->rival_phase++;
            if(!spectator_relocate(d,v,d->gang_fixture.approach[d->rival_phase]))return 0;
        }
    }
    /* Reproduce the explicit capture fixture's idle entry-clock setting.
     * Active controller records, player updates, contact tests, and all pose
     * evaluation still execute from the original user-supplied image. */
    unsigned ending=1;
    for(unsigned i=0;i<3;i++)if(d->gang_actor[i] && guest(d,d->gang_controller[i]+0xb8)){
        unsigned remaining=guest(d,d->gang_controller[i]+0x78)+1,record=guest(d,d->gang_controller[i]+0x70)+4,visits=0;
        while(remaining<=2 && record>=0x8c000000 && record<0x8d000000 && !(guest(d,record)&0x80) && visits++<3){
            remaining+=(guest(d,record)&255)+1;record+=4;
        }
        if(remaining>2 || record<0x8c000000 || record>=0x8d000000)ending=0;
    }
    for(unsigned i=0;i<3;i++)if(d->gang_actor[i]){
        unsigned section=guest(d,d->gang_controller[i]+0xc0);if(section>=3)return 0;
        unsigned target=d->gang_fixture.idle_phase[i][section];
        if(target && !guest(d,d->gang_controller[i]+0xb8) && guest(d,d->gang_actor[i]+0x78)==3 &&
           !d->gang_started[i] && d->gang_segment[i]==section &&
           ((d->rival_phase==UINT32_MAX && section==0) || section==d->rival_phase || (ending && section==d->rival_phase+1))){
            float clock=(float)(target-1);uint32_t bits;memcpy(&bits,&clock,4);
            if(!portable_replay_vm_write(v,d->gang_actor[i]+0x5ac,4,bits))return 0;
        }
    }
    return 1;
}
static int select_demo(Devices*d,PortableReplayVM*v)
{
#define PUT(a,x) do{if(!portable_replay_vm_write(v,(a),4,(x)))return 0;}while(0)
    d->frames++;
    if(d->selection.kind==PORTABLE_REPLAY_TITLE){
        uint32_t main=task(d,6,0),state=guest(d,main+0xa8);
        if(main && state>=0x8c015b7c && state<=0x8c015dd4){
            PUT(main+0x78,d->selection.character);d->requested=1;
        }
        if(d->samples && !task(d,0x1b,0))d->complete=1;
        return 1;
    }
    if(!d->main_task && d->frames>360 && task(d,6,0) && (d->force_cleanup || task(d,0x1b,0))){
        d->force_cleanup=0;d->main_task=task(d,6,0);uint32_t child=guest(d,d->main_task+0x1c);unsigned count=0;
        while(child && count++<2000){PUT(child,guest(d,child)|1);child=guest(d,child+0x24);}
        if(child)return 0;
        PUT(d->main_task+0xa8,0x8cfe0000);d->cleanup=1;
        if(d->selection.kind!=PORTABLE_REPLAY_STAFF){
            PUT(d->main_task+0x74,0x222);PUT(0x8c2e997c,1);PUT(0x8c2e9984,1);PUT(0x8c2e998c,1);
        }
        
    }
    if(d->cleanup && !guest(d,d->main_task+0x1c)){
        PUT(d->main_task+0xa8,d->selection.kind==PORTABLE_REPLAY_STAFF?0x8cfe0020:0x8c015ef4);d->cleanup=0;
    }
    if(d->selection.kind==PORTABLE_REPLAY_STAFF){
        if(task(d,0x9ff,0) && !d->requested){PUT(d->main_task+0x78,d->selection.character);d->requested=1;}
        if(d->samples && !task(d,0x1b,0))d->complete=1;
        return 1;
    }
    if(!d->reuse && d->main_task && !d->requested && guest(d,d->main_task+0xa8)==0x8c014e20){
        PUT(d->main_task+0x78,d->selection.character);PUT(d->main_task+0x7c,d->selection.stage-1);PUT(d->main_task+0x80,d->selection.chapter);PUT(d->main_task+0xa8,0x8c015480);d->requested=1;
        
    }
    uint32_t loader=task(d,0,0x8c0ca2ac);
    if(d->selection.kind!=PORTABLE_REPLAY_GANG && d->requested && loader && !d->selected && guest(d,loader+0x6c)==2){PUT(loader+0x90,d->selection.record);PUT(loader+0x94,d->selection.subset);PUT(loader+0x6c,1);d->selected=1;}
    if(d->requested && !d->player){
        uint32_t p=task(d,0,0x8c0198f8),md=guest(d,0x8c17ea98);
        if(p && guest(d,md)==0x63444d4a && guest(d,md+8)==d->selection.stage-1 && guest(d,p+0x850)){
            d->player=p;
        }
    }
    if(d->player)d->age++;
    if(d->selection.kind==PORTABLE_REPLAY_RIVAL)return select_rival(d,v);
    if(d->selection.kind==PORTABLE_REPLAY_GANG)return select_gang(d,v);
    if(d->samples>30 && guest(d,guest(d,0x8c17ea98)+0x14)==4)d->complete=1;
    return 1;
#undef PUT
}
static int ta_packet(Devices*d,uint32_t header)
{
    unsigned type=header>>29;
    if((type==4 || type==5) && d->ta_list==7)d->ta_list=(header>>24)&7;
    else if(type==0){
        if(d->ta_list==7)d->ta_list=(header>>24)&7;
        if(d->ta_list>4)return 0;
        d->sb[0x100/4]|=1u<<(d->ta_list==4?21:7+d->ta_list);d->ta_list=7;
    }
    return 1;
}
static int maple(Devices*d,PortableReplayVM*v)
{
    uint16_t buttons=0xffff;
    if(d->selection.kind==PORTABLE_REPLAY_DEMONSTRATION && d->player && d->age>=200 && d->age<=204)buttons&=~4u;
    if(d->selection.kind==PORTABLE_REPLAY_RIVAL && d->spectator && d->age>=200 &&
       (!d->rival || d->samples<2) && d->age%120<5)buttons&=~4u;
    if(d->selection.kind==PORTABLE_REPLAY_GANG && d->spectator && d->age>=200 &&
       ((d->age<2100 && d->age%120<5) || d->age<=204))buttons&=~4u;
    if(!portable_replay_maple_dma(v,d->sb[0x404/4],d->sb[0x4e8/4]&1,buttons)){
        return 0;
    }
    d->sb[0x418/4]=0;d->sb[0x100/4]|=1u<<12;return 1;
}
static int flush_queue(PortableReplayVM*v,uint32_t a,void*ctx)
{
    Devices*d=ctx;unsigned q=(a>>5)&1;
    uint32_t dest=(a&0x03ffffe0u)|((d->qacr[q]&0x1c)<<24);
    for(unsigned i=0;i<8;i++)if(!portable_replay_vm_write(v,dest+i*4,4,d->sq[q*8+i]))return 0;
    return 1;
}
static int copy_output(PortableReplayVM*v,uint32_t a,unsigned bytes,void*ctx)
{
    Devices*d=ctx;uint32_t p=a&0x1fffffffu;(void)v;
    if(a>=0xe0000000u || bytes>0x800000)return 0;
    if((p>=0x04000000 && p<=0x04800000-bytes) ||
       (p>=0x05000000 && p<=0x05800000-bytes)){
        d->discarded_vram_bytes+=bytes;return 1;
    }
    return 0;
}
static int service_due(PortableReplayVM*v,void*ctx)
{
    Devices*d=ctx;uint32_t pc=v->pc&0x1fffffffu;
    d->clock+=v->instructions-d->last_instruction;d->last_instruction=v->instructions;
    /* Collapse the SDK's observed no-op idle callback only while its original
     * render/vblank wait condition remains pending. Every device event and
     * original interrupt handler still runs. No game callback is skipped. */
    if(pc==0x0c224330 && (v->pr==0x8c226240 || v->pr==0x8c226280) &&
       guest(d,0x8c32daa4)==v->pc && guest(d,0x8c32dbb0) && d->next_vblank){
        unsigned waiting=v->pr==0x8c226240?guest(d,0x8c32dbac)<=guest(d,0x8c32da60)+guest(d,0x8c32da64)+1:guest(d,0x8c32dbac)<=20;
        uint64_t next=d->next_vblank;
        if(d->arm_ready && d->arm_next<next)next=d->arm_next;
        if(waiting && next>d->clock){
            uint32_t load=d->pvr_set[0xd8/4]?d->pvr[0xd8/4]:0x01060359;
            uint32_t interrupts=d->pvr_set[0xcc/4]?d->pvr[0xcc/4]:0x00150104;
            next=portable_replay_scanline_idle(&d->next_vblank,&d->spg_lines,
                ((load>>16)&1023)+1,interrupts&1023,(interrupts>>16)&1023,
                d->arm_ready?d->arm_next:UINT64_MAX);
        }
        if(waiting && next>d->clock){d->skipped+=next-d->clock;d->clock=next;}
    }
    if(d->complete){d->stopped_pc=v->pc;v->pc=0xfffffff0;return 1;}
    /* Task draw callbacks are a separate traversal from original task update,
     * player pose/collision, camera, graffiti and audio progression. Discard
     * that presentation traversal during installation. Every replay family,
     * including the STAFF wall-reveal stream, is checked against full output. */
    if(d->model_sink && pc==0x0c0b305c){v->pc=v->pr;return 1;}
    /* Vendor model submission transforms vertices/strips into rendering
     * scratch. Preserve the enclosing object/matrix traversal and every
     * game callback; only this headless output is discarded. */
    if(d->model_sink && (pc==0x0c23592c || pc==0x0c294208 || pc==0x0c235aa8 || pc==0x0c234ea8)){
        d->model_calls++;v->r[0]=0;v->pc=v->pr;return 1;
    }
    /* The non-callback object walkers only push/pop the vendor matrix stack
     * around the model submission leaves above. Animated walkers with pose
     * callbacks are deliberately outside this set. */
    if(d->model_sink && (pc==0x0c235982 || pc==0x0c294262 || pc==0x0c235c14 || pc==0x0c234f04)){
        d->model_calls++;v->pc=v->pr;return 1;
    }
    /* Bounded vendor texture-handle lookup. The table and handles remain
     * original, disc-loaded RAM; this is a generic fixed-stride key search. */
    if(pc==0x0c27e006){
        uint32_t base=v->r[4],count=v->r[5],key=v->r[6],value;
        if(count>0x1000000/0x44 || (base && count &&
            ((base&0x1fffffffu)<0x0c000000 || (base&0x1fffffffu)>0x0d000000-count*0x44)))return -1;
        v->r[0]=0xffffffffu;
        if(base && count){
            for(uint32_t i=0;i<count;i++){
                if(!portable_replay_vm_read(v,base+i*0x44,4,&value))return -1;
                if(value==key){v->r[0]=i;break;}
            }
        }
        v->pc=v->pr;return 1;
    }
    /* The vendor texture compactor copies VRAM pixels through a temporary
     * stack buffer. Its caller retains all texture allocation/bookkeeping;
     * the pixel-only relocation has no output in this headless context. */
    if(d->model_sink && pc==0x0c22192a){
        if(v->r[4]>0x800000 || v->r[5]>0x800000 ||
           v->r[6]>0x800000-v->r[4] || v->r[6]>0x800000-v->r[5])return -1;
        v->pc=v->pr;return 1;
    }
    if(pc==0x0cfe0000){v->pc=v->pr;return 1;}
    if((d->selection.kind==PORTABLE_REPLAY_RIVAL || d->selection.kind==PORTABLE_REPLAY_GANG) && pc==0x0cfe0040){
        if(d->relocate_call || !portable_replay_vm_write(v,v->r[15]-4,4,v->pr) ||
           !portable_replay_vm_write(v,v->r[15]-8,4,v->r[14]))return -1;
        v->r[15]-=8;v->r[14]=v->r[4];v->pr=0x8cfe0042;v->pc=0x8c0198f8;d->relocate_call=1;return 1;
    }
    if((d->selection.kind==PORTABLE_REPLAY_RIVAL || d->selection.kind==PORTABLE_REPLAY_GANG) && pc==0x0cfe0042){
        if(d->relocate_call==1){d->relocate_call=2;v->r[4]=v->r[14];v->r[5]=d->selection.kind==PORTABLE_REPLAY_GANG?0x8cfe7800:0x8cfdf800;v->r[6]=1;v->pc=0x8c01aeb0;v->pr=0x8cfe0042;}
        else if(d->relocate_call==2){d->relocate_call=3;v->r[4]=v->r[14];v->r[5]=1;v->pc=0x8c06bd04;v->pr=0x8cfe0042;}
        else if(d->relocate_call==3){
            if(!portable_replay_vm_write(v,v->r[14]+8,4,0x8c0198f8) ||
               !portable_replay_vm_read(v,v->r[15],4,&v->r[14]) || !portable_replay_vm_read(v,v->r[15]+4,4,&v->pr))return -1;
            v->r[15]+=8;v->pc=v->pr;d->relocate_call=0;
        }else return -1;
        return 1;
    }
    if(d->selection.kind==PORTABLE_REPLAY_STAFF && pc==0x0c0ee90c){
        /* Authored scenario selection: enter STAFF instead of movie playback. */
        v->r[5]=2;v->pc+=2;return 1;
    }
    if(d->selection.kind==PORTABLE_REPLAY_STAFF && pc==0x0cfe0020){
        if(d->ending_call || !portable_replay_vm_write(v,v->r[15]-4,4,v->pr) ||
           !portable_replay_vm_write(v,v->r[15]-8,4,v->r[14]))return -1;
        v->r[15]-=8;v->r[14]=v->r[4];v->r[4]=4;v->pr=0x8cfe0022;v->pc=0x8c0170a0;d->ending_call=1;return 1;
    }
    if(d->selection.kind==PORTABLE_REPLAY_STAFF && pc==0x0cfe0022){
        if(d->ending_call==1){d->ending_call=2;v->pc=0x8c0d91bc;v->pr=0x8cfe0022;}
        else if(d->ending_call==2){d->ending_call=3;v->r[4]=v->r[14];v->pc=0x8c016314;v->pr=0x8cfe0022;}
        else if(d->ending_call==3){
            if(!portable_replay_vm_read(v,v->r[15],4,&v->r[14]) || !portable_replay_vm_read(v,v->r[15]+4,4,&v->pr))return -1;
            v->r[15]+=8;v->pc=v->pr;d->ending_call=0;
        }else return -1;
        return 1;
    }
    if(d->reuse && !d->requested && d->main_task && pc==0x0c014e20 && v->r[4]==d->main_task){
        if(!portable_replay_vm_write(v,d->main_task+0x78,4,d->selection.character) ||
           !portable_replay_vm_write(v,d->main_task+0x7c,4,d->selection.stage-1) ||
           !portable_replay_vm_write(v,d->main_task+0x80,4,d->selection.chapter) ||
           !portable_replay_vm_write(v,d->main_task+0xa8,4,0x8c015480))return -1;
        d->requested=1;v->pc=0x8c015480;return 1;
    }
    if((d->selection.kind==PORTABLE_REPLAY_TITLE || d->selection.kind==PORTABLE_REPLAY_STAFF) && d->requested && pc==0x0c01dc40){
        uint32_t parent=guest(d,v->r[4]+0x18);
        if(guest(d,parent+8)==0x8c0198f8 && (d->selection.kind!=PORTABLE_REPLAY_STAFF || guest(d,v->r[4]+0x274)))d->player=parent;
    }
    if(d->selection.kind==PORTABLE_REPLAY_GANG && pc==0x0c01dc40){
        unsigned parent=guest(d,v->r[4]+0x18);
        for(unsigned i=0;i<3;i++)if(d->gang_actor[i] && d->gang_actor[i]==parent){d->player=parent;d->gang_slot=i;}
    }
    if(d->player && d->sample && (d->selection.kind!=PORTABLE_REPLAY_RIVAL || d->rival) &&
       (d->selection.kind!=PORTABLE_REPLAY_GANG || d->gang_actor[d->gang_slot]==d->player)){
        if(d->selection.kind==PORTABLE_REPLAY_DEMONSTRATION && portable_replay_cues_observe(v,&d->cues)<0)return -1;
        if(d->camera_return && v->pc==d->camera_return && v->r[15]==d->camera_stack){
            uint32_t camera=guest(d,d->player+0x850),controller=guest(d,d->player+0x18),pose=guest(d,d->player+0x594);
            uint32_t addresses[4]={d->player,camera,controller,pose};unsigned sizes[4]={0x900,0x290,0xd0,0x400};
            const unsigned char *views[4];
            for(unsigned i=0;i<4;i++){
                uint32_t p=addresses[i]&0x1fffffffu;
                if(p<0x0c000000 || p>0x0d000000-sizes[i])return -1;
                views[i]=d->gd.ram+(p-0x0c000000);
            }
            if(!staff_record_sample(d,views[0],views[1],views[2]) ||
               !d->sample(d->sample_context,views[0],views[1],views[2],views[3]))return -1;
            if(d->selection.kind==PORTABLE_REPLAY_RIVAL){d->rival_started=guest(d,d->rival_controller+0xb8);d->rival_segment=guest(d,d->rival_controller+0xc0);}
            if(d->selection.kind==PORTABLE_REPLAY_GANG){
                unsigned i=d->gang_slot;d->gang_started[i]=guest(d,d->gang_controller[i]+0xb8);d->gang_segment[i]=guest(d,d->gang_controller[i]+0xc0);
            }
            d->samples++;d->camera_return=0;
            if(d->selection.kind==PORTABLE_REPLAY_DEMONSTRATION && d->samples>30 && guest(d,guest(d,0x8c17ea98)+0x14)==4){d->complete=1;d->stopped_pc=v->pc;v->pc=0xfffffff0;return 1;}
        }
        if(pc==0x0c01dc40 && v->r[4]==guest(d,d->player+0x850) &&
           (d->selection.kind!=PORTABLE_REPLAY_STAFF || guest(d,v->r[4]+0x274))){
            if(d->camera_return)return -1;
            d->camera_return=v->pr;d->camera_stack=v->r[15];
        }
    }
    if(d->next_vblank && d->clock>=d->next_vblank){
        uint32_t load=d->pvr_set[0xd8/4]?d->pvr[0xd8/4]:0x01060359;
        uint32_t interrupts=d->pvr_set[0xcc/4]?d->pvr[0xcc/4]:0x00150104;
        unsigned line=(unsigned)(++d->spg_lines)%(((load>>16)&1023)+1);
        if(line==(interrupts&1023))d->sb[0x100/4]|=8;
        if(line==((interrupts>>16)&1023)){d->sb[0x100/4]|=16;if(!select_demo(d,v))return -1;
            if(d->frame && !d->frame(d->frame_context))return -1;
            if((d->sb[0x410/4]&1) && (d->sb[0x414/4]&1) && !maple(d,v))return -1;
        }
        d->next_vblank+=3175;
    }
    if(d->arm_ready && d->clock>=d->arm_next){
        d->arm_next=d->clock+(3175u*525u)/4;
        d->aica[0x2d00]=2; /* timer A FIQ level selected by original driver */
        if(!portable_replay_arm_fiq(&d->arm,10000) || !portable_replay_arm_run(&d->arm,0x194,100000)){return -1;}
    }
    unsigned priority=0;
    if(d->sb[0x100/4]&d->sb[0x130/4])priority=6;
    else if(d->sb[0x100/4]&d->sb[0x120/4])priority=4;
    else if(d->sb[0x100/4]&d->sb[0x110/4])priority=2;
    if(priority && portable_replay_vm_interrupt(v,priority)){d->intevt=0x3e0-priority*0x20;d->irqs++;return 1;}
    if(pc==0x0c001006){
        if(!portable_replay_gdrom_service(&d->gd,v)){return -1;}
        v->pc=v->pr;return 1;
    }
    /* Explicit headless system profile: anonymous console ID. User flash
     * settings are absent; unsupported services still stop. */
    if(pc==0x0c001004 && v->r[7]<=3){v->r[0]=UINT32_MAX;v->pc=v->pr;return 1;} /* no persistent console flash */
    if(pc==0x0c001000 && v->r[7]==0){v->r[0]=0;v->pc=v->pr;return 1;}
    if(pc==0x0c001000 && v->r[7]==3){v->r[0]=0x8c000068;v->pc=v->pr;return 1;}
    if(pc>=0x0c001000 && pc<=0x0c00100a){return -1;}
    return 0;
}
static int service(PortableReplayVM*v,void*ctx)
{
    Devices*d=ctx;uint32_t pc=v->pc&0x1fffffffu;
    if(d->staff_reuse && (pc==0x0c03ed98 || pc==0x0c051d48 || v->pc==d->staff_watch)){
        int handled=staff_reuse_hook(d,v);if(handled)return handled;
    }
    if(!d->hook_urgent && (int32_t)((uint32_t)v->instructions-d->hook_until)<0 && v->pc!=d->camera_return){
        switch(pc){
        case 0x0c001000:case 0x0c001002:case 0x0c001004:case 0x0c001006:case 0x0c001008:case 0x0c00100a:
        case 0x0c014e20:case 0x0c01dc40:case 0x0c05d288:case 0x0c05d2c0:case 0x0c05d2f8:case 0x0c05d330:
        case 0x0c0b305c:case 0x0c235982:case 0x0c294262:case 0x0c235c14:case 0x0c234f04:case 0x0c27e006:
        case 0x0c0ee90c:case 0x0c22192a:case 0x0c224330:case 0x0c234ea8:case 0x0c23592c:case 0x0c235aa8:case 0x0c294208:
        case 0x0c223d10:case 0x0c223d20:case 0x0c0b2a7e:
        case 0x0cfe0000:case 0x0cfe0020:case 0x0cfe0022:case 0x0cfe0040:case 0x0cfe0042:break;
        default:return 0;
        }
    }
    int result=service_due(v,ctx);
    d->hook_urgent=d->complete || (d->sb[0x100/4]&(d->sb[0x110/4]|d->sb[0x120/4]|d->sb[0x130/4]))!=0;
    uint64_t now=d->clock+v->instructions-d->last_instruction,next=now+1000000;
    if(d->next_vblank && d->next_vblank<next)next=d->next_vblank;
    if(d->arm_ready && d->arm_next<next)next=d->arm_next;
    d->hook_until=(uint32_t)v->instructions+(next>now?(uint32_t)(next-now):1);
    if(!result && !d->hook_urgent && (pc==0x0c223d10 || pc==0x0c223d20))
        return portable_replay_vm_copy_loop(v,d->hook_until-(uint32_t)v->instructions)!=0;
    if(!result && !d->hook_urgent && pc==0x0c0b2a7e)
        return portable_replay_vm_fill_loop(v,d->hook_until-(uint32_t)v->instructions)!=0;
    return result;
}
static int device_read(PortableReplayVM*v,uint32_t a,unsigned n,uint32_t*x,void*ctx)
{
    Devices*d=ctx;(void)v;
    if(a<0xe0000000u && (a&0x1f000000u)==0x0c000000u)return 0;
    d->hook_urgent=1;
    if(a==0xff000028 && n==4){*x=d->intevt;return 1;}
    if(a>=0xf4000000 && a<0xf4004000 && n==4){*x=0;return 1;} /* no dirty data-cache tags in coherent model */
    if(a>=0xffa00000 && a<=0xffa00040 && n==4 && !(a&3)){*x=d->dmac[(a-0xffa00000)/4];return 1;}
    if(a>=0xffd80000 && a<0xffd80030 && !(a&3)){
        unsigned index=(a-0xffd80000)/4;*x=d->tmu[index];
        if(index==3 || index==6 || index==9){unsigned ch=(index-3)/3;if(d->tmu[1]&(1u<<ch)){
            unsigned scale=d->tmu[index+1]&7;if(scale>4)return -1;
            uint64_t ticks=(d->clock+v->instructions-d->last_instruction-d->tmu_at[ch])*6250/40005;
            *x-=(uint32_t)(ticks>>(scale*2));
        }}return 1;
    }
    if(a==0xff000010 && n==4){*x=d->mmucr;return 1;}
    if(a==0xff00001c && n==4){*x=d->ccr;d->reads++;return 1;}
    if(a>=0xffd00000 && a<=0xffd0000c && !(a&3) && n==2){*x=d->intc[(a-0xffd00000)/4];return 1;}
    if(a==0xff800030 && n==2){*x=0;return 1;} /* explicit VGA cable setting */
    uint32_t p=a&0x1fffffffu;
    if((p==0x00710000 || p==0x00710004) && n==4){*x=0;return 1;} /* explicit install clock epoch */
    if(p==0x005f6900 && n==4){*x=d->sb[0x100/4];return 1;}
    if(p==0x005f8138 && n==4){*x=d->pvr[0x128/4];return 1;} /* output sink allocates no ISP geometry */
    if(p==0x005f8000 && n==4){*x=0x17fd11db;return 1;} /* PowerVR2 device identity */
    if(p==0x005f8004 && n==4){*x=0x11;return 1;}
    if(p==0x005f80e8 && n==4 && !d->pvr_set[0xe8/4]){*x=0x108;return 1;} /* video-output control reset */
    if(p==0x005f689c && n==4){*x=0xb;return 1;} /* system-bus revision */
    if(p==0x005f688c && n==4){*x=0;return 1;} /* no outstanding FIFO writes */
    if(n==4 && p>=0x005f7800 && p<0x005f7880 && !(p&3)){*x=d->sb[(p-0x005f6800)/4];return 1;} /* G2 DMA registers reset idle */
    if(p==0x005f810c && n==4){
        if(!d->next_vblank){d->spg_lines=(d->clock+v->instructions-d->last_instruction)/3175;d->next_vblank=(d->spg_lines+1)*3175;}
        uint32_t load=d->pvr_set[0xd8/4]?d->pvr[0xd8/4]:0x01060359;
        uint32_t blank=d->pvr_set[0xdc/4]?d->pvr[0xdc/4]:0x01500104;
        unsigned line=(unsigned)d->spg_lines%(((load>>16)&1023)+1),start=blank&1023,end=(blank>>16)&1023;
        int vsync=start>end?(line>=start || line<end):(line>=start && line<end);
        *x=line|(vsync?1u<<13:0);return 1;
    }
    if(p>=0x005f6800 && p<0x005f8000 && n==4 && !(p&3)) {
        unsigned i=(p-0x005f6800)/4;if(d->sb_set[i]){*x=d->sb[i];return 1;}
    }
    if(p>=0x005f8000 && p<0x005f9000 && n==4 && !(p&3)) {
        unsigned i=(p-0x005f8000)/4;if(d->pvr_set[i]){*x=d->pvr[i];return 1;}
    }
    return 0;
}
static int device_write(PortableReplayVM*v,uint32_t a,unsigned n,uint32_t x,void*ctx)
{
    Devices*d=ctx;(void)v;
    if(a<0xe0000000u && (a&0x1f000000u)==0x0c000000u)return 0;
    d->hook_urgent=1;
    if((a==0xff000038 || a==0xff00003c) && n==4){d->qacr[(a>>2)&1]=x;return 1;}
    if((a&0xfc000000u)==0xe0000000u && n==4 && !(a&3)){d->sq[(a&63)/4]=x;return 1;}
    if(a>=0xffa00000 && a<=0xffa00040 && n==4 && !(a&3)){d->dmac[(a-0xffa00000)/4]=x;return 1;}
    if(a>=0xffd80000 && a<0xffd80030 && !(a&3)){
        unsigned index=(a-0xffd80000)/4;
        if(index==1)for(unsigned ch=0;ch<3;ch++)if((d->tmu[1]^x)&(1u<<ch))d->tmu_at[ch]=d->clock+v->instructions-d->last_instruction;
        if(index==3 || index==6 || index==9)d->tmu_at[(index-3)/3]=d->clock+v->instructions-d->last_instruction;
        d->tmu[index]=x;return 1;
    }
    if((a>=0xf4000000 && a<0xf4004000) && n==4 && x==0)return 1; /* invalidate data-cache tags */
    if(a==0xff000010 && n==4){if(x&1)return -1;d->mmucr=x;return 1;}
    if(a==0xff00001c && n==4){d->ccr=x;d->writes++;return 1;}
    if(a>=0xffd00000 && a<=0xffd0000c && !(a&3) && n==2){d->intc[(a-0xffd00000)/4]=x;return 1;}
    uint32_t p=a&0x1fffffffu;
    if(p==0x00702c00){
        unsigned before=d->aica[0x2c00]&1;
        for(unsigned j=0;j<n;j++)d->aica[0x2c00+j]=(unsigned char)(x>>(j*8));
        if(before && !(x&1)){
            if(!portable_replay_arm_run(&d->arm,0x194,2000000)){return -1;}
            d->arm_ready=1;
        }return 1;
    }
    if(p>=0x10000000 && p<0x11000000 && n==4){
        unsigned word=(p&31)/4;d->ta[word]=x;
        if(word==7 && !ta_packet(d,d->ta[0]))return -1;
        return 1;
    }
    if((p>=0x04000000 && p<0x04800000) || (p>=0x05000000 && p<0x05800000)){d->discarded_vram_bytes+=n;return 1;} /* headless output sink; VRAM reads remain unsupported */
    if(p>=0x005f6800 && p<0x005f8000 && n==4 && !(p&3)) {
        unsigned i=(p-0x005f6800)/4;d->sb_set[i]=1;
        if(p==0x005f6c18 && (x&1) && (d->sb[0x414/4]&1))return maple(d,v)?1:-1;
        if(p==0x005f7818 && (x&1)){
            unsigned base=0x1000/4;uint32_t src=d->sb[base+1],dst=d->sb[base],bytes=d->sb[base+2]&0x7fffffffu;
            if(d->sb[base+3]){uint32_t t=src;src=dst;dst=t;}
            if(bytes>2u*1024*1024 || (bytes&3))return -1;
            for(uint32_t j=0;j<bytes;j+=4){uint32_t word;if(!portable_replay_vm_read(v,src+j,4,&word) || !portable_replay_vm_write(v,dst+j,4,word))return -1;}
            if(d->sb[base+2]&0x80000000u)d->sb[base+5]=0;
            d->sb[base]+=bytes;d->sb[base+1]+=bytes;d->sb[base+2]=d->sb[base+6]=0;
            d->sb[0x100/4]|=1u<<15;return 1;
        }
        if(p==0x005f6808 && (x&1)){
            uint32_t src=d->dmac[0x20/4]&0x1fffffffu,bytes=d->sb[1];
            if(src<0x0c000000 || src-0x0c000000>0x1000000 || bytes>0x1000000-(src-0x0c000000))return -1;
            if(!(d->sb[0]&0x01000000u))for(uint32_t j=0;j+32<=bytes;j+=32){uint32_t h;
                if(!portable_replay_vm_read(v,src+j,4,&h) || !ta_packet(d,h))return -1;
            }
            d->discarded_vram_bytes+=bytes;d->dmac[0x2c/4]|=2;d->dmac[0x28/4]=0;
            d->sb[1]=d->sb[2]=0;d->sb[0x100/4]|=1u<<19;return 1;
        }
        if(p==0x005f6900 || p==0x005f6908)d->sb[i]&=~x;else d->sb[i]=x;
        return 1;
    }
    if(p>=0x005f8000 && p<0x005f9000 && n==4 && !(p&3)) {
        unsigned i=(p-0x005f8000)/4;d->pvr_set[i]=1;d->pvr[i]=x;d->writes++;
        if(p==0x005f8014 && x)d->sb[0x100/4]|=7; /* completed headless render */
        return 1;
    }
    return 0;
}

#include "portable_replay_staff_reuse.inc"

PortableReplayBootstrap *portable_replay_bootstrap_create(JgrDisc*disc,const uint32_t*hardware,unsigned words)
{
    if(!disc || !hardware || words!=32768)return NULL;
    Devices*d=calloc(1,sizeof *d);if(!d)return NULL;
    unsigned char *ram=calloc(1,16u*1024*1024),*aram=calloc(1,2u*1024*1024);
    portable_replay_gdrom_init(&d->gd,disc,ram,16u*1024*1024);
    portable_replay_arm_init(&d->arm,aram,2u*1024*1024,d->aica,sizeof d->aica);
    PortableReplayVM*v=&d->vm;portable_replay_vm_init(v);d->ta_list=7;d->model_sink=1;
    JgrDiscFile executable;uint32_t first,count;
    if(!ram || !aram || jgr_disc_find(disc,"1ST_READ.BIN",&executable)<0 || executable.size>16u*1024*1024-65536 ||
       jgr_disc_read(disc,&executable,0,ram+65536,executable.size)<0 ||
       jgr_disc_sector_bounds(disc,&first,&count)<0 || jgr_disc_sectors(disc,first,16,ram+0x8000,32768)<0 ||
       !portable_replay_vm_map(v,0x8c000000,ram,16u*1024*1024,7) ||
       !portable_replay_vm_map(v,0x00800000,aram,2u*1024*1024,3) ||
       !portable_replay_vm_map(v,0x00700000,d->aica,sizeof d->aica,3))goto failed;
    v->fsca_positive=hardware;v->fsca_words=words;
    v->r[15]=0x8d000000;v->sr=0x400000f1;v->fpscr=0x40001;v->gbr=v->vbr=0x8c000000;
    for(unsigned i=0;i<6;i++){
        unsigned offset=i<5?0xb0+i*4:0xe0;
        if(!portable_replay_vm_write(v,0x8c000000+offset,4,0x8c001000+i*2))goto failed;
    }
    v->hook=service;v->context=d;v->store_queue_hook=flush_queue;v->store_queue_context=d;
    v->copy_output_hook=copy_output;v->copy_output_context=d;
    if(!portable_replay_vm_direct_memory(v,0x8c000000))goto failed;
    v->read_hook=device_read;v->read_context=d;v->write_hook=device_write;v->write_context=d;
    if(!portable_replay_vm_run(v,0x8c00e07c,0xac010000,256))goto failed;
    d->cues.events=d->cue_events;d->cues.capacity=700;return d;
failed:
    portable_replay_bootstrap_destroy(d);return NULL;
}
int portable_replay_bootstrap_select(Devices*d,const Selection*s,PortableReplaySample sample,void*context)
{
    if(!d || !s || !sample || (d->active && !d->complete) || !s->stage || s->stage>6 ||
       s->chapter>63 || s->record>23 || s->subset>31 || s->character>15 || s->kind>PORTABLE_REPLAY_GANG ||
       (s->kind==PORTABLE_REPLAY_GANG && !d->gang_fixture_valid))return 0;
    if(d->staff_reuse){if(d->staff_reuse->mode!=4)return 0;staff_reuse_release(d);}
    if(d->complete){if(d->vm.pc==0xfffffff0)d->vm.pc=d->stopped_pc;d->force_cleanup=1;d->reuse=1;}
    d->selection=*s;d->sample=sample;d->sample_context=context;d->active=1;
    d->main_task=d->cleanup=d->requested=d->selected=d->player=d->age=0;
    d->camera_return=d->camera_stack=d->samples=d->complete=d->cues.count=0;
    d->spectator=d->rival=d->rival_controller=d->rival_started=d->rival_segment=d->rival_observed=d->relocate_call=0;d->rival_phase=UINT32_MAX;
    memset(d->gang_actor,0,sizeof d->gang_actor);memset(d->gang_controller,0,sizeof d->gang_controller);
    memset(d->gang_started,0,sizeof d->gang_started);memset(d->gang_segment,0,sizeof d->gang_segment);d->gang_slot=0;
    return 1;
}
int portable_replay_bootstrap_gang_fixture(Devices*d,const PortableReplayGangFixture*f)
{
    if(!d || !f || (d->active && !d->complete))return 0;
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)if(!isfinite(f->approach[i][j]) || f->idle_phase[i][j]>65535)return 0;
    d->gang_fixture=*f;d->gang_fixture_valid=1;return 1;
}
int portable_replay_bootstrap_step(Devices*d,unsigned budget)
{
    if(!d || !d->active || !budget)return -1;
    if(d->complete)return 1;
    if(d->staff_reuse && d->staff_reuse->mode==3)return staff_pose_step(d,budget);
    int ok=portable_replay_vm_continue(&d->vm,0xfffffff0,budget);
    if(ok)return d->complete?1:(d->staff_reuse && d->staff_reuse->mode==3?0:-1);
    return d->vm.fault==PRVM_BUDGET?0:-1;
}
void portable_replay_bootstrap_status(const Devices*d,PortableReplayStatus*s)
{
    if(!s)return;
    memset(s,0,sizeof *s);if(!d)return;
    s->instructions=d->vm.instructions;s->virtual_clock=d->clock;s->idle_ticks_skipped=d->skipped;
    s->pr=d->vm.pr;s->sp=d->vm.r[15];for(unsigned i=0;i<3;i++)s->argument[i]=d->vm.r[4+i];
    s->sectors_read=d->gd.sectors_read;s->frames=d->frames;s->samples=d->samples;s->cues=d->cues.count;s->completed=d->complete;
    s->main_task=d->main_task;s->main_callback=guest((Devices*)d,d->main_task+0xa8);s->player=d->player;
    s->cleanup=d->cleanup;s->requested=d->requested;s->selected=d->selected;
    if(d->vm.fault!=PRVM_BUDGET){s->fault=d->vm.fault;s->pc=d->vm.fault_pc;s->address=d->vm.fault_address;s->opcode=d->vm.fault_opcode;}
}
const PortableReplayCue *portable_replay_bootstrap_cues(const Devices*d,unsigned*n)
{if(n)*n=d?d->cues.count:0;return d?d->cue_events:NULL;}
int portable_replay_bootstrap_read(const Devices*d,uint32_t a,void*destination,unsigned bytes)
{
    a&=0x1fffffffu;
    if(!d || !destination || a<0x0c000000 || a>=0x0d000000 || bytes>0x0d000000-a)return 0;
    memcpy(destination,d->gd.ram+(a-0x0c000000),bytes);return 1;
}
void portable_replay_bootstrap_destroy(Devices*d)
{if(d){staff_reuse_release(d);free(d->arm.ram);free(d->gd.ram);memset(d,0,sizeof *d);free(d);}}

void portable_replay_bootstrap_frame_observer(Devices*d,PortableReplayFrame f,void*context)
{if(d){d->frame=f;d->frame_context=context;}}
