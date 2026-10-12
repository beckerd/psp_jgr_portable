#!/usr/bin/env python3
"""Immutable native pose storage: ownership, failure fallback, and exact reuse.

Set JGR_NATIVE_BANK_ASSETS to locally generated assets for real-bank coverage.
Only authored synthetic fixture bytes are included in this test.
"""
from pathlib import Path
import os
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROBE = r'''
#include "portable_native_world.h"
#include "portable_native_pose.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Count only production cache/bank allocations, not test or stdio buffers. */
static struct {void *p;size_t n;} allocations[64];
static size_t live,peak,total;
static unsigned requests,fail_request,forgets;
void *bank_test_malloc(size_t n)
{
 if(++requests==fail_request)return NULL;
 void *p=malloc(n);if(!p)return NULL;
 for(unsigned i=0;i<64;i++)if(!allocations[i].p){
  allocations[i].p=p;allocations[i].n=n;live+=n;total+=n;if(live>peak)peak=live;return p;
 }
 assert(0);return NULL;
}
void *bank_test_calloc(size_t n,size_t s)
{assert(!s||n<=SIZE_MAX/s);void *p=bank_test_malloc(n*s);if(p)memset(p,0,n*s);return p;}
void bank_test_free(void *p)
{
 if(!p)return;
 for(unsigned i=0;i<64;i++)if(allocations[i].p==p){live-=allocations[i].n;allocations[i].p=NULL;free(p);return;}
 assert(0);
}
void real_pose_forget(void);
void jgr_native_chars_pose_forget(void){forgets++;real_pose_forget();}
int jgr_native_error(const JgrNativeConfig *c,const char *s){(void)c;(void)s;return -1;}
int jgr_world_native_collision(JgrDisc *d,unsigned s,FILE *f){(void)d;(void)s;(void)f;assert(0);return -1;}
typedef struct {const char *directory;unsigned opens,fail;} Source;
static FILE *dependency(void *u,const char *name)
{
 Source *s=u;s->opens++;if(s->fail)return NULL;char path[4096];
 assert(snprintf(path,sizeof path,"%s/%s",s->directory,name)>0);return fopen(path,"rb");
}
static FILE *other_dependency(void *u,const char *name){return dependency(u,name);}
static JgrNativeConfig config(Source *s,JgrNativeCache *cache)
{JgrNativeConfig c={0};c.disc=(JgrDisc *)(uintptr_t)1;c.dependency=dependency;c.dependency_user=s;c.cache=cache;return c;}
static void reset(void){assert(!live);total=peak=requests=fail_request=forgets=0;}
static void clean_bank(const Chars *b){Chars empty={0};assert(!memcmp(b,&empty,sizeof empty));}
static void compare_section(FILE *f,unsigned offset,const unsigned char *data,unsigned bytes)
{
 unsigned char block[16384];assert(!fseek(f,offset,SEEK_SET));
 while(bytes){unsigned n=bytes>sizeof block?sizeof block:bytes;assert(fread(block,1,n,f)==n);assert(!memcmp(block,data,n));data+=n;bytes-=n;}
}
static void exact(Source *source,const Chars *bank,const char *name)
{
 char path[4096];snprintf(path,sizeof path,"%s/%s",source->directory,name);FILE *f=fopen(path,"rb");assert(f);
 unsigned metadata=bank->h->off_bones+bank->h->n_bones*sizeof(JgcBone),motions=bank->h->off_pix-bank->h->off_motions;
 assert(bank->resident_bytes==metadata+motions&&!bank->motion_owner);
 compare_section(f,0,bank->base,metadata);compare_section(f,bank->h->off_motions,bank->motion_data,motions);assert(!fclose(f));
}
static void alias(const JgrNativeConfig *c,Chars *bank,unsigned model,CharPose *sample,unsigned *spray_bytes)
{
 char name[32];snprintf(name,sizeof name,"spraymesh%02u.jgc",model);Chars spray={0};
 assert(!jgr_native_bank_load(c,&spray,name));*spray_bytes=spray.resident_bytes;
 assert(!spray.motion_owner&&!bank->motion_owner&&!chars_share_motions(&spray,bank,(int)model));
 assert(spray.motion_owner==bank&&spray.motions==bank->motions);
 jgr_native_chars_sample_pose(&spray,spray.chars,(int)spray.chars->motion0,.375f,0,sample);
 jgr_native_bank_free(c,&spray);clean_bank(&spray);
}
static void reuse(const char *directory,const char *scratch,unsigned models)
{
 size_t totals[2],peaks[2];unsigned opens[2],resident=0,cache_bytes=0;CharPose expected[52];
 for(unsigned enabled=0;enabled<2;enabled++){
  reset();Source source={directory,0,0};JgrNativeCache *cache=enabled?jgr_native_cache_create((JgrDisc *)(uintptr_t)1,scratch):NULL;
  if(enabled){assert(cache);cache_bytes=(unsigned)live;}JgrNativeConfig c=config(&source,cache);
  unsigned char *storage=NULL;unsigned max_spray=0;
  for(unsigned i=0;i<52;i++){
   Chars bank={0};unsigned before=requests;assert(!jgr_native_bank_load(&c,&bank,"chars.jgc"));
   exact(&source,&bank,"chars.jgc");resident=bank.resident_bytes;
   if(enabled&&i){assert(storage==bank.base);assert(requests==before);}storage=bank.base;
   unsigned spray_bytes=0;CharPose sample;alias(&c,&bank,i%models,&sample,&spray_bytes);
   if(spray_bytes>max_spray)max_spray=spray_bytes;
   if(!enabled)expected[i]=sample;else assert(!memcmp(&sample,expected+i,sizeof sample));
   jgr_native_bank_free(&c,&bank);clean_bank(&bank);jgr_native_bank_free(&c,&bank);
   assert(live==(enabled?(size_t)cache_bytes+resident:0));
  }
  totals[enabled]=total;peaks[enabled]=peak;opens[enabled]=source.opens;
  assert(peak<=(size_t)resident+max_spray+(enabled?cache_bytes:0));
  jgr_native_cache_destroy(cache);assert(!live);
 }
 assert(opens[0]==104&&opens[1]==53);
 assert(totals[0]-totals[1]+cache_bytes==(size_t)51*resident);
 printf("{\"loads\":52,\"pose_resident_bytes\":%u,\"cache_owner_bytes\":%u,\"baseline_opens\":%u,\"cached_opens\":%u,\"baseline_allocated_bytes\":%zu,\"cached_allocated_bytes\":%zu,\"baseline_peak_bytes\":%zu,\"cached_peak_bytes\":%zu,\"poses_exact\":true,\"sections_exact\":true}\n",resident,cache_bytes,opens[0],opens[1],totals[0],totals[1],peaks[0],peaks[1]);
}
static void lifecycle(const char *directory,const char *scratch)
{
 reset();Source source={directory,0,0};JgrNativeCache *cache=jgr_native_cache_create((JgrDisc *)(uintptr_t)1,scratch);assert(cache);
 JgrNativeConfig c=config(&source,cache);Chars a={0},b={0};
 assert(!jgr_native_bank_load(&c,&a,"chars.jgc"));assert(!jgr_native_bank_load(&c,&b,"chars.jgc"));assert(a.base==b.base&&source.opens==1);
 unsigned char *storage=a.base;size_t retained=live;jgr_native_cache_trim(cache,0);assert(live==retained);
 jgr_native_bank_free(&c,&a);assert(forgets);exact(&source,&b,"chars.jgc");
 assert(!jgr_native_bank_load(&c,&a,"chars.jgc")&&a.base==storage&&source.opens==1);
 /* Cancellation releases aliases before their descriptor; reuse is unchanged. */
 CharPose p;unsigned n;alias(&c,&a,0,&p,&n);jgr_native_bank_free(&c,&a);jgr_native_bank_free(&c,&b);
 jgr_native_cache_trim(cache,SIZE_MAX);assert(live==retained);
 unsigned previous=forgets;jgr_native_cache_trim(cache,0);assert(live<retained&&forgets>previous);
 assert(!jgr_native_bank_load(&c,&a,"chars.jgc")&&source.opens==3);exact(&source,&a,"chars.jgc");
 /* A close with live views permits their final release, but no further loads. */
 assert(!jgr_native_bank_load(&c,&b,"chars.jgc"));jgr_native_cache_destroy(cache);
 jgr_native_bank_free(&c,&a);exact(&source,&b,"chars.jgc");jgr_native_bank_free(&c,&b);assert(!live);
 jgr_native_bank_free(&c,&b); /* Empty repeated cleanup must not consult a dead owner. */
 cache=jgr_native_cache_create((JgrDisc *)(uintptr_t)1,scratch);assert(cache);c.cache=cache;
 unsigned before=source.opens;assert(!jgr_native_bank_load(&c,&a,"chars.jgc")&&source.opens==before+1);
 jgr_native_bank_free(&c,&a);jgr_native_cache_destroy(cache);assert(!live);
}
static void identity(const char *directory,const char *scratch)
{
 reset();Source source={directory,0,0},other={directory,0,0};
 JgrNativeCache *cache=jgr_native_cache_create((JgrDisc *)(uintptr_t)1,scratch);assert(cache);
 JgrNativeConfig c=config(&source,cache);Chars a={0},b={0};assert(!jgr_native_bank_load(&c,&a,"chars.jgc"));
 for(unsigned i=0;i<4;i++){
  JgrNativeConfig changed=c;
  if(i==0)changed.disc=(JgrDisc *)(uintptr_t)2;
  if(i==1)changed.dependency=other_dependency;
  if(i==2)changed.dependency_user=&other;
  const char *name=i==3?"other.jgc":"chars.jgc";
  assert(!jgr_native_bank_load(&changed,&b,name)&&b.base!=a.base);jgr_native_bank_free(&changed,&b);
 }
 assert(source.opens==4&&other.opens==1);assert(!jgr_native_bank_load(&c,&b,"chars.jgc")&&b.base==a.base);
 jgr_native_bank_free(&c,&a);jgr_native_bank_free(&c,&b);jgr_native_cache_destroy(cache);assert(!live);
}
static void failures(const char *directory,const char *scratch)
{
 reset();Source source={directory,0,0};fail_request=1;
 assert(!jgr_native_cache_create((JgrDisc *)(uintptr_t)1,scratch));fail_request=0;
 JgrNativeConfig c=config(&source,NULL);Chars bank={0};assert(!jgr_native_bank_load(&c,&bank,"chars.jgc"));
 jgr_native_bank_free(&c,&bank);assert(!live);
 JgrNativeCache *cache=jgr_native_cache_create((JgrDisc *)(uintptr_t)1,scratch);assert(cache);c.cache=cache;size_t owner=live;
 for(unsigned allocation=1;allocation<=2;allocation++){
  fail_request=requests+allocation;assert(jgr_native_bank_load(&c,&bank,"chars.jgc")==-1);clean_bank(&bank);assert(live==owner);fail_request=0;
 }
 source.fail=1;assert(jgr_native_bank_load(&c,&bank,"chars.jgc")==-1);clean_bank(&bank);assert(live==owner);source.fail=0;
 /* The callback points at a malformed bank, then returns to the valid source. */
 char bad_directory[4096];snprintf(bad_directory,sizeof bad_directory,"%s/bad",directory);const char *good=source.directory;source.directory=bad_directory;
 assert(jgr_native_bank_load(&c,&bank,"chars.jgc")==-1);clean_bank(&bank);assert(live==owner);source.directory=good;
 assert(!jgr_native_bank_load(&c,&bank,"chars.jgc"));exact(&source,&bank,"chars.jgc");unsigned opened=source.opens;
 jgr_native_bank_free(&c,&bank);assert(!jgr_native_bank_load(&c,&bank,"chars.jgc")&&source.opens==opened);
 jgr_native_bank_free(&c,&bank);jgr_native_cache_destroy(cache);assert(!live);
}
int main(int argc,char **argv)
{
 assert(argc==4||argc==5);
 if(!strcmp(argv[1],"reuse"))reuse(argv[2],argv[3],argc==5?16:1);
 else if(!strcmp(argv[1],"lifecycle"))lifecycle(argv[2],argv[3]);
 else if(!strcmp(argv[1],"identity"))identity(argv[2],argv[3]);
 else if(!strcmp(argv[1],"failures"))failures(argv[2],argv[3]);
 else assert(0);
 assert(!live);puts("native bank cache PASS");return 0;
}
'''


