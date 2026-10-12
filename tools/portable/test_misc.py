"""Native small-format providers: original-disc byte parity and bad requests."""
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from misc_recipes import recipes
ROOT=Path(__file__).resolve().parents[2]
class Misc(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory(prefix='jgr-misc-');cls.work=Path(cls.temp.name)
  bridge=cls.work/'bridge.c';bridge.write_text('''#include "portable_misc.h"
int convert(const char *gdi,const char *name,const char *out){JgrDisc *d=0;char error[256];
 if(jgr_disc_open(&d,gdi,error,sizeof error))return -1;FILE *f=fopen(out,"wb");int r=f?jgr_misc_convert(d,name,f):-1;
 if(f&&fclose(f))r=-1;jgr_disc_close(d);return r;}
''')
  subprocess.run(['cc','-std=gnu99','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-shared','-fPIC','-I'+str(ROOT/'psp/src'),str(bridge),*[str(ROOT/'psp/src'/(n+'.c')) for n in ('portable_misc','portable_hardware','portable_models','portable_disc','portable_convert')],'-lm','-o',str(cls.work/'native.so')],check=True)
  cls.lib=C.CDLL(str(cls.work/'native.so'));cls.lib.convert.argtypes=[C.c_char_p]*3;cls.lib.jgr_misc_known.argtypes=[C.c_char_p]
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def test_unknown_output(self):
  for name in (None,b'',b'../gamehud.bin',b'fake.bin'):self.assertEqual(self.lib.jgr_misc_known(name),0)
  self.assertEqual(self.lib.jgr_misc_convert(None,b'gamehud.bin',None),-1)
 def test_source_formats(self):
  gdi=os.environ.get('JGR_TEST_GDI')
  if not gdi:self.skipTest('Set JGR_TEST_GDI to supported original disc')
  catalog={r['path']:r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']}
  report=[]
  for name in recipes():
   with self.subTest(asset=name):
    out=self.work/name;self.assertEqual(self.lib.convert(os.fsencode(gdi),name.encode(),os.fsencode(out)),0)
    data=out.read_bytes();actual=(len(data),hashlib.sha256(data).hexdigest());self.assertEqual(actual,(catalog[name]['bytes'],catalog[name]['sha256']))
    report.append({'path':name,'bytes':len(data),'sha256':actual[1]})
  if os.environ.get('JGR_MISC_REPORT'):Path(os.environ['JGR_MISC_REPORT']).write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':unittest.main()
