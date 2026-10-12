/* Standalone installation research executable. Inputs are user disc files;
 * output is an input-controller trace, NOT a route or complete asset pack. */
#include "portable_replay_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __PSP__
#include <pspkernel.h>
#include <pspdebug.h>
PSP_MODULE_INFO("JGR Replay Proof",0,1,0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-256);
#endif
static void *read_file(const char *path,unsigned *bytes,unsigned maximum)
{
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);
    if(n<=0 || (unsigned long)n>maximum || fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    void *p=malloc((size_t)n);if(!p){fclose(f);return NULL;}
    if(fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
    fclose(f);*bytes=(unsigned)n;return p;
}
static unsigned long long now(void)
{
#ifdef __PSP__
    return sceKernelGetSystemTimeWide();
#else
    return (unsigned long long)clock()*1000000u/CLOCKS_PER_SEC;
#endif
}
static int word(FILE *f,uint32_t w)
{unsigned char b[4]={(unsigned char)w,(unsigned char)(w>>8),(unsigned char)(w>>16),(unsigned char)(w>>24)};return fwrite(b,1,4,f)==4;}
int main(int argc,char **argv)
{
    const char *exe_path,*set_path,*out_path;unsigned subset=0;
#ifdef __PSP__
    (void)argc;(void)argv;pspDebugScreenInit();
    pspDebugScreenPrintf("JGR installer replay research\nOriginal controller execution only\n");
    exe_path="original/1ST_READ.BIN";set_path="original/mission_set.bin";out_path="controller.trace";
#else
    if(argc!=5){fprintf(stderr,"usage: replay-proof EXECUTABLE MISSION_SET SUBSET OUTPUT\n");return 2;}
    exe_path=argv[1];set_path=argv[2];subset=(unsigned)strtoul(argv[3],NULL,10);out_path=argv[4];
#endif
    unsigned exe_bytes=0,set_bytes=0;
    void *exe=read_file(exe_path,&exe_bytes,16u*1024u*1024u);
    void *set=read_file(set_path,&set_bytes,1024u*1024u);
    PortableReplayController *c=calloc(1,sizeof *c);FILE *out=NULL;
    unsigned frames=0,ended=0;int ok=0;unsigned long long start=now();
    if(!exe || !set || !c)goto done;
    if(!portable_replay_controller_begin(c,exe,exe_bytes,set,set_bytes,subset))goto done;
    out=fopen(out_path,"wb");if(!out)goto done;
    if(fwrite("PRC1",1,4,out)!=4 || !word(out,1))goto done;
    while(frames<=c->maximum_ticks) {
        PortableReplayInput s;
        if(!portable_replay_controller_step(c,&s))break;
        const uint32_t values[8]={s.tick,s.record,s.input,s.previous,s.repeat,s.ended,s.direction,s.magnitude};
        for(unsigned i=0;i<8;i++)if(!word(out,values[i]))goto done;
        frames++;if(s.ended){ended=1;break;}
    }
    ok=ended && !c->vm.fault;
done:
    if(out && fclose(out))ok=0;
    unsigned long long elapsed=now()-start;
    char report[512];snprintf(report,sizeof report,
        "{\"complete_controller\":%s,\"complete_route\":false,\"frames\":%u,\"instructions\":%llu,\"microseconds\":%llu,\"fault\":%u,\"pc\":%u,\"address\":%u,\"opcode\":%u}\n",
        ok?"true":"false",frames,c?(unsigned long long)c->vm.instructions:0,elapsed,
        c?(unsigned)c->vm.fault:0,c?(unsigned)c->vm.fault_pc:0,
        c?(unsigned)c->vm.fault_address:0,c?(unsigned)c->vm.fault_opcode:0);
#ifdef __PSP__
    FILE *log=fopen("replay_result.json","wb");if(log){fputs(report,log);fclose(log);}
    pspDebugScreenPrintf("%s",report);
#else
    fputs(report,stdout);
#endif
    free(c);free(set);free(exe);
#ifdef __PSP__
    sceKernelDelayThread(2000000);sceKernelExitGame();
#endif
    return ok?0:1;
}