class NativeBankCache(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-native-bank-cache-')
        cls.directory = Path(cls.temp.name)
        (cls.directory/'probe.c').write_text(PROBE)
        flags = [os.environ.get('CC', 'cc'), '-std=c99', '-D_XOPEN_SOURCE=700', '-O2',
                 '-ffp-contract=off', '-ffunction-sections', '-fdata-sections', '-DJGR_DIAGNOSTICS=0',
                 '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-misleading-indentation',
                 '-I'+str(ROOT/'psp/src'), '-I'+str(ROOT/'psp/tests/render_stubs'), '-I'+str(ROOT/'psp/tests/stubs')]
        flags += shlex.split(os.environ.get('JGR_NATIVE_BANK_CFLAGS', '-fsanitize=address,undefined'))
        objects = []
        for name in ('portable_native_bank', 'portable_native_cache', 'portable_native_pose', 'charpose'):
            obj = cls.directory/(name+'.o')
            macros = ['-Dmalloc=bank_test_malloc', '-Dcalloc=bank_test_calloc', '-Dfree=bank_test_free'] if name in ('portable_native_bank', 'portable_native_cache') else []
            if name == 'portable_native_pose':
                macros += ['-Djgr_native_chars_pose_forget=real_pose_forget']
            subprocess.run(flags+macros+['-c', str(ROOT/'psp/src'/(name+'.c')), '-o', str(obj)], check=True)
            objects.append(str(obj))
        cls.exe = cls.directory/'probe'
        subprocess.run(flags+[str(cls.directory/'probe.c'), *objects,
                       '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections',
                       '-lm', '-o', str(cls.exe)], check=True)
        for name, motions in (('chars.jgc', True), ('other.jgc', True), ('spraymesh00.jgc', False)):
            (cls.directory/name).write_bytes(cls.fixture(motions))
        (cls.directory/'bad').mkdir()
        (cls.directory/'bad/chars.jgc').write_bytes(b'bad')

    @staticmethod
    def fixture(motions):
        n = int(motions)
        start = 65536
        keys = start+8*n+24*n
        pixels = keys+12*n
        data = bytearray(pixels+4096)
        struct.pack_into('<4s16I', data, 0, b'JGC2', 1, 1, 0, 0, 0, 0, n,
                         80, 128, 176, 176, 176, 176, start, keys, pixels)
        struct.pack_into('<4I', data, 80, 0, 1, 0, n)
        struct.pack_into('<hH3f3i3f2I', data, 128, -1, 0, 1, 2, 3, 0, 0, 0, 1, 1, 1, 0, 0)
        if motions:
            struct.pack_into('<2I', data, start, 1, start+8)
            struct.pack_into('<6I', data, start+8, 1, 0, 0, 0, 0, 0)
            struct.pack_into('<3f', data, keys, 12.5, 2, 3)
        return data

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_probe(self, mode, directory=None):
        arguments = [str(self.exe), mode, str(directory or self.directory), str(self.directory)]
        if directory:
            arguments.append('real')
        subprocess.run(arguments, check=True)

    def test_52_loads_share_one_exact_bank_and_private_spray_aliases(self):
        self.run_probe('reuse')

    def test_multiple_views_cancellation_trim_reload_and_delayed_close(self):
        self.run_probe('lifecycle')

    def test_disc_callback_user_and_name_identity(self):
        self.run_probe('identity')

    def test_cache_and_payload_oom_read_failure_and_invalid_layout_fallback(self):
        self.run_probe('failures')

    @unittest.skipUnless(os.environ.get('JGR_NATIVE_BANK_ASSETS'), 'set JGR_NATIVE_BANK_ASSETS for local real-bank coverage')
    def test_actual_bank_bytes_memory_and_all_16_spray_aliases(self):
        self.run_probe('reuse', os.environ['JGR_NATIVE_BANK_ASSETS'])


if __name__ == '__main__':
    unittest.main()
