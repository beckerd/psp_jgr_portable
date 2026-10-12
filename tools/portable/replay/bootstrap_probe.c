/* Disc-only original startup diagnostic. No initialized game RAM input. */
#include "portable_disc.h"
#include "portable_replay_gdrom.h"
#include "portable_replay_arm.h"
#include "portable_replay_maple.h"
#include "portable_replay_cues.h"
#include "portable_replay_vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __PSP__
#include <pspkernel.h>
#include <pspdebug.h>
PSP_MODULE_INFO("JGR Disc Replay Proof",0,1,0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(-512);
#endif

typedef struct {unsigned stage,chapter,record,subset,character;} Selection;
typedef struct {Selection selection;unsigned model_sink,model_calls;unsigned profile[4096];uint64_t clock,last_instruction,skipped;FILE *route,*cue_file;PortableReplayCue cue_events[700];PortableReplayCues cues;unsigned camera_return,camera_stack,samples,complete;PortableReplayGdrom gd;PortableReplayArm arm;unsigned frames,main_task,cleanup,requested,selected,player,age;unsigned arm_ready;uint64_t arm_next;unsigned char aica[0x8000];uint32_t ta[8],ta_list,intevt,irqs,ccr,mmucr,qacr[2],sq[16];uint32_t tmu[12],dmac[17];uint64_t tmu_at[3];uint64_t next_vblank,spg_lines;unsigned reads,writes;uint64_t discarded_vram_bytes;uint32_t intc[4];uint32_t sb[1536];unsigned char sb_set[1536];uint32_t pvr[1024];unsigned char pvr_set[1024];} Devices;
static uint32_t guest(Devices*d,uint32_t p)
{
    p&=0x1fffffffu;if(p<0x0c000000 || p>0x0cfffffc || (p&3))return 0;
    unsigned char*b=d->gd.ram+p-0x0c000000;return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
}
static uint32_t task(Devices*d,unsigned id,uint32_t exec)
{
    uint32_t stack[2000];unsigned n=0,visits=0;stack[n++]=guest(d,0x8c2e9c9c);
    while(n && visits++<2000){uint32_t p=stack[--n];if(p<0x8c000000 || p>0x8cffffd8 || (p&3))continue;
        if((id && (guest(d,p+4)&65535)==id) || (exec && guest(d,p+8)==exec))return p;
        if(n>1997)return 0;stack[n++]=guest(d,p+0x24);stack[n++]=guest(d,p+0x1c);
    }return 0;
}
static int select_demo(Devices*d,PortableReplayVM*v)
{
#define PUT(a,x) do{if(!portable_replay_vm_write(v,(a),4,(x)))return 0;}while(0)
    d->frames++;
    if(!d->main_task && d->frames>360 && task(d,6,0) && task(d,0x1b,0)){
        d->main_task=task(d,6,0);uint32_t child=guest(d,d->main_task+0x1c);unsigned count=0;
        while(child && count++<2000){PUT(child,guest(d,child)|1);child=guest(d,child+0x24);}
        if(child)return 0;
        PUT(d->main_task+0xa8,0x8cfe0000);d->cleanup=1;PUT(d->main_task+0x74,0x222);
        PUT(0x8c2e997c,1);PUT(0x8c2e9984,1);PUT(0x8c2e998c,1);
        fprintf(stderr,"Original stage cleanup requested frame=%u main=%08x\n",d->frames,d->main_task);
    }
    if(d->cleanup && !guest(d,d->main_task+0x1c)){PUT(d->main_task+0xa8,0x8c015ef4);d->cleanup=0;}
    if(d->main_task && !d->requested && guest(d,d->main_task+0xa8)==0x8c014e20){
        PUT(d->main_task+0x78,d->selection.character);PUT(d->main_task+0x7c,d->selection.stage-1);PUT(d->main_task+0x80,d->selection.chapter);PUT(d->main_task+0xa8,0x8c015480);d->requested=1;
        fprintf(stderr,"Original replay stage/mission loader requested frame=%u\n",d->frames);
    }
    uint32_t loader=task(d,0,0x8c0ca2ac);
    if(loader && !d->selected && guest(d,loader+0x6c)==2){PUT(loader+0x90,d->selection.record);PUT(loader+0x94,d->selection.subset);PUT(loader+0x6c,1);d->selected=1;}
    if(d->requested && !d->player){
        uint32_t p=task(d,0,0x8c0198f8),md=guest(d,0x8c17ea98);
        if(p && guest(d,md)==0x63444d4a && guest(d,md+8)==d->selection.stage-1 && guest(d,p+0x850)){
            d->player=p;fprintf(stderr,"DISC-ONLY PLAYER READY frame=%u actor=%08x camera=%08x\n",d->frames,p,guest(d,p+0x850));
        }
    }
    if(d->player)d->age++;
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
    if(d->player && d->age>=200 && d->age<=204)buttons&=~4u;
    if(!portable_replay_maple_dma(v,d->sb[0x404/4],d->sb[0x4e8/4]&1,buttons)){
        fprintf(stderr,"Maple DMA failed at %08x\n",d->sb[0x404/4]);return 0;
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
static int service(PortableReplayVM*v,void*ctx)
{
    Devices*d=ctx;uint32_t pc=v->pc&0x1fffffffu;
    if(pc>=0x0c000000 && pc<0x0d000000)d->profile[(pc-0x0c000000)>>12]++;
    d->clock+=v->instructions-d->last_instruction;d->last_instruction=v->instructions;
    /* Collapse the SDK's observed no-op idle callback only while its original
     * render/vblank wait condition remains pending. Every device event and
     * original interrupt handler still runs. No game callback is skipped. */
    if(pc==0x0c224330 && (v->pr==0x8c226240 || v->pr==0x8c226280) &&
       guest(d,0x8c32daa4)==v->pc && guest(d,0x8c32dbb0) && d->next_vblank){
        unsigned waiting=v->pr==0x8c226240?guest(d,0x8c32dbac)<=guest(d,0x8c32da60)+guest(d,0x8c32da64)+1:guest(d,0x8c32dbac)<=20;
        uint64_t next=d->next_vblank;
        if(d->arm_ready && d->arm_next<next)next=d->arm_next;
        if(waiting && next>d->clock){d->skipped+=next-d->clock;d->clock=next;}
    }
    if(d->complete){v->pc=0xfffffff0;return 1;}
    if(d->model_sink && (pc==0x0c23592c || pc==0x0c294208 || pc==0x0c235aa8 || pc==0x0c234ea8)){
        d->model_calls++;v->r[0]=0;v->pc=v->pr;return 1;
    }
    if(pc==0x0cfe0000){v->pc=v->pr;return 1;}
    if(d->player && !d->route){v->pc=0xfffffff0;return 1;}
    if(d->player && d->route){
        if(portable_replay_cues_observe(v,&d->cues)<0)return -1;
        if(d->camera_return && v->pc==d->camera_return && v->r[15]==d->camera_stack){
            uint32_t camera=guest(d,d->player+0x850),controller=guest(d,d->player+0x18),pose=guest(d,d->player+0x594);
            uint32_t addresses[4]={d->player,camera,controller,pose};unsigned sizes[4]={0x900,0x290,0xd0,0x400};
            for(unsigned i=0;i<4;i++){
                uint32_t p=addresses[i]&0x1fffffffu;
                if(p<0x0c000000 || p>0x0d000000-sizes[i] || fwrite(d->gd.ram+p-0x0c000000,1,sizes[i],d->route)!=sizes[i])return -1;
            }
            d->samples++;d->camera_return=0;
            if(d->samples%60==0){fprintf(stderr,"Route sample=%u clock=%u instructions=%llu\n",d->samples,guest(d,d->player+0x98),(unsigned long long)v->instructions);fflush(d->route);}
            if(d->samples>30 && guest(d,guest(d,0x8c17ea98)+0x14)==4){d->complete=1;v->pc=0xfffffff0;return 1;}
        }
        if(pc==0x0c01dc40 && v->r[4]==guest(d,d->player+0x850)){
            if(d->camera_return)return -1;d->camera_return=v->pr;d->camera_stack=v->r[15];
        }
    }
    if(d->next_vblank && d->clock>=d->next_vblank){
        uint32_t load=d->pvr_set[0xd8/4]?d->pvr[0xd8/4]:0x01060359;
        uint32_t interrupts=d->pvr_set[0xcc/4]?d->pvr[0xcc/4]:0x00150104;
        unsigned line=(unsigned)(++d->spg_lines)%(((load>>16)&1023)+1);
        if(line==(interrupts&1023))d->sb[0x100/4]|=8;
        if(line==((interrupts>>16)&1023)){d->sb[0x100/4]|=16;if(!select_demo(d,v))return -1;
            if((d->sb[0x410/4]&1) && (d->sb[0x414/4]&1) && !maple(d,v))return -1;
        }
        d->next_vblank+=3175;
    }
    if(d->arm_ready && d->clock>=d->arm_next){
        d->arm_next=d->clock+(3175u*525u)/4;
        d->aica[0x2d00]=2; /* timer A FIQ level selected by original driver */
        if(!portable_replay_arm_fiq(&d->arm,10000) || !portable_replay_arm_run(&d->arm,0x194,100000)){fprintf(stderr,"ARM control fault=%u pc=%08x address=%08x opcode=%08x instructions=%llu\n",d->arm.fault,d->arm.pc,d->arm.address,d->arm.opcode,(unsigned long long)d->arm.instructions);return -1;}
    }
    unsigned priority=0;
    if(d->sb[0x100/4]&d->sb[0x130/4])priority=6;
    else if(d->sb[0x100/4]&d->sb[0x120/4])priority=4;
    else if(d->sb[0x100/4]&d->sb[0x110/4])priority=2;
    if(priority && portable_replay_vm_interrupt(v,priority)){d->intevt=0x3e0-priority*0x20;d->irqs++;return 1;}
    if(pc==0x0c001006){
        if(!portable_replay_gdrom_service(&d->gd,v)){fprintf(stderr,"GDROM unsupported op=%u command=%u args=%u,%u,%u,%u\n",v->r[7],d->gd.command,d->gd.parameter[0],d->gd.parameter[1],d->gd.parameter[2],d->gd.parameter[3]);return -1;}
        v->pc=v->pr;return 1;
    }
    /* Explicit headless system profile: anonymous console ID. User flash
     * settings are absent; unsupported services still stop. */
    if(pc==0x0c001004 && v->r[7]<=3){v->r[0]=UINT32_MAX;v->pc=v->pr;return 1;} /* no persistent console flash */
    if(pc==0x0c001000 && v->r[7]==0){v->r[0]=0;v->pc=v->pr;return 1;}
    if(pc==0x0c001000 && v->r[7]==3){v->r[0]=0x8c000068;v->pc=v->pr;return 1;}
    if(pc>=0x0c001000 && pc<=0x0c00100a){fprintf(stderr,"BIOS service pc=%08x pr=%08x r4=%08x r5=%08x r6=%08x r7=%08x\n",v->pc,v->pr,v->r[4],v->r[5],v->r[6],v->r[7]);return -1;}
    return 0;
}
static int device_read(PortableReplayVM*v,uint32_t a,unsigned n,uint32_t*x,void*ctx)
{
    Devices*d=ctx;(void)v;
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
            if(!portable_replay_arm_run(&d->arm,0x194,2000000)){fprintf(stderr,"ARM init fault=%u pc=%08x address=%08x opcode=%08x instructions=%llu\n",d->arm.fault,d->arm.pc,d->arm.address,d->arm.opcode,(unsigned long long)d->arm.instructions);return -1;}
            d->arm_ready=1;fprintf(stderr,"ARM original initialization completed: %llu instructions\n",(unsigned long long)d->arm.instructions);
        }return 1;
    }
    if(p>=0x10000000 && p<0x11000000 && n==4){
        unsigned word=(p&31)/4;d->ta[word]=x;
        if(word==7 && !ta_packet(d,d->ta[0]))return -1;return 1;
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
int main(int argc,char**argv)
{
#ifdef __PSP__
    (void)argc;(void)argv;pspDebugScreenInit();pspDebugScreenPrintf("Disc-only replay installation proof\n");
    char disc_path[512];FILE *settings=fopen("disc-path.txt","r");
    if(!settings || !fgets(disc_path,sizeof disc_path,settings))return 2;
    fclose(settings);disc_path[strcspn(disc_path,"\r\n")]=0;
    char *arguments[]={"bootstrap-proof",disc_path,"fsca-hardware.bin"};argc=3;argv=arguments;
    if(!freopen("bootstrap.log","w",stderr) || !freopen("replay-result.tmp","w",stdout))return 2;
#endif
    setvbuf(stderr,NULL,_IONBF,0);fprintf(stderr,"Opening original disc\n");
    JgrDisc*disc=NULL;JgrDiscFile exe;char error[192];
    if(argc<2 || argc>3){fprintf(stderr,"usage: bootstrap-probe original.gdi [hardware-fsca.bin]\n");return 2;}
    if(jgr_disc_open(&disc,argv[1],error,sizeof error)<0){fprintf(stderr,"%s\n",error);return 2;}
    fprintf(stderr,"Disc verified; allocating original work memory\n");
    unsigned char*ram=calloc(1,16u*1024*1024);
    unsigned char*aram=calloc(1,2u*1024*1024);
    PortableReplayVM v;static Devices d;d.ta_list=7;d.model_sink=getenv("JGR_REPLAY_RENDER_SINK")!=NULL;d.selection.stage=1;d.selection.chapter=10;
    const char *selection=getenv("JGR_REPLAY_SELECTION");
    if(selection && (sscanf(selection,"%u %u %u %u %u",&d.selection.stage,&d.selection.chapter,&d.selection.record,&d.selection.subset,&d.selection.character)!=5 ||
        !d.selection.stage || d.selection.stage>6 || d.selection.chapter>63 || d.selection.record>23 || d.selection.subset>31 || d.selection.character>15))return 2;
    portable_replay_vm_init(&v);
    uint32_t *hardware=NULL;
    fprintf(stderr,"RAM allocated: %p %p\n",(void*)ram,(void*)aram);
    if(argc==3){hardware=malloc(32768*4);FILE*f=fopen(argv[2],"rb");if(!hardware || !f || fread(hardware,4,32768,f)!=32768 || fgetc(f)!=EOF){fprintf(stderr,"External CPU hardware table read failed\n");return 2;}fclose(f);v.fsca_positive=hardware;v.fsca_words=32768;}
    fprintf(stderr,"CPU hardware reference loaded; loading original executable\n");
    if(!ram || !aram || jgr_disc_find(disc,"1ST_READ.BIN",&exe)<0 || exe.size>16u*1024*1024-65536 ||
       jgr_disc_read(disc,&exe,0,ram+65536,exe.size)<0){
        fprintf(stderr,"original executable load: %s\n",jgr_disc_error(disc));return 2;
    }
    portable_replay_gdrom_init(&d.gd,disc,ram,16u*1024*1024);
    portable_replay_arm_init(&d.arm,aram,2u*1024*1024,d.aica,sizeof d.aica);
    portable_replay_vm_map(&v,0x8c000000,ram,16u*1024*1024,7);
    portable_replay_vm_map(&v,0x00800000,aram,2u*1024*1024,PRVM_READ|PRVM_WRITE);
    portable_replay_vm_map(&v,0x00700000,d.aica,sizeof d.aica,PRVM_READ|PRVM_WRITE);
    /* Execute the original IP.BIN handoff to derive its application-entry
     * stack, vector base, register bank and floating-point mode. */
    uint32_t first_lba,sector_count;
    if(jgr_disc_sector_bounds(disc,&first_lba,&sector_count)<0 || jgr_disc_sectors(disc,first_lba,16,ram+0x8000,32768)<0)return 2;
    v.r[15]=0x8d000000;v.sr=0x400000f1;v.fpscr=0x40001;v.gbr=v.vbr=0x8c000000;
    for(unsigned i=0;i<6;i++){unsigned offset=i<5?0xb0+i*4:0xe0;portable_replay_vm_write(&v,0x8c000000+offset,4,0x8c001000+i*2);}
    v.hook=service;v.context=&d;v.store_queue_hook=flush_queue;v.store_queue_context=&d;
    v.read_hook=device_read;v.read_context=&d;v.write_hook=device_write;v.write_context=&d;
    int handed=portable_replay_vm_run(&v,0x8c00e07c,0xac010000,256);
    if(!handed){fprintf(stderr,"original IP handoff failed\n");return 2;}
    fprintf(stderr,"IP handoff original stack=%08x SR=%08x FPSCR=%08x VBR=%08x\n",v.r[15],v.sr,v.fpscr,v.vbr);
    const char *route_path=getenv("JGR_REPLAY_ROUTE");
#ifdef __PSP__
    route_path="disc-route.bin";
#endif
    if(route_path){
        d.route=fopen(route_path,"wb");if(!d.route)return 2;
        static const unsigned char header[16]={'T','A','C','2',0,9,0,0,0x90,2,0,0,0xd0,0,0,0};
        if(fwrite(header,1,sizeof header,d.route)!=sizeof header)return 2;
        char cue_path[1024];if(snprintf(cue_path,sizeof cue_path,"%s.cues",route_path)>=(int)sizeof cue_path)return 2;
        d.cue_file=fopen(cue_path,"wb");if(!d.cue_file)return 2;
    }
    d.cues.events=d.cue_events;d.cues.capacity=700;
    uint64_t budget=16000000000ull;const char*limit=getenv("JGR_REPLAY_BUDGET");if(limit)budget=strtoull(limit,NULL,0);
    int ok=0;
    while(budget){unsigned slice=budget>1000000000?1000000000:(unsigned)budget;
        ok=portable_replay_vm_continue(&v,0xfffffff0,slice);budget-=slice;
        if(ok || v.fault!=PRVM_BUDGET)break;
    }
    if(d.route){fclose(d.route);fwrite(d.cue_events,sizeof *d.cue_events,d.cues.count,d.cue_file);fclose(d.cue_file);}
    fprintf(stderr,"Headless model calls=%u\n",d.model_calls);
    fprintf(stderr,"Route samples=%u cues=%u complete=%u skipped_idle=%llu\n",d.samples,d.cues.count,d.complete,(unsigned long long)d.skipped);
    printf("{\"game_inputs_disc_only\":true,\"external_cpu_hardware_reference\":true,\"complete_route\":%s,\"returned\":%s,\"instructions\":%llu,\"fault\":%u,\"pc\":%u,\"address\":%u,\"opcode\":%u,\"ccr_reads\":%u,\"ccr_writes\":%u,\"registers\":[",d.complete?"true":"false",ok?"true":"false",(unsigned long long)v.instructions,v.fault,v.fault_pc,v.fault_address,v.fault_opcode,d.reads,d.writes);
    for(unsigned i=0;i<16;i++)printf("%s%u",i?",":"",v.r[i]);puts("]}");
    fprintf(stderr,"frames=%u task6=%08x task1b=%08x root=%08x\n",d.frames,task(&d,6,0),task(&d,0x1b,0),guest(&d,0x8c2e9c9c));
    fprintf(stderr,"main callback=%08x state=%08x\n",guest(&d,task(&d,6,0)+0xa8),guest(&d,task(&d,6,0)+0x74));
    {uint32_t st[2000];unsigned sp=0,seen=0;st[sp++]=guest(&d,0x8c2e9c9c);while(sp && seen<40){uint32_t t=st[--sp];if(t<0x8c000000 || t>0x8cffffd0)continue;seen++;fprintf(stderr,"task %08x id=%x exec=%08x sub=%08x state=%08x\n",t,guest(&d,t+4),guest(&d,t+8),guest(&d,t+0x6c),guest(&d,t+0x70));st[sp++]=guest(&d,t+0x24);st[sp++]=guest(&d,t+0x1c);}}
    fprintf(stderr,"PVR cc=%08x d0=%08x d8=%08x dc=%08x global=%08x,%08x irq=%u\n",d.pvr[0xcc/4],d.pvr[0xd0/4],d.pvr[0xd8/4],d.pvr[0xdc/4],*(uint32_t*)(ram+0x32dbac),*(uint32_t*)(ram+0x32dbb0),d.irqs);
    fprintf(stderr,"pr=%08x sr=%08x vbr=%08x gdcommands=%u sectors=%llu lastcmd=%u status=%u pending=%x\n",v.pr,v.sr,v.vbr,d.gd.commands,(unsigned long long)d.gd.sectors_read,d.gd.command,d.gd.status,d.sb[0x100/4]);
    for(unsigned i=0;i<40 && (v.r[15]&0x1fffffff)+i*4<0x0d000000;i++){uint32_t word;memcpy(&word,ram+(v.r[15]&0xffffff)+i*4,4);fprintf(stderr,"stack %08x=%08x\n",v.r[15]+i*4,word);}
    for(unsigned i=0;i<1536;i++)if(d.sb_set[i])fprintf(stderr,"sb %08x=%08x\n",0x005f6800+i*4,d.sb[i]);
    for(unsigned i=0;i<4096;i++)if(d.profile[i]>1000000)fprintf(stderr,"profile %08x %u\n",0x8c000000+i*4096,d.profile[i]);
    const char *diagnostic=getenv("JGR_REPLAY_PRIVATE_ARAM");
    if(diagnostic){FILE*f=fopen(diagnostic,"wb");if(f){fwrite(aram,1,2u*1024*1024,f);fclose(f);}}
    free(hardware);free(aram);free(ram);jgr_disc_close(disc);
#ifdef __PSP__
    fclose(stdout);rename("replay-result.tmp","replay-result.json");
    pspDebugScreenPrintf("Samples: %u  Complete: %u\n",d.samples,d.complete);
    sceKernelExitGame();
#endif
    return ok?0:1;
}
