/* Private disc-only STAFF parity harness. Oracle captures are never inputs. */
#include "portable_replay_bootstrap.h"
#include "portable_replay_staff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef JGR_REPLAY_DRAW_PROBE
#include "../../../psp/src/portable_replay_bootstrap.c"
static int draw_service(PortableReplayVM*v,void*context)
{
    #ifdef JGR_REPLAY_DRAW_WHOLE
    if((v->pc&0x1fffffffu)==0x0c0b2f18)
#else
    if((v->pc&0x1fffffffu)==0x0c0b305c)
#endif
    {v->pc=v->pr;return 1;}
    return service(v,context);
}
#endif
typedef struct {
    PortableReplayBootstrap *boot;
    JgrReplayStaff *native;
    FILE *raw,*csv,*gf,*wall;
    unsigned character,count,observed,parts,prior_actor,prior_tick,prior_row;
    unsigned first[9],last[9],counts[9],descriptors[6][512];
    unsigned previous[6][512][7],valid[6][512];
} Capture;
static unsigned word(const unsigned char *p,unsigned at)
{return (unsigned)p[at]|(unsigned)p[at+1]<<8|(unsigned)p[at+2]<<16|(unsigned)p[at+3]<<24;}
static unsigned readword(Capture*c,unsigned at)
{unsigned char b[4];return portable_replay_bootstrap_read(c->boot,at,b,4)?word(b,0):0;}
static unsigned find(Capture*c,unsigned id)
{
    unsigned stack[2000],n=1,visits=0;stack[0]=readword(c,0x8c2e9c9c);
    while(n && visits++<2000){unsigned p=stack[--n];if(p<0x8c000000 || p>0x8cffffd8 || (p&3))continue;
        if((readword(c,p+4)&65535)==id)return p;
        if(n>1997)return 0;
        stack[n++]=readword(c,p+0x24);stack[n++]=readword(c,p+0x1c);
    }return 0;
}
static int sample(void*context,const unsigned char*a,const unsigned char*cam,const unsigned char*i,const unsigned char*p)
{
    Capture*c=context;unsigned staff=find(c,0x1b),row=readword(c,staff+0x6c),tick=word(a,0x98);
    unsigned native_part,native_cut,native_variant;
    if(!jgr_replay_staff_sample(c->native,a,cam,i,&native_part,&native_cut,&native_variant))return 0;
    unsigned actor=word(cam,0x18),part=c->parts,variant=word(a,0x560)==word(a,0x580);
    if(!part || actor!=c->prior_actor || tick<c->prior_tick || row!=c->prior_row){
        if(part==9)return 0;
        c->parts=++part;c->first[part-1]=tick;
        memset(c->valid,0,sizeof c->valid);memset(c->descriptors,0,sizeof c->descriptors);
        for(unsigned area=0;area<6;area++){
            unsigned table=readword(c,0x8cddf04c+area*4);
            for(unsigned at=0;at<512;at++){unsigned desc=readword(c,table+at*4);if(!desc)break;c->descriptors[area][at]=desc;}
        }
        fprintf(stderr,"STAFF part=%u row=%08x tick=%u sample=%u\n",part,row,tick,c->count+1);
    }else if(tick!=c->prior_tick+1)return 0;
    c->count++;c->counts[part-1]++;c->last[part-1]=tick;
    c->prior_actor=actor;c->prior_tick=tick;c->prior_row=row;
    if(fwrite(a,1,0x900,c->raw)!=0x900 || fwrite(cam,1,0x290,c->raw)!=0x290 ||
       fwrite(i,1,0xd0,c->raw)!=0xd0 || fwrite(p,1,0x400,c->raw)!=0x400)return 0;
    for(unsigned which=0;which<3;which++){
        unsigned char data[0x158]={0};unsigned bytes=which==2?0xd0:0x158;
        unsigned pointer=which==2?staff:word(a,which?0x274:0x268);
        (void)portable_replay_bootstrap_read(c->boot,pointer,data,bytes);
        if(fwrite(data,1,bytes,c->gf)!=bytes)return 0;
    }
    unsigned values[]={c->count,c->character,tick,(row-0x8cfe0000u)/20,
        word(a,0x7c),variant,word(i,0x70),word(i,0x78),word(i,0x6c),part-1,c->character,tick,word(cam,0x78)};
    for(unsigned j=0;j<sizeof values/sizeof*values;j++)if(fprintf(c->csv,"%s%u",j?",":"",values[j])<0)return 0;
    return fputc('\n',c->csv)!=EOF;
}
static int frame(void*context)
{
    Capture*c=context;if(!jgr_replay_staff_frame(c->native))return 0;
    if(c->count==c->observed)return 1;c->observed=c->count;
    unsigned stack[2000],gf[2000],reveal[2000],kind[2000],n=1,visits=0,ng=0,nr=0;
    stack[0]=readword(c,0x8c2e9c9c);
    while(n && visits++<2000){unsigned p=stack[--n];if(p<0x8c000000 || p>0x8cffffd8 || (p&3))continue;
        unsigned exec=readword(c,p+8),work=readword(c,p+0x6c);
        if(exec==0x8c03e6f8)gf[ng++]=work;
        else if(exec==0x8c03b828 || exec==0x8c03b8a8){reveal[nr]=work;kind[nr++]=exec==0x8c03b828?1:2;}
        if(n>1997)return 0;
        stack[n++]=readword(c,p+0x24);stack[n++]=readword(c,p+0x1c);
    }
    if(n)return 0;
    for(unsigned g=0;g<ng;g++){
        unsigned desc=readword(c,gf[g]+0xb8);
        for(unsigned area=0;area<6;area++)for(unsigned index=0;index<512 && c->descriptors[area][index];index++){
            if(c->descriptors[area][index]!=desc)continue;
            unsigned state[7];for(unsigned j=0;j<4;j++)state[j]=readword(c,gf[g]+0x144+j*4);
            state[4]=0;for(unsigned j=0;j<nr;j++)if(reveal[j]==gf[g])state[4]=kind[j];
            state[5]=gf[g];state[6]=readword(c,gf[g]+0x154);
            if(!c->valid[area][index] || memcmp(state,c->previous[area][index],sizeof state)){
                if(fprintf(c->wall,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",c->count,c->count-1,area,index,
                    state[0],state[1],state[2],state[3],state[4],desc,state[5],state[6])<0)return 0;
                memcpy(c->previous[area][index],state,sizeof state);c->valid[area][index]=1;
            }
        }
    }return 1;
}
static FILE *output(const char*dir,const char*name)
{char path[2048];if(snprintf(path,sizeof path,"%s/%s",dir,name)>=(int)sizeof path)return NULL;return fopen(path,"wb");}
int main(int argc,char**argv)
{
    if(argc!=5){fprintf(stderr,"usage: staff-proof ORIGINAL_GDI CPU_HARDWARE_TABLE OUTPUT_DIRECTORY CHARACTER\n");return 2;}
    JgrDisc*disc=NULL;char error[192];uint32_t*hardware=malloc(131072);FILE*f=fopen(argv[2],"rb");
    if(!hardware || !f || fread(hardware,1,131072,f)!=131072 || fgetc(f)!=EOF)return 2;
    fclose(f);if(jgr_disc_open(&disc,argv[1],error,sizeof error)<0){fprintf(stderr,"%s\n",error);return 2;}
    Capture*c=calloc(1,sizeof* c);if(!c)return 2;c->character=(unsigned)strtoul(argv[4],NULL,0);
    c->boot=portable_replay_bootstrap_create(disc,hardware,32768);if(!c->boot)return 2;
    #ifdef JGR_REPLAY_DRAW_PROBE
    c->boot->vm.hook=draw_service;
#endif
    c->native=jgr_replay_staff_create(c->boot,c->character);if(!c->native)return 2;
    (void)mkdir(argv[3],0700);
    c->raw=output(argv[3],"title_actor_camera.bin");c->csv=output(argv[3],"title.csv");
    c->gf=output(argv[3],"staff_graffiti.bin");c->wall=output(argv[3],"staff_graffiti.csv");
    if(!c->raw || !c->csv || !c->gf || !c->wall)return 2;
    static const unsigned char tac[16]={'T','A','C','2',0,9,0,0,0x90,2,0,0,0xd0,0,0,0};
    static const unsigned char sgf[16]={'S','G','F','1',0x58,1,0,0,2,0,0,0,0xd0,0,0,0};
    if(fwrite(tac,1,16,c->raw)!=16 || fwrite(sgf,1,16,c->gf)!=16)return 2;
    fprintf(c->csv,"sample,character,player_clock,staff_row,actor_character,actor_variant,controller_record,controller_repeat,controller_input,part,selected_character,player_tick,replay_tick\n");
    fprintf(c->wall,"sample,frame,area,index,stroke,reveal,complete,started,revealing,descriptor,pointer,busy\n");
    PortableReplaySelection selection={1,0,0,0,c->character,PORTABLE_REPLAY_STAFF};
    if(!portable_replay_bootstrap_select(c->boot,&selection,sample,c))return 2;
    portable_replay_bootstrap_frame_observer(c->boot,frame,c);
    int result=0;unsigned slices=0;PortableReplayStatus status;
    while(!result && slices++<30000){result=portable_replay_bootstrap_step(c->boot,1000000);
        if(slices%500==0){portable_replay_bootstrap_status(c->boot,&status);fprintf(stderr,"progress frames=%u samples=%u main=%08x\n",status.frames,status.samples,(unsigned)status.main_callback);}}
    portable_replay_bootstrap_status(c->boot,&status);
    f=output(argv[3],"staff_native_meta.bin");if(!f || !jgr_replay_staff_finish(c->native,f))result=-1;
    if(f)fclose(f);
    fclose(c->raw);fclose(c->csv);fclose(c->gf);fclose(c->wall);
    f=output(argv[3],"staff_route_metadata.json");if(!f)return 2;
    fprintf(f,"{\"character\":%u,\"samples\":%u,\"parts\":%u,\"complete\":%s,\"reason\":\"%s\",\"streams\":[",c->character,c->count,c->parts,result==1?"true":"false",result==1?"staff_deleted":"execution_failed");
    for(unsigned j=0;j<c->parts;j++)fprintf(f,"%s{\"part\":%u,\"first_tick\":%u,\"last_tick\":%u,\"samples\":%u}",j?",":"",j,c->first[j],c->last[j],c->counts[j]);
    fprintf(f,"]}\n");fclose(f);
    printf("{\"result\":%d,\"samples\":%u,\"parts\":%u,\"instructions\":%llu,\"fault\":%u,\"pc\":%u,\"address\":%u,\"opcode\":%u}\n",result,c->count,c->parts,(unsigned long long)status.instructions,(unsigned)status.fault,(unsigned)status.pc,(unsigned)status.address,(unsigned)status.opcode);
    jgr_replay_staff_destroy(c->native);portable_replay_bootstrap_destroy(c->boot);jgr_disc_close(disc);free(hardware);free(c);return result==1?0:1;
}
