#!/usr/bin/env python3
"""Build an isolated PSP-1000 setup-to-game memory transition proof.

Synthetic allocations only; this does not prepare assets or establish firmware
compatibility. Both executables remain in the private output directory.
"""
import argparse
import os
from pathlib import Path
import subprocess

COMMON = r'''
#include <pspkernel.h>
#include <psploadexec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(64);
PSP_HEAP_SIZE_KB(-256);
static int touch(unsigned char *p,unsigned n) {
 if(!p)return 0;
 for(unsigned i=0;i<n;i+=4096)p[i]=(unsigned char)(i/4096);
 for(unsigned i=0;i<n;i+=4096)if(p[i]!=(unsigned char)(i/4096))return 0;
 return 1;
}
'''
LAUNCHER = COMMON + r'''
PSP_MODULE_INFO("SetupMemoryProof",0,1,0);
static volatile unsigned char importer[1024*1024];
int main(void) {
 char root[512],target[640],args[1200];
 if(!getcwd(root,sizeof root))return 1;
 if(root[strlen(root)-1]==':')strcat(root,"/");
 for(unsigned i=0;i<sizeof importer;i+=4096)importer[i]=1;
 unsigned char *ram=malloc(18u*1024*1024);
 int good=touch(ram,18u*1024*1024);
 FILE *f=fopen("launcher-memory.json","wb");
 if(f){fprintf(f,"{\"allocation_bytes\":18874368,\"allocation_ok\":%s,\"root\":\"%s\"}\n",good?"true":"false",root);fclose(f);}
 free(ram);if(!good){sceKernelExitGame();return 1;}
 snprintf(target,sizeof target,"%s%sreceiver/game.prx",root,root[strlen(root)-1]=='/'?"":"/");
 size_t n=strlen(target)+1,m=strlen(root)+1;
 memcpy(args,target,n);memcpy(args+n,root,m);
 struct SceKernelLoadExecParam param={sizeof param,n+m,args,NULL};
 int status=sceKernelLoadExec(target,&param);
 f=fopen("loadexec-error.json","wb");
 if(f){fprintf(f,"{\"status\":%d,\"target\":\"%s\"}\n",status,target);fclose(f);}
 sceKernelExitGame();return 1;
}
'''
RECEIVER = COMMON + r'''
PSP_MODULE_INFO("GameMemoryProof",0,1,0);
static volatile unsigned char gameplay[6*1024*1024];
int main(int argc,char **argv) {
 char before[512],after[512];getcwd(before,sizeof before);
 if(argc!=2||chdir(argv[1])){sceKernelExitGame();return 1;}
 getcwd(after,sizeof after);
 for(unsigned i=0;i<sizeof gameplay;i+=4096)gameplay[i]=(unsigned char)(i/4096);
 unsigned char *ram=malloc(16u*1024*1024);
 int good=touch(ram,16u*1024*1024);
 FILE *f=fopen("receiver-memory.json","wb");
 if(f){fprintf(f,"{\"allocation_bytes\":16777216,\"allocation_ok\":%s,\"static_bytes\":%u,\"argc\":%d,\"cwd_before\":\"%s\",\"root\":\"%s\"}\n",good?"true":"false",(unsigned)sizeof gameplay,argc,before,after);fclose(f);}
 free(ram);sceKernelExitGame();return good?0:1;
}
'''

def build(out, sdk):
    env={**os.environ,'PSPDEV':str(sdk),'PSPSDK':str(sdk/'psp/sdk'),
         'PATH':str(sdk/'bin')+os.pathsep+os.environ['PATH']}
    for name,source in [('launcher',LAUNCHER),('receiver',RECEIVER)]:
        folder=out/name;folder.mkdir(parents=True,exist_ok=True)
        (folder/'main.c').write_text(source)
        (folder/'Makefile').write_text('''TARGET = memory-proof
.DEFAULT_GOAL := all
OBJS = main.o
CFLAGS = -O2 -G0 -std=gnu99 -Wall -Wextra -Werror
BUILD_PRX = 1
PSP_FW_VERSION = 660
PSP_LARGE_MEMORY = 0
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = JGR Memory Transition Proof
PARAM.SFO EBOOT.PBP: Makefile
include $(PSPSDK)/lib/build.mak
''')
        subprocess.run(['make','-j2'],cwd=folder,env=env,check=True)
    (out/'EBOOT.PBP').write_bytes((out/'launcher/EBOOT.PBP').read_bytes())
    (out/'receiver/game.prx').write_bytes((out/'receiver/memory-proof.prx').read_bytes())

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--pspdev',type=Path,default=Path.home()/'pspdev')
    args=parser.parse_args();build(args.out.resolve(),args.pspdev.resolve())
