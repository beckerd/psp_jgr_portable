"""Native derived mission model masks from verified generated mission banks."""
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
class MissionCast(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(prefix='jgr-mission-cast-');cls.work=Path(cls.tmp.name)
  bridge=cls.work/'bridge.c';bridge.write_text('''#include "portable_mission_cast.h"
int run(const char*gdi,const char*assets,const char*output){JgrDisc*d=0;char error[256];if(jgr_disc_open(&d,gdi,error,256))return -1;FILE*o=fopen(output,"wb");int r=o?jgr_mission_cast(d,assets,o):-1;if(o)fclose(o);jgr_disc_close(d);return r;}''')
  subprocess.run(['cc','-std=gnu99','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-I'+str(ROOT/'psp/src'),str(bridge),str(ROOT/'psp/src/portable_mission_cast.c'),str(ROOT/'psp/src/portable_disc.c'),'-o',str(cls.work/'native.so')],check=True)
  cls.lib=C.CDLL(str(cls.work/'native.so'));cls.lib.run.argtypes=[C.c_char_p]*3;cls.lib.jgr_mission_cast.argtypes=[C.c_void_p,C.c_char_p,C.c_void_p]
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_invalid_arguments(self):self.assertEqual(self.lib.jgr_mission_cast(None,None,None),-1)
 @unittest.skipUnless(os.environ.get('JGR_TEST_GDI') and os.environ.get('JGR_TEST_ASSETS'),'Private original disc and verified assets not supplied')
 def test_original_mission_cast(self):
  assets=Path(os.environ['JGR_TEST_ASSETS']);output=self.work/'cast.raw';self.assertEqual(self.lib.run(os.fsencode(os.environ['JGR_TEST_GDI']),os.fsencode(assets),os.fsencode(output)),0)
  raw=output.read_bytes();self.assertEqual(len(raw),492);jsd=(assets/'source_data_ptr_mission_cast.jsd').read_bytes()
  self.assertEqual(raw,jsd[96:576]+jsd[576:584]+jsd[592:596])
  if os.environ.get('JGR_CAST_REPORT'):Path(os.environ['JGR_CAST_REPORT']).write_text(json.dumps(dict(kind='native-derived-mission-cast-parity',bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()),indent=2)+'\n')
  invalid=self.work/'bad';invalid.mkdir();(invalid/'missions.bin').write_bytes(b'MIS3'+struct.pack('<I',0xffffffff));self.assertEqual(self.lib.run(os.fsencode(os.environ['JGR_TEST_GDI']),os.fsencode(invalid),os.fsencode(output)),-1)
if __name__=='__main__':unittest.main()
