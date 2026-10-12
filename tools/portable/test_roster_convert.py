"""Lossless native cast archive and bounded input/cache checks."""
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class RosterConvert(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(prefix='jgr-roster-convert-');cls.work=Path(cls.tmp.name)
  bridge=cls.work/'bridge.c';bridge.write_text('''#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "portable_roster_convert.h"
#include "portable_scratch.h"
static FILE *source_file;
static size_t failed_allocation;
static unsigned fail_bulk_read,source_reads;
static void *test_malloc(size_t n){if(n==failed_allocation){failed_allocation=0;errno=ENOMEM;return NULL;}return malloc(n);}
static size_t test_fread(void *p,size_t n,size_t count,FILE *f){
 if(f==source_file){source_reads++;if(fail_bulk_read && n*count>=512u*1024u){fail_bulk_read=0;errno=EIO;return 0;}}
 return fread(p,n,count,f);
}
#define malloc test_malloc
#define fread test_fread
#include "portable_roster_convert.c"
#undef malloc
#undef fread
unsigned read_calls(void){return source_reads;}
int run(const char*chars,const char*light,const char*cache,const char*output,unsigned mode){
 FILE*a=fopen(chars,"rb"),*b=fopen(light,"rb"),*o=fopen(output,"w+b");
 source_file=a;source_reads=0;failed_allocation=0;fail_bulk_read=mode==4;
 jgr_roster_test_source_disabled(mode==1);
 if(mode==2 && a && !fseek(a,0,SEEK_END)){long n=ftell(a);if(n>0)failed_allocation=(size_t)n;rewind(a);}
 int r=-1;JgrScratchPool *pool=NULL;
 if(jgr_scratch_setup_phase(1))goto done;
 if(mode==3){pool=jgr_scratch_begin();if(!pool)goto done;pool->budget=4096;}
 r=jgr_roster_convert(a,b,cache,o);
 if(pool && jgr_scratch_end())r=-1;
 done:if(a && fclose(a))r=-1;if(b && fclose(b))r=-1;if(o && fclose(o))r=-1;
 source_file=NULL;failed_allocation=0;fail_bulk_read=0;return r;
}''')
  subprocess.run(['cc','-std=gnu99','-O2','-Wall','-Wextra','-Werror','-DJGR_ROSTER_TESTING','-shared','-fPIC','-I'+str(ROOT/'psp/src'),str(bridge),str(ROOT/'psp/src/portable_scratch.c'),'-o',str(cls.work/'native.so')],check=True)
  cls.lib=C.CDLL(str(cls.work/'native.so'));cls.lib.run.argtypes=[C.c_char_p]*4+[C.c_uint]
  cls.lib.jgr_roster_test_source_stats.argtypes=[C.POINTER(C.c_uint)]*2
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def run_roster(self,chars,light,cache=None,mode=0):
  return self.lib.run(os.fsencode(chars),os.fsencode(light),os.fsencode(cache or self.work),os.fsencode(self.work/'output.bin'),mode)
 def stats(self):
  loaded=C.c_uint();reclaimed=C.c_uint();self.lib.jgr_roster_test_source_stats(C.byref(loaded),C.byref(reclaimed))
  return loaded.value,reclaimed.value
 def test_truncated_and_oversized_input(self):
  chars=self.work/'chars';light=self.work/'light';light.write_bytes(b'PLT1'+bytes(16))
  for size in (0,4,79):chars.write_bytes(b'JGC2'+bytes(max(0,size-4)));self.assertEqual(self.run_roster(chars,light),-1)
  b=bytearray(80);b[:4]=b'JGC2';b[68:72]=b'JCL1';struct.pack_into('<I',b,72,51);struct.pack_into('<I',b,4,0xffffffff);chars.write_bytes(b);self.assertEqual(self.run_roster(chars,light),-1)
 @unittest.skipUnless(os.environ.get('JGR_TEST_ASSETS'),'Private verified generated assets not supplied')
 def test_archive_and_cache_safety(self):
  assets=Path(os.environ['JGR_TEST_ASSETS']);chars=assets/'chars_compact.jgc';light=assets/'playerlight.bin'
  self.assertEqual(self.run_roster(chars,light),0);data=(self.work/'output.bin').read_bytes();self.assertEqual(data,(assets/'chars_roster.bin').read_bytes());self.assertFalse(list(self.work.glob('roster-setup-*.tmp')))
  expected=dict(path='chars_roster.bin',bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
  if os.environ.get('JGR_ROSTER_REPORT'):Path(os.environ['JGR_ROSTER_REPORT']).write_text(json.dumps(dict(kind='native-derived-cast-parity',files=[expected]),indent=2)+'\n')
  target=self.work/'sentinel';target.write_bytes(b'preserve');link=self.work/'roster-setup-0.tmp';link.symlink_to(target)
  self.assertEqual(self.run_roster(chars,light),-1);self.assertEqual(target.read_bytes(),b'preserve');self.assertTrue(link.is_symlink());link.unlink()
  bad=self.work/'wrong_light';b=bytearray(light.read_bytes());b[12]^=1;bad.write_bytes(b);self.assertEqual(self.run_roster(chars,bad),-1)
 @unittest.skipUnless(os.environ.get('JGR_TEST_ASSETS'),'Private verified generated assets not supplied')
 def test_source_cache_fallback_pressure_and_read_failure(self):
  assets=Path(os.environ['JGR_TEST_ASSETS']);chars=assets/'chars_compact.jgc';light=assets/'playerlight.bin';expected=(assets/'chars_roster.bin').read_bytes()
  reads={}
  for mode,stats in ((0,(1,0)),(1,(0,0)),(2,(0,0)),(3,(1,1))):
   with self.subTest(mode=mode):
    self.assertEqual(self.run_roster(chars,light,mode=mode),0)
    self.assertEqual((self.work/'output.bin').read_bytes(),expected)
    self.assertEqual(self.stats(),stats);reads[mode]=self.lib.read_calls()
    self.assertFalse(list(self.work.glob('roster-setup-*.tmp')))
  self.assertLess(reads[0]*100,reads[1])
  self.assertEqual(self.run_roster(chars,light,mode=4),-1)
  self.assertEqual(self.stats(),(0,0));self.assertFalse(list(self.work.glob('roster-setup-*.tmp')))
  # A failed preload must leave no dangling reclaim scope or active scratch pool.
  self.assertEqual(self.run_roster(chars,light),0);self.assertEqual(self.stats(),(1,0))
  self.assertEqual((self.work/'output.bin').read_bytes(),expected)
if __name__=='__main__':unittest.main()
