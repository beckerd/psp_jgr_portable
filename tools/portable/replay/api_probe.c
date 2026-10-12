/* Disc-only bounded installer API proof. Expected observations are not inputs. */
#include "portable_replay_bootstrap.h"
#include "portable_replay_vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(JGR_REPLAY_PROFILE) || defined(JGR_REPLAY_DRAW_PROBE)
#include "../../../psp/src/portable_replay_bootstrap.c"
#ifdef JGR_REPLAY_DRAW_PROBE
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
#ifdef JGR_REPLAY_PROFILE
static uint32_t profile_count[65536];
static int profile_service(PortableReplayVM*v,void*context)
{uint32_t p=v->pc&0x1fffffffu;if(p>=0x0c000000 && p<0x0d000000)profile_count[(p-0x0c000000)>>8]++;return service(v,context);}
#endif
#endif
#ifdef __PSP__
#include <pspkernel.h>
#include <pspdebug.h>
PSP_MODULE_INFO("JGR Disc Replay API Proof",0,1,0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(-512);
#endif
static int rounding_test(void)
{
    PortableReplayVM v;unsigned char code[]={0x10,0xf0,0x0b,0,9,0};
    portable_replay_vm_init(&v);v.fpscr=1;v.fr[0]=0x3f800000;v.fr[1]=0x33c00000;
    return portable_replay_vm_map(&v,0x8c000000,code,sizeof code,5) &&
        portable_replay_vm_run(&v,0x8c000000,0,10) && v.fr[0]==0x3f800000;
}
static int sample(void*ctx,const unsigned char*a,const unsigned char*c,const unsigned char*i,const unsigned char*p)
{FILE*f=ctx;return fwrite(a,1,0x900,f)==0x900 && fwrite(c,1,0x290,f)==0x290 && fwrite(i,1,0xd0,f)==0xd0 && fwrite(p,1,0x400,f)==0x400;}
static void tasks(PortableReplayBootstrap*b)
{
    uint32_t stack[2000];unsigned n=1,visits=0;
    if(!portable_replay_bootstrap_read(b,0x8c2e9c9c,stack,4))return;
    while(n && visits++<2000){uint32_t p=stack[--n],w[43];
        if(p<0x8c000000 || p>0x8cffff54 || !portable_replay_bootstrap_read(b,p,w,sizeof w))continue;
        if((w[1]&65535)==107 || (w[1]&65535)==0x6a || w[2]==0x8c0278c0)
            fprintf(stderr,"task %08x id=%08x exec=%08x state=%u player=%u camera=%08x\n",(unsigned)p,(unsigned)w[1],(unsigned)w[2],(unsigned)w[0x84/4],(unsigned)w[0x90/4],(unsigned)w[0xa8/4]);
        if((w[1]&65535)==0x6a){float position[3];if(portable_replay_bootstrap_read(b,p+0xb8,position,sizeof position))
            fprintf(stderr,"spectator xyz=%.9g,%.9g,%.9g mode=%u state=%u parent=%08x\n",position[0],position[1],position[2],(unsigned)w[0x70/4],(unsigned)w[0x78/4],(unsigned)w[0x18/4]);}
        if(n>1997)return;
        stack[n++]=w[0x24/4];stack[n++]=w[0x1c/4];
    }
}
int main(int argc,char**argv)
{
#ifdef __PSP__
    (void)argc;(void)argv;pspDebugScreenInit();pspDebugScreenPrintf("Disc-only replay API proof\n");
    char disc_path[512];FILE*settings=fopen("disc-path.txt","r");
    if(!settings || !fgets(disc_path,sizeof disc_path,settings))return 2;
    fclose(settings);disc_path[strcspn(disc_path,"\r\n")]=0;
    char*arguments[]={"api-proof",disc_path,"fsca-hardware.bin","disc-route.bin"};argc=4;argv=arguments;
    if(!freopen("bootstrap.log","w",stderr) || !freopen("replay-result.tmp","w",stdout))return 2;
#endif
    setvbuf(stderr,NULL,_IONBF,0);
    if(!rounding_test()){fprintf(stderr,"Original truncating FPU mode test failed\n");return 2;}
    fprintf(stderr,"Authored FPU rounding test passed; opening original disc\n");
    if(argc<4 || argc>6){fprintf(stderr,"usage: api-proof ORIGINAL_GDI CPU_HARDWARE_TABLE OUTPUT [stage,chapter,record,set,player] [repeat]\n");return 2;}
    JgrDisc*disc=NULL;char error[192];uint32_t*hardware=malloc(131072);FILE*f=fopen(argv[2],"rb");
    if(!hardware || !f || fread(hardware,1,131072,f)!=131072 || fgetc(f)!=EOF)return 2;
    fclose(f);if(jgr_disc_open(&disc,argv[1],error,sizeof error)<0){fprintf(stderr,"%s\n",error);return 2;}
    PortableReplayBootstrap*b=portable_replay_bootstrap_create(disc,hardware,32768);if(!b)return 2;
#ifdef JGR_REPLAY_PROFILE
    b->vm.hook=profile_service;
#endif
#ifdef JGR_REPLAY_DRAW_PROBE
    b->vm.hook=draw_service;
#endif
    fprintf(stderr,"Disc verified; original work memory allocated; IP handoff complete\n");
    PortableReplaySelection s={1,10,0,0,0,0};
    if(argc>=5 && sscanf(argv[4],"%u,%u,%u,%u,%u,%u",&s.stage,&s.chapter,&s.record,&s.subset,&s.character,&s.kind)<5)return 2;
    if(s.kind==PORTABLE_REPLAY_GANG){
        PortableReplayGangFixture fixture;const char*path=getenv("JGR_REPLAY_GANG_FIXTURE");
        if(!path || !(f=fopen(path,"r")))return 2;
        for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)if(fscanf(f,"%f",&fixture.approach[i][j])!=1)return 2;
        for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)if(fscanf(f,"%u",&fixture.idle_phase[i][j])!=1)return 2;
        fclose(f);if(!portable_replay_bootstrap_gang_fixture(b,&fixture))return 2;
    }
    unsigned jobs=argc==6?2:1;
    for(unsigned j=0;j<jobs;j++){
        char path[1024];if(snprintf(path,sizeof path,"%s%s",argv[3],j?".repeat":"")>=(int)sizeof path)return 2;
        f=fopen(path,"wb");if(!f)return 2;
        static const unsigned char header[16]={'T','A','C','2',0,9,0,0,0x90,2,0,0,0xd0,0,0,0};
        if(fwrite(header,1,16,f)!=16 || !portable_replay_bootstrap_select(b,&s,sample,f))return 2;
        int result=0;unsigned slices=0;PortableReplayStatus status;
        unsigned maximum=20000;const char*limit=getenv("JGR_REPLAY_LIMIT_SLICES");if(limit)maximum=(unsigned)strtoul(limit,NULL,0);
        while(result==0 && slices++<maximum){
            result=portable_replay_bootstrap_step(b,1000000);
            if(slices%50==0){portable_replay_bootstrap_status(b,&status);
                fprintf(stderr,"progress job=%u frames=%u samples=%u main=%08x cleanup=%u requested=%u selected=%u\n",j,status.frames,status.samples,(unsigned)status.main_callback,status.cleanup,status.requested,status.selected);
                fflush(f);
                if(s.kind>=PORTABLE_REPLAY_RIVAL && slices%100==0)tasks(b);
            }
        }
        portable_replay_bootstrap_status(b,&status);fclose(f);
        if(strlen(path)+6>=sizeof path)return 2;strcat(path,".cues");f=fopen(path,"wb");if(!f)return 2;
        unsigned count;const PortableReplayCue*cues=portable_replay_bootstrap_cues(b,&count);
        if(fwrite(cues,sizeof *cues,count,f)!=count)return 2;fclose(f);
        printf("{\"job\":%u,\"result\":%d,\"complete_route\":%s,\"samples\":%u,\"cues\":%u,\"instructions\":%llu,\"fault\":%u,\"pc\":%u,\"address\":%u,\"opcode\":%u}\n",
            j,result,result==1?"true":"false",status.samples,count,(unsigned long long)status.instructions,(unsigned)status.fault,(unsigned)status.pc,(unsigned)status.address,(unsigned)status.opcode);fflush(stdout);
        if(result!=1){
            fprintf(stderr,"fault pr=%08x sp=%08x args=%08x,%08x,%08x\n",(unsigned)status.pr,(unsigned)status.sp,(unsigned)status.argument[0],(unsigned)status.argument[1],(unsigned)status.argument[2]);
            for(unsigned i=0;i<24;i++){uint32_t word;if(portable_replay_bootstrap_read(b,status.sp+i*4,&word,4))fprintf(stderr,"stack %08x=%08x\n",(unsigned)status.sp+i*4,(unsigned)word);}
            return 1;
        }
    }
    portable_replay_bootstrap_destroy(b);jgr_disc_close(disc);free(hardware);
#ifdef JGR_REPLAY_PROFILE
    for(unsigned i=0;i<65536;i++)if(profile_count[i]>1000000)fprintf(stderr,"profile %08x %u\n",0x8c000000+i*256,profile_count[i]);
#endif
#ifdef __PSP__
    fclose(stdout);rename("replay-result.tmp","replay-result.json");sceKernelExitGame();
#endif
    return 0;
}
