/* Run the optional converter BEFORE newlib reserves the gameplay heap.
 * This path uses raw PSP I/O and stack buffers only: no stdio or allocation.
 * The normal game CRT continues in this same executable after PRX unload. */
#undef sceIoOpen
#undef sceIoRemove
#undef sceIoRename
#undef sceIoGetstat
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <psppower.h>
#include <stdint.h>
#include <string.h>
#include "setup_module.h"
#include "portable_asset_identity.h"
#include "portable_crc32.h"

static int boot_status;
int startup_module_status(void){return boot_status;}
static unsigned word(const unsigned char *p)
{return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static char *append(char *p,const char *s)
{while(*s)*p++=*s++;*p=0;return p;}
static char *hex8(char *p,unsigned n)
{for(int bit=28;bit>=0;bit-=4)*p++="0123456789abcdef"[(n>>bit)&15];*p=0;return p;}
static char *decimal(char *p,unsigned n)
{char b[10];unsigned i=0;do{b[i++]=(char)('0'+n%10);n/=10;}while(n);while(i)*p++=b[--i];*p=0;return p;}
static int path(char *out,size_t capacity,const char *root,const char *leaf)
{
    size_t n=strlen(root),m=strlen(leaf);
    if(!n || n+1+m>=capacity)return -1;
    memcpy(out,root,n);out[n++]='/';memcpy(out+n,leaf,m+1);return 0;
}
static int read_all(int fd,void *data,unsigned bytes)
{
    unsigned char *p=data;
    while(bytes){int n=sceIoRead(fd,p,bytes);if(n<=0 || (unsigned)n>bytes)return -1;p+=n;bytes-=n;}
    return 0;
}
static int write_all(int fd,const void *data,unsigned bytes)
{
    const unsigned char *p=data;
    while(bytes){int n=sceIoWrite(fd,p,bytes);if(n<=0 || (unsigned)n>bytes)return -1;p+=n;bytes-=n;}
    return 0;
}
static int receipt_matches(const char *root)
{
    char file[JGR_SETUP_ROOT_BYTES+80],expected[96],actual[128];
    if(path(file,sizeof file,root,"data/cache/assets.checked"))return 0;
    int fd=sceIoOpen(file,PSP_O_RDONLY,0);if(fd<0)return 0;
    int n=sceIoRead(fd,actual,sizeof actual),closed=sceIoClose(fd);
    char *p=append(expected,"JGR_CHECKED 1\n");p=hex8(p,JGR_ASSET_MANIFEST_CRC32);
    *p++=' ';p=decimal(p,JGR_ASSET_LAYOUT_VERSION);*p++=' ';p=decimal(p,JGR_ASSET_FILE_COUNT);*p++=' ';
    unsigned prefix=(unsigned)(p-expected);
    if(closed<0 || n!=(int)prefix+9 || memcmp(actual,expected,prefix) || actual[n-1]!='\n')return 0;
    for(unsigned i=prefix;i<prefix+8;i++)if(!((actual[i]>='0' && actual[i]<='9') || (actual[i]>='a' && actual[i]<='f')))return 0;
    return 1;
}
static void trace(const char *root,const char *event,int result)
{
    char file[JGR_SETUP_ROOT_BYTES+80],text[160],device[5];
    if(path(file,sizeof file,root,"data/log/startup-module.log"))return;
    char *p=append(text,event);p=append(p," result=");p=hex8(p,(unsigned)result);
    p=append(p," free=");p=decimal(p,sceKernelTotalFreeMemSize());
    p=append(p," largest=");p=decimal(p,sceKernelMaxFreeMemSize());*p++='\n';
    int fd=sceIoOpen(file,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
    if(fd>=0){(void)write_all(fd,text,(unsigned)(p-text));sceIoClose(fd);}
    memcpy(device,root,4);device[4]=0;sceIoSync(device,0);
}
static int extract_converter(const char *root,char *output,size_t capacity)
{
    char executable[JGR_SETUP_ROOT_BYTES+80],temporary[JGR_SETUP_ROOT_BYTES+80];
    if(path(executable,sizeof executable,root,"EBOOT.PBP") ||
       path(output,capacity,root,"data/cache/setup.prx") ||
       path(temporary,sizeof temporary,root,"data/cache/setup.prx.tmp"))return -1;
    int input=sceIoOpen(executable,PSP_O_RDONLY,0),target=-1,status=-1;
    if(input<0)return input;
    unsigned char pbp[40],header[24],table[80],block[16384];
    SceOff size=sceIoLseek(input,0,PSP_SEEK_END);
    if(size<40 || size>0x7fffffff || sceIoLseek(input,0,PSP_SEEK_SET)<0 || read_all(input,pbp,40))goto done;
    if(memcmp(pbp,"\0PBP",4) || word(pbp+8)!=40)goto done;
    unsigned previous=40;
    for(unsigned i=0;i<8;i++){unsigned offset=word(pbp+8+4*i);if(offset<previous || offset>(unsigned)size)goto done;previous=offset;}
    unsigned psar=word(pbp+36),total=(unsigned)size-psar;
    if(total<104 || sceIoLseek(input,psar,PSP_SEEK_SET)<0 || read_all(input,header,24) || read_all(input,table,80))goto done;
    if(memcmp(header,"JGRBND2\0",8) || word(header+8)!=2 || word(header+12)!=5 || word(header+16)!=total ||
       (jgr_crc32_add(0xffffffffu,table,80)^0xffffffffu)!=word(header+20))goto done;
    unsigned cursor=104;
    static const unsigned limits[]={256*1024,256*1024,128*1024,4*1024*1024,8*1024*1024};
    for(unsigned i=0;i<5;i++){
        const unsigned char *entry=table+16*i;unsigned bytes=word(entry+8);
        if(word(entry)!=i+1 || word(entry+4)!=cursor || !bytes || bytes>limits[i] || cursor>total || bytes>total-cursor)goto done;
        cursor+=bytes;
    }
    if(cursor!=total)goto done;
    unsigned bytes=word(table+72),crc=0xffffffffu;
    if(bytes<52 || sceIoLseek(input,psar+word(table+68),PSP_SEEK_SET)<0)goto done;
    target=sceIoOpen(temporary,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(target<0)goto done;
    unsigned left=bytes;
    while(left){
        unsigned n=left>sizeof block?sizeof block:left;
        if(read_all(input,block,n))goto done;
        if(left==bytes && (memcmp(block,"\177ELF\1\1\1",7) || block[16]!=0xa0 || block[17]!=0xff ||
                          block[18]!=8 || block[19] || block[40]!=52 || block[41]))goto done;
        crc=jgr_crc32_add(crc,block,n);
        if(write_all(target,block,n))goto done;
        left-=n;scePowerTick(0);
    }
    if((crc^0xffffffffu)!=word(table+76))goto done;
    status=sceIoClose(target);target=-1;if(status<0)goto done;
    /* The filename belongs only to this loader; every load verifies fresh
     * bytes from EBOOT, never a cached or partially written module. */
    (void)sceIoRemove(output);status=sceIoRename(temporary,output);
done:
    if(target>=0)sceIoClose(target);
    if(sceIoClose(input)<0)status=-1;
    if(status<0)sceIoRemove(temporary);
    return status;
}
static int prepare(const char *root)
{
    char file[JGR_SETUP_ROOT_BYTES+80];
    static const char *const dirs[]={"data","data/cache","data/log"};
    for(unsigned i=0;i<3;i++){
        if(path(file,sizeof file,root,dirs[i]))return -1;
        SceIoStat info;
        if(sceIoGetstat(file,&info)<0){int result=sceIoMkdir(file,0777);if(result<0)return result;}
        else if(!FIO_S_ISDIR(info.st_mode))return -1;
    }
    trace(root,"converter.begin",0);
    int result=extract_converter(root,file,sizeof file);if(result<0){trace(root,"converter.extract",result);return result;}
    SceUID module=sceKernelLoadModule(file,0,NULL);
    trace(root,"converter.load",module);
    if(module<0){sceIoRemove(file);return module;}
    volatile int completed=JGR_SETUP_PENDING;
    JgrSetupModuleArgs args={JGR_SETUP_MODULE_ABI,sizeof args,{0},&completed};
    memcpy(args.root,root,strlen(root)+1);
    int status=0;
    /* module_start only creates the worker. Use the firmware's normal start
     * thread; the converter reserves its own VFPU stack before libc starts. */
    result=sceKernelStartModule(module,sizeof args,&args,&status,NULL);
    trace(root,"converter.start",result);
    trace(root,"converter.start_status",status);
    if(result>=0 && status>=0){
        while(completed==JGR_SETUP_PENDING){scePowerTick(0);sceKernelDelayThread(50000);}
        trace(root,"converter.return",completed);
        int stopped=0;result=sceKernelStopModule(module,0,NULL,&stopped,NULL);
        if(result<0 || stopped<0){trace(root,"converter.stop_failed",result<0?result:stopped);return result<0?result:stopped;}
    }
    int unloaded=sceKernelUnloadModule(module);
    trace(root,"converter.unload",unloaded);
    if(unloaded<0)return unloaded;
    sceIoRemove(file);
    if(result<0)return result;
    if(status<0)return status;
    return completed;
}
extern void __real___libcglue_init(int,char **);
void __wrap___libcglue_init(int argc,char **argv)
{
    char root[JGR_SETUP_ROOT_BYTES];
    boot_status=-1;
    if(argc>0 && argv && argv[0]){
        size_t n=strlen(argv[0]);
        if(n<sizeof root && (!strncmp(argv[0],"ms0:/",5) || !strncmp(argv[0],"ef0:/",5))){
            memcpy(root,argv[0],n+1);
            char *end=strrchr(root,'/');
            if(end && end>root+4 && !strcmp(end+1,"EBOOT.PBP")){
                *end=0;
                boot_status=receipt_matches(root)?0:prepare(root);
            }
        }
    }
    /* All converter workers, callbacks and its heap are gone now. The CRT
     * can reserve the ordinary full gameplay heap, with no fixed setup tax. */
    __real___libcglue_init(argc,argv);
}
