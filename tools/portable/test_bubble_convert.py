"""Disc-only spray-bubble geometry: parity and malformed source bounds."""
import hashlib,json,os,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
DRIVER=r'''
#include "portable_bubble.h"
#include <string.h>
int main(int argc,char **argv){JgrDisc*d;JgrDiscFile f;char error[192];if(argc!=3)return 2;if(jgr_disc_open(&d,argv[1],error,sizeof error)||jgr_disc_source(d,"decompile/afs/PLAYER/016_player_effect_dat.bin",&f))return 3;if(!strcmp(argv[2],"bad"))f.size=4;int r=jgr_convert_bubble(d,&f,stdout);jgr_disc_close(d);return r?4:fflush(stdout)!=0;}
'''
class BubbleTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory();cls.folder=Path(cls.temp.name);cls.exe=cls.folder/'bubble';h=cls.folder/'h.c';h.write_text(DRIVER)
  subprocess.run(['cc','-std=c99','-O2','-ffp-contract=off','-D_XOPEN_SOURCE=700','-Wall','-Wextra','-Werror','-I'+str(ROOT/'psp/src'),str(h),*[str(ROOT/'psp/src'/f'{n}.c') for n in ('portable_bubble','portable_models','portable_disc')],'-lm','-o',str(cls.exe)],check=True)
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def test_source_disc_matches_catalog(self):
  gdi=os.environ.get('JGR_TEST_GDI')
  if not gdi:self.skipTest('Set JGR_TEST_GDI')
  row=next(r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files'] if r['path']=='spray_bubble.jsd')
  p=subprocess.run([str(self.exe),gdi,'ok'],capture_output=True);self.assertEqual(p.returncode,0,p.stderr.decode());self.assertEqual(len(p.stdout),row['bytes']);self.assertEqual(hashlib.sha256(p.stdout).hexdigest(),row['sha256'])
 def test_declared_source_range_checked_before_output(self):
  gdi=os.environ.get('JGR_TEST_GDI')
  if not gdi:self.skipTest('Set JGR_TEST_GDI')
  p=subprocess.run([str(self.exe),gdi,'bad'],capture_output=True);self.assertNotEqual(p.returncode,0);self.assertEqual(p.stdout,b'')
 def test_psp_compile(self):
  cc=os.environ.get('PSP_CC')
  if not cc:self.skipTest('Set PSP_CC')
  subprocess.run([cc,'-std=c99','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-DJGR_PORTABLE_ASSETS=1','-include',str(ROOT/'psp/src/portable_assets.h'),'-I'+str(ROOT/'psp/src'),'-c',str(ROOT/'psp/src/portable_bubble.c'),'-o',str(self.folder/'bubble.o')],check=True)
if __name__=='__main__':unittest.main()
