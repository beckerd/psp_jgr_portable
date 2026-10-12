"""Native authored Grind Square light-band JGS conversion."""
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class GrindConvert(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(prefix='jgr-grind-convert-');cls.work=Path(cls.tmp.name)
  bridge=cls.work/'bridge.c';bridge.write_text('''#include "portable_grind_convert.h"
int run(const char*gdi,const char*cache,const char*output){JgrDisc*d=0;char error[256];if(jgr_disc_open(&d,gdi,error,sizeof error))return -1;FILE*f=fopen(output,"w+b");int r=f?jgr_grind_lights(d,cache,f):-1;if(f)fclose(f);jgr_disc_close(d);return r;}''')
  names=('portable_grind_convert','portable_stage','portable_stage_pack','portable_models','portable_disc','portable_convert','portable_indexed')
  subprocess.run(['cc','-std=gnu99','-O2','-ffp-contract=off','-shared','-fPIC','-I'+str(ROOT/'psp/src'),str(bridge),*[str(ROOT/'psp/src'/(n+'.c')) for n in names],'-lm','-o',str(cls.work/'native.so')],check=True)
  cls.lib=C.CDLL(str(cls.work/'native.so'));cls.lib.run.argtypes=[C.c_char_p]*3
  cls.lib.jgr_grind_lights.argtypes=[C.c_void_p,C.c_char_p,C.c_void_p]
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_missing_arguments(self):self.assertEqual(self.lib.jgr_grind_lights(None,None,None),-1)
 @unittest.skipUnless(os.environ.get('JGR_TEST_GDI'),'Private original input not supplied')
 def test_native_grind_lights(self):
  path=self.work/'grind_lights.jgs';self.assertEqual(self.lib.run(os.fsencode(os.environ['JGR_TEST_GDI']),os.fsencode(self.work),os.fsencode(path)),0)
  data=path.read_bytes();catalog=json.loads((ROOT/'tools/portable/asset_catalog.json').read_text());expected=next(r for r in catalog['files'] if r['path']==path.name)
  self.assertEqual(len(data),expected['bytes']);self.assertEqual(hashlib.sha256(data).hexdigest(),expected['sha256']);self.assertFalse(list(self.work.glob('jgs-setup-*.tmp')))
  if os.environ.get('JGR_GRIND_REPORT'):Path(os.environ['JGR_GRIND_REPORT']).write_text(json.dumps(dict(kind='native-disc-grind-light-parity',files=[expected]),indent=2)+'\n')
if __name__=='__main__':unittest.main()
