/* Private development oracle replay. This is not the disc-only installer:
 * before/after RAM images must never enter exported code, recipes or assets. */
#include "portable_replay_vm.h"
#include "portable_replay_cues.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __PSP__
#include <pspkernel.h>
#include <pspdebug.h>
PSP_MODULE_INFO("JGR Callback Proof",0,1,0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(64);
PSP_HEAP_SIZE_KB(-256);
#endif
typedef struct {unsigned char *words;unsigned bytes,offset;} Timer;
static void *readall(const char *p,unsigned *length)
{
    FILE *f=fopen(p,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    if(n<0 || n>32*1024*1024){fclose(f);return NULL;}
    void *b=malloc(n?n:1);
    if(!b || fread(b,1,n,f)!=(size_t)n){fclose(f);free(b);return NULL;}
    fclose(f);*length=(unsigned)n;return b;
}
static int timer_read(PortableReplayVM *v,uint32_t a,unsigned n,uint32_t *result,void *context)
{
    (void)v;if(a!=0xffd8000cu)return 0;Timer *t=context;
    if(n!=4 || t->offset>t->bytes || t->bytes-t->offset<4)return -1;
    const unsigned char *p=t->words+t->offset;t->offset+=4;
    *result=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;return 1;
}
static int read_slice(const char *path,unsigned offset,void *p,unsigned size)
{
    FILE *f=fopen(path,"rb");if(!f)return 0;
    int ok=fseek(f,(long)offset,SEEK_SET)==0 && fread(p,1,size,f)==size;
    fclose(f);return ok;
}
static unsigned long long now(void)
{
#ifdef __PSP__
    return sceKernelGetSystemTimeWide();
#else
    return (unsigned long long)clock()*1000000u/CLOCKS_PER_SEC;
#endif
}
int main(int argc,char **argv)
{
#ifdef __PSP__
    (void)argc;(void)argv;pspDebugScreenInit();pspDebugScreenPrintf("Original callback interpreter parity\nPrivate development fixture only\n");
    char values[6][32];unsigned long input[6];FILE *config=fopen("context.txt","r");
    if(!config)return 2;
    int found=fscanf(config,"%lu %lu %lu %lu %lu %lu",input,input+1,input+2,input+3,input+4,input+5);fclose(config);
    if(found!=6)return 2;
    for(unsigned i=0;i<6;i++)snprintf(values[i],sizeof values[i],"%lu",input[i]);
    char *arguments[]={"callback-proof","before.ram","after.ram","timer_reads.bin",values[0],values[1],values[2],values[3],values[4],values[5],"fsca-hardware.bin","actor.trace"};
    argc=12;argv=arguments;
#endif
    if(argc!=12){fprintf(stderr,"usage: callback-proof BEFORE AFTER TIMERS ACTOR STACK FPSCR SR FPUL FPU_IMAGE FSCA_HARDWARE_TABLE OUTPUT\n");return 2;}
    unsigned before_n=0;Timer timers={0};
    unsigned char *before=readall(argv[1],&before_n);
    timers.words=readall(argv[3],&timers.bytes);
    unsigned table_bytes=0;uint32_t *table=readall(argv[10],&table_bytes);
    uint32_t actor=(uint32_t)strtoul(argv[4],0,0),stack=(uint32_t)strtoul(argv[5],0,0),fpscr=(uint32_t)strtoul(argv[6],0,0);
    if(!before || !timers.words || before_n!=16*1024*1024 ||
       actor<0x8c000000u || actor>0x8d000000u-0x900)return 2;
    unsigned char expected[0x900],expected_cnk[0x400];
    if(!read_slice(argv[2],actor-0x8c000000u,expected,sizeof expected))return 2;
    PortableReplayVM vm;portable_replay_vm_init(&vm);
    portable_replay_vm_map(&vm,0x8c000000u,before,before_n,PRVM_READ|PRVM_WRITE|PRVM_EXEC);
    vm.read_hook=timer_read;vm.read_context=&timers;vm.r[4]=actor;vm.r[15]=stack;vm.fpscr=fpscr;
    vm.sr=(uint32_t)strtoul(argv[7],0,0);vm.fpul=(uint32_t)strtoul(argv[8],0,0);
    uint32_t fpu_image=(uint32_t)strtoul(argv[9],0,0);
    if(fpu_image){
        if(fpu_image<0x8c000000u || fpu_image>0x8d000000u-128)return 2;
        memcpy(vm.fr,before+fpu_image-0x8c000000u,64);memcpy(vm.xf,before+fpu_image-0x8c000000u+64,64);
    }
    if(table_bytes==131072){vm.fsca_positive=table;vm.fsca_words=32768;}
    PortableReplayCue events[64];PortableReplayCues cues={events,0,64};
    vm.hook=portable_replay_cues_observe;vm.context=&cues;
    unsigned long long started=now();
    int completed=portable_replay_vm_run(&vm,0x8c0198f8u,0,2000000);
    unsigned long long microseconds=now()-started;
    unsigned differences=0;for(unsigned i=0;i<0x900;i++)differences+=before[actor-0x8c000000u+i]!=expected[i];
    uint32_t cnk=0;unsigned cnk_differences=0;
    if(!portable_replay_vm_read(&vm,actor+0x594,4,&cnk) || cnk<0x8c000000u || cnk>0x8d000000u-sizeof expected_cnk ||
       !read_slice(argv[2],cnk-0x8c000000u,expected_cnk,sizeof expected_cnk))cnk_differences=~0u;
    else for(unsigned i=0;i<sizeof expected_cnk;i++)cnk_differences+=before[cnk-0x8c000000u+i]!=expected_cnk[i];
    FILE *out=fopen(argv[11],"wb");if(!out)return 2;
    fwrite(before+actor-0x8c000000u,1,0x900,out);fclose(out);
    char cue_path[512];if(strlen(argv[11])+6>=sizeof cue_path)return 2;
    snprintf(cue_path,sizeof cue_path,"%s.cues",argv[11]);out=fopen(cue_path,"wb");if(!out)return 2;
    for(unsigned i=0;i<cues.count;i++){
        uint32_t values[6]={events[i].tick,events[i].kind,events[i].bank,events[i].program,events[i].parent,events[i].task};
        for(unsigned j=0;j<6;j++){
            unsigned char b[4]={(unsigned char)values[j],(unsigned char)(values[j]>>8),(unsigned char)(values[j]>>16),(unsigned char)(values[j]>>24)};
            if(fwrite(b,1,4,out)!=4){fclose(out);return 2;}
        }
    }
    fclose(out);
    char report[640];snprintf(report,sizeof report,"{\"complete_callback\":%s,\"actor_differing_bytes\":%u,\"cnk_differing_bytes\":%u,\"instructions\":%llu,\"microseconds\":%llu,\"timers_consumed\":%u,\"timer_count\":%u,\"cue_count\":%u,\"fault\":%u,\"pc\":%u,\"address\":%u,\"opcode\":%u,\"fpscr\":%u,\"private_fixture_only\":true}\n",
        completed?"true":"false",differences,cnk_differences,(unsigned long long)vm.instructions,microseconds,timers.offset/4,timers.bytes/4,cues.count,
        (unsigned)vm.fault,(unsigned)vm.fault_pc,(unsigned)vm.fault_address,(unsigned)vm.fault_opcode,(unsigned)vm.fpscr);
    free(before);free(timers.words);free(table);
#ifdef __PSP__
    FILE *log=fopen("replay_result.json","wb");if(log){fputs(report,log);fclose(log);}
    pspDebugScreenPrintf("%s",report);sceKernelDelayThread(2000000);sceKernelExitGame();
#else
    fputs(report,stdout);
#endif
    return completed&&differences==0&&cnk_differences==0?0:1;
}
