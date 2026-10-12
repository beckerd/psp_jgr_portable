"""Native JGS prop-bank geometry/bounds/palette parity from the original disc."""
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class Stage(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory(prefix='jgr-stage-');cls.work=Path(cls.temp.name)
  bridge=cls.work/'bridge.c';bridge.write_text('''#include "portable_stage.h"
int stage(const char *gdi,unsigned stage,const char *cache,const char *output){
 JgrDisc *d=0;char error[256];if(jgr_disc_open(&d,gdi,error,sizeof error))return -1;
 FILE *f=fopen(output,"wb");int r=f?jgr_stage_objects(d,stage,cache,f):-1;
 if(f && fclose(f))r=-1;jgr_disc_close(d);return r;}
int city(const char *gdi,unsigned stage,const char *cache,const char *output,const char *ownership){
 JgrDisc *d=0;char error[256];if(jgr_disc_open(&d,gdi,error,sizeof error))return -1;
 FILE *f=fopen(output,"w+b"),*g=fopen(ownership,"w+b");int r=f&&g?jgr_stage_city_geometry(d,stage,cache,f,g):-1;
 if(f&&fclose(f))r=-1;if(g&&fclose(g))r=-1;jgr_disc_close(d);return r;}
int surface(const char *gdi,const char *output){
 JgrDisc *d=0;char error[256];if(jgr_disc_open(&d,gdi,error,sizeof error))return -1;
 FILE*f=fopen(output,"wb");int r=f?jgr_stage_surface(d,f):-1;
 if(f&&fclose(f))r=-1;jgr_disc_close(d);return r;}
int tunnel(const char *gdi,const char *cache,const char *geometry,const char *output){
 JgrDisc *d=0;char error[256];if(jgr_disc_open(&d,gdi,error,sizeof error))return -1;
 FILE *f=fopen(geometry,"rb"),*g=fopen(output,"w+b");int r=f&&g?jgr_stage_tunnel_materials(d,cache,f,g):-1;
 if(f)fclose(f);if(g)fclose(g);jgr_disc_close(d);return r;}
''')
  names=('portable_stage','portable_stage_pack','portable_scratch','portable_models','portable_disc','portable_convert','portable_indexed')
  subprocess.run(['cc','-std=gnu99','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-shared','-fPIC',
    '-DJGR_STAGE_STATS='+str(int(os.environ.get('JGR_TEST_STAGE_STATS','0'))),
    '-I'+str(ROOT/'psp/src'),str(bridge),*[str(ROOT/'psp/src'/(n+'.c')) for n in names],'-lm','-o',str(cls.work/'native.so')],check=True)
  cls.lib=C.CDLL(str(cls.work/'native.so'));cls.lib.stage.argtypes=[C.c_char_p,C.c_uint,C.c_char_p,C.c_char_p]
  cls.lib.tunnel.argtypes=[C.c_char_p]*4
  cls.lib.city.argtypes=[C.c_char_p,C.c_uint,C.c_char_p,C.c_char_p,C.c_char_p]
  cls.lib.surface.argtypes=[C.c_char_p,C.c_char_p]
  cls.lib.jgr_stage_objects.argtypes=[C.c_void_p,C.c_uint,C.c_char_p,C.c_void_p]
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def test_invalid_stage_rejected(self):
  for stage in (0,4,7,0xffffffff):self.assertEqual(self.lib.jgr_stage_objects(None,stage,None,None),-1)
 def test_original_stage_banks(self):
  source=os.environ.get('JGR_TEST_GDI')
  if not source:self.skipTest('Set JGR_TEST_GDI to the supported original disc')
  catalog={r['path']:r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']}
  report=[]
  for stage in (1,2,3,5,6):
   name=f'stage{stage}_obj.jgs';output=self.work/name
   with self.subTest(stage=stage):
    self.assertEqual(self.lib.stage(os.fsencode(source),stage,os.fsencode(self.work),os.fsencode(output)),0)
    data=output.read_bytes();self.assertEqual(len(data),catalog[name]['bytes'])
    self.assertEqual(hashlib.sha256(data).hexdigest(),catalog[name]['sha256'])
    self.assertFalse(list(self.work.glob('jgs-setup-*.tmp')))
    report.append({'asset':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'exact':True})
  if os.environ.get('JGR_STAGE_REPORT'):
   Path(os.environ['JGR_STAGE_REPORT']).write_text(json.dumps({'source':'native original-disc reads','files':report},indent=2)+'\n')
 def test_original_water_animation(self):
  source=os.environ.get('JGR_TEST_GDI')
  if not source:self.skipTest('Set JGR_TEST_GDI to the supported original disc')
  expected=next(r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files'] if r['path']=='kogane_surface.bin')
  out=self.work/'kogane_surface.bin'
  self.assertEqual(self.lib.surface(os.fsencode(source),os.fsencode(out)),0)
  data=out.read_bytes();self.assertEqual((len(data),hashlib.sha256(data).hexdigest()),(expected['bytes'],expected['sha256']))
 def test_original_city_near_far(self):
  source=os.environ.get('JGR_TEST_GDI')
  if not source:self.skipTest('Set JGR_TEST_GDI to the supported original disc')
  # Independent desktop converter hashes, before adding the rigid prop layer.
  expected={'stage1.jgs': (7338352, '877ccb6bd37dacdfd116383321e6285a93980b83fe1c3f3846af4621e92400f3'), 'stage1.jrs': (32208, '74f8d9f6309d3e7eddcf72c885386f3117f7513cfdb106acdb6ae4aec3928816'), 'stage2.jgs': (7458800, '682c67f51a5b5db9239f5f1abc89d1eea078fec8442d537af0c882ad40a01fdf'), 'stage2.jrs': (40448, 'e9f7816d0413d4fb2e234d7318bd56e7d06ed0a25d522294699ae0268aa54c97'), 'stage3.jgs': (6663856, '37ac9f95741544e52957af07889e6639c730611d00616c8b735a7ecc293fe324'), 'stage3.jrs': (39904, '3d485736efd6cb4b61db90e5df98662b0a33460a43a3e587131e7db3ebc2ee5c'), 'stage5.jgs': (4356240, '97b5e6736f6be9f15ca5f97a675f0233afd48904b1b1f366726996970f47cf4f'), 'stage5.jrs': (17072, '86a2f495c30a917848f5bc4a4866ef4264782b74182987756f2260cd52eacefe'), 'stage6.jgs': (3400560, 'b5d8d73599d093bc5c54f27276be6643c62f465c9d1ddfe6511f7fd9d29649f0'), 'stage6.jrs': (5392, 'fd2c7a980dbcb6b6197f8f3f616863c8b3c4b57de90bb42c2a092d8bc1c9a510')}
  for stage in (1,2,3,5,6):
   with self.subTest(stage=stage):
    geometry=self.work/f'stage{stage}.jgs';ownership=geometry.with_suffix('.jrs')
    self.assertEqual(self.lib.city(os.fsencode(source),stage,os.fsencode(self.work),os.fsencode(geometry),os.fsencode(ownership)),0)
    for p in (geometry,ownership):
     data=p.read_bytes();self.assertEqual((len(data),hashlib.sha256(data).hexdigest()),expected[p.name])
    self.assertFalse(list(self.work.glob('jgs-*.tmp')))
 def test_tunnel_authored_material_capture(self):
  source,assets=os.environ.get('JGR_TEST_GDI'),os.environ.get('JGR_TEST_ASSETS')
  if not source or not assets:self.skipTest('Private original disc and final geometry not supplied')
  import struct
  geometry=Path(assets)/'stage2.jgs';output=self.work/'tunnel.raw'
  self.assertEqual(self.lib.tunnel(os.fsencode(source),os.fsencode(self.work),os.fsencode(geometry),os.fsencode(output)),0)
  raw=output.read_bytes();self.assertEqual(len(raw),52)
  jsd=(Path(assets)/'source_data_ptr_kogane_tunnel_materials.jsd').read_bytes()
  self.assertEqual(raw,jsd[-52:]);self.assertFalse(list(self.work.glob('tunnel-setup-*.tmp')))
  # A verified final geometry material must agree with every captured source strip.
  invalid=self.work/'wrong.jgs';data=bytearray(geometry.read_bytes());batch=struct.unpack_from('<H',raw)[0];at=struct.unpack_from('<I',data,32)[0]+batch*16+12;data[at]^=1;invalid.write_bytes(data)
  self.assertEqual(self.lib.tunnel(os.fsencode(source),os.fsencode(self.work),os.fsencode(invalid),os.fsencode(output)),-1)
if __name__=='__main__':unittest.main()
