"""Exercise pre-libc selection, verified extraction and synchronous module lifetime."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib
from test_setup_launcher import HEADERS, ROOT
DRIVER=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "startup_module.c"
static int scenario,loads,starts,stops,unloads,libc_started,delays;
static volatile int *pending;
static const char *host(const char *p){assert(!strncmp(p,"ms0:/",5));return p+5;}
int sceIoOpen(const char *p,int f,int m){return open(host(p),f,m);}
int sceIoRead(int f,void *p,unsigned n){return (int)read(f,p,n);}
int sceIoWrite(int f,const void *p,unsigned n){if(scenario==9&&n>=52)return -1;return (int)write(f,p,n>37?37:n);}
int sceIoClose(int f){return close(f);}
SceOff sceIoLseek(int f,SceOff o,int w){return lseek(f,o,w);}
int sceIoGetstat(const char *p,SceIoStat *s){return stat(host(p),s);}
int sceIoMkdir(const char *p,int m){return mkdir(host(p),m);}
int sceIoRemove(const char *p){return unlink(host(p));}
int sceIoRename(const char *a,const char *b){return rename(host(a),host(b));}
int sceIoSync(const char *p,int n){(void)n;assert(!strcmp(p,"ms0:"));return 0;}
unsigned sceKernelTotalFreeMemSize(void){return 20000000;}
unsigned sceKernelMaxFreeMemSize(void){return 19000000;}
int scePowerTick(int n){(void)n;return 0;}
int sceKernelLoadModule(const char *p,int flags,void *option){
 assert(!libc_started&&!flags&&!option);loads++;
 int f=sceIoOpen(p,PSP_O_RDONLY,0);unsigned char h[52];assert(f>=0&&!read_all(f,h,sizeof h)&&!memcmp(h,"\177ELF",4));sceIoClose(f);
 return scenario==2?-42:12;
}
int sceKernelStartModule(int id,unsigned bytes,void *p,int *status,SceKernelSMOption *o){
 assert(!libc_started&&id==12&&bytes==sizeof(JgrSetupModuleArgs)&&!o);
 JgrSetupModuleArgs *a=p;assert(a->abi==JGR_SETUP_MODULE_ABI&&!strcmp(a->root,"ms0:/PSP/GAME/JGR"));
 assert(*a->result==JGR_SETUP_PENDING);starts++;
 if(scenario==3)return -43;
 if(scenario==4){*status=-44;return 0;}
 pending=a->result;
 *status=0;return 12;
}
int sceKernelDelayThread(unsigned us){assert(us==50000&&pending&&!libc_started);if(++delays==3)*pending=scenario==5?-10:scenario==8?-18:0;return 0;}
int sceKernelStopModule(int id,unsigned bytes,void *p,int *status,void *o){assert(!libc_started&&id==12&&!bytes&&!p&&!o);assert(pending&&*pending!=JGR_SETUP_PENDING&&delays==3);stops++;*status=0;return scenario==6?-46:0;}
int sceKernelUnloadModule(int id){assert(!libc_started&&id==12);unloads++;return scenario==7?-47:0;}
void __real___libcglue_init(int argc,char **argv){assert(argc==1&&argv[0]);libc_started++;}
int main(int argc,char **argv){
 assert(argc==3);scenario=atoi(argv[1]);int expected=atoi(argv[2]);
 char *args[]={"ms0:/PSP/GAME/JGR/EBOOT.PBP",NULL};
 if(scenario==12)args[0]="ms0:/PSP/GAME/JGR/not-eboot";
 __wrap___libcglue_init(1,args);
 assert(libc_started==1&&startup_module_status()==expected);
 int wanted=!(scenario==1||scenario==9||scenario==10||scenario==11||scenario==12);
 assert(loads==wanted&&starts==(wanted&&scenario!=2));
 assert(stops==(wanted&&scenario!=2&&scenario!=3&&scenario!=4));
 assert(unloads==(wanted&&scenario!=2&&scenario!=6));
 return 0;
}
'''
def pbp():
    module=bytearray(96);module[:7]=b'\x7fELF\1\1\1';struct.pack_into('<HH',module,16,0xffa0,8);struct.pack_into('<H',module,40,52)
    payloads=[bytes(module),bytes(module),b'authored notice',b'authored recipe',bytes(module)]
    table=b'';cursor=104
    for i,p in enumerate(payloads):
        table+=struct.pack('<4I',i+1,cursor,len(p),zlib.crc32(p));cursor+=len(p)
    packed=struct.pack('<8s4I',b'JGRBND2\0',2,5,cursor,zlib.crc32(table))+table+b''.join(payloads)
    return b'\0PBP'+b'\0'*4+struct.pack('<8I',40,44,44,44,44,44,44,44+len(module))+b'SFO!'+module+packed
class StartupModule(unittest.TestCase):
    def test_prepared_fast_path_and_module_failure_paths(self):
        with tempfile.TemporaryDirectory(prefix='jgr-module-host-') as tmp:
            root=Path(tmp)
            for name,text in HEADERS.items():(root/name).write_text(text)
            (root/'driver.c').write_text(DRIVER)
            exe=root/'driver'
            subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',
                            '-I'+str(root),'-I'+str(ROOT/'psp/src'),str(root/'driver.c'),'-o',str(exe)],check=True)
            for scenario,expected in enumerate([0,0,-42,-43,-44,-10,-46,-47,-18,-1,-1,-1,-1]):
                with self.subTest(scenario=scenario):
                    case=root/str(scenario);game=case/'PSP/GAME/JGR';game.mkdir(parents=True)
                    data=bytearray(pbp())
                    if scenario==10:data[-1]^=1
                    if scenario==11:data[44+96+20]^=1
                    (game/'EBOOT.PBP').write_bytes(data)
                    if scenario==1:
                        cache=game/'data/cache';cache.mkdir(parents=True)
                        (cache/'assets.checked').write_text('JGR_CHECKED 1\nb1827734 2 1894 1234abcd\n')
                    subprocess.run([str(exe),str(scenario),str(expected)],cwd=case,check=True)
                    if scenario in (9,10,11):self.assertFalse((game/'data/cache/setup.prx').exists())
if __name__=='__main__':unittest.main()
