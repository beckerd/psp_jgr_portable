"""Authored pedestrian strips exactly matched to native-generated JGC banks."""
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
class PeopleConvert(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(prefix='jgr-people-convert-');cls.work=Path(cls.tmp.name)
  bridge=cls.work/'bridge.c';bridge.write_text('''#include "portable_people_convert.h"
int run(const char*gdi,unsigned city,const char*bank,const char*output){JgrDisc*d=0;char error[256];if(jgr_disc_open(&d,gdi,error,256))return -1;FILE*b=fopen(bank,"rb"),*o=fopen(output,"w+b");int r=jgr_people_native(d,city,b,o);if(b)fclose(b);if(o)fclose(o);jgr_disc_close(d);return r;}''')
  subprocess.run(['cc','-std=gnu99','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-shared','-fPIC','-I'+str(ROOT/'psp/src'),str(bridge),str(ROOT/'psp/src/portable_people_convert.c'),str(ROOT/'psp/src/portable_disc.c'),'-lm','-o',str(cls.work/'native.so')],check=True)
  cls.lib=C.CDLL(str(cls.work/'native.so'));cls.lib.run.argtypes=[C.c_char_p,C.c_uint,C.c_char_p,C.c_char_p];cls.lib.jgr_people_native.argtypes=[C.c_void_p,C.c_uint,C.c_void_p,C.c_void_p]
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_invalid_arguments(self):
  for city in(0,1,2,3,4,5,6,0xffffffff):self.assertEqual(self.lib.jgr_people_native(None,city,None,None),-1)
 @unittest.skipUnless(os.environ.get('JGR_TEST_GDI') and os.environ.get('JGR_TEST_ASSETS'),'Private original disc and verified banks not supplied')
 def test_native_people_sidecars_and_mismatched_geometry(self):
  assets=Path(os.environ['JGR_TEST_ASSETS']);rows=[];source=os.fsencode(os.environ['JGR_TEST_GDI'])
  for city in(1,2,3,5):
   with self.subTest(city=city):
    bank=assets/f'people{city}_compact.jgc';out=self.work/f'people{city}_native.jpn'
    self.assertEqual(self.lib.run(source,city,os.fsencode(bank),os.fsencode(out)),0);raw=out.read_bytes();self.assertEqual(raw,(assets/out.name).read_bytes());rows.append(dict(path=out.name,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()))
  if os.environ.get('JGR_PEOPLE_REPORT'):Path(os.environ['JGR_PEOPLE_REPORT']).write_text(json.dumps(dict(kind='native-disc-pedestrian-strips-parity',files=rows),indent=2)+'\n')
  bad=self.work/'bad.jgc';data=bytearray((assets/'people1_compact.jgc').read_bytes());vertices=struct.unpack_from('<I',data,44)[0];data[vertices]^=1;bad.write_bytes(data)
  self.assertEqual(self.lib.run(source,1,os.fsencode(bad),os.fsencode(self.work/'bad.jpn')),-1)
  data=bytearray((assets/'people1_compact.jgc').read_bytes());struct.pack_into('<I',data,20,0xffffffff);bad.write_bytes(data)
  self.assertEqual(self.lib.run(source,1,os.fsencode(bad),os.fsencode(self.work/'bad.jpn')),-1)
if __name__=='__main__':unittest.main()
