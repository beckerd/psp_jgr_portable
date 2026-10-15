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
  expected={'stage1.jgs': (7338352, '877ccb6bd37dacdfd116383321e6285a93980b83fe1c3f3846af4621e92400f3'), 'stage1.jrs': (32208, '74f8d9f6309d3e7eddcf72c885386f3117f7513cfdb106acdb6ae4aec3928816'), 'stage2.jgs': (7479056, '955716be27a009fe16bdf9e2a2b044e3c24ee97f6c361262e53b734e8956b699'), 'stage2.jrs': (40448, '443a0554e64a149351ddda70158f46be61487c4d68e16fad2638eaba2be21b55'), 'stage3.jgs': (6668288, '49ce45f44ba7930505dbbee68f5b4e8a50dbf339c51d1377f2934f9d096b0db8'), 'stage3.jrs': (39904, 'f9acca459b131f42e1909659a6244bac65d270440bf510874bc3097d8c348e98'), 'stage5.jgs': (4356240, '97b5e6736f6be9f15ca5f97a675f0233afd48904b1b1f366726996970f47cf4f'), 'stage5.jrs': (17072, '86a2f495c30a917848f5bc4a4866ef4264782b74182987756f2260cd52eacefe'), 'stage6.jgs': (3400560, 'b5d8d73599d093bc5c54f27276be6643c62f465c9d1ddfe6511f7fd9d29649f0'), 'stage6.jrs': (5392, 'fd2c7a980dbcb6b6197f8f3f616863c8b3c4b57de90bb42c2a092d8bc1c9a510')}
  for stage in (1,2,3,5,6):
   with self.subTest(stage=stage):
    geometry=self.work/f'stage{stage}.jgs';ownership=geometry.with_suffix('.jrs')
    self.assertEqual(self.lib.city(os.fsencode(source),stage,os.fsencode(self.work),os.fsencode(geometry),os.fsencode(ownership)),0)
    for p in (geometry,ownership):
     data=p.read_bytes();self.assertEqual((len(data),hashlib.sha256(data).hexdigest()),expected[p.name])
    self.assertFalse(list(self.work.glob('jgs-*.tmp')))
 def test_original_powerline_endpoints_poles_and_subdivision(self):
  """Original 039558/048580 TRS, independently applied to disc vertices.

  Check every wire/pole instance and both LODs, not just output checksums.
  Long restored spans must also retain safe PSP triangle sizes and bounds.
  """
  source=os.environ.get('JGR_TEST_GDI')
  if not source:self.skipTest('Set JGR_TEST_GDI to the supported original disc')
  import math
  import struct
  import sys
  from disc import Disc
  sys.path.insert(0,str(ROOT/'decompile/tools'))
  import ninjalib as nl
  import stage_render_assets as sr
  def authored(point,record):
   x,y,z=(point[k]*record['scale'][k] for k in range(3))
   ax,ay,az=(a*math.tau/65536 for a in record['ang'])
   y,z=y*math.cos(ax)-z*math.sin(ax),y*math.sin(ax)+z*math.cos(ax)
   x,z=x*math.cos(ay)+z*math.sin(ay),-x*math.sin(ay)+z*math.cos(ay)
   x,y=x*math.cos(az)-y*math.sin(az),x*math.sin(az)+y*math.cos(az)
   return tuple(v+record['pos'][k] for k,v in enumerate((x,y,z)))
  with Disc(Path(source)) as disc:
   e=disc.entry('1ST_READ.BIN');exe=disc.extent(e.lba,e.size)
   for stage,types,expected_count in ((2,{23,24,25,26,27,70},95),(3,{7,95},29)):
    e=disc.entry(f'JETRADIO/STAGE{stage}.AFS')
    offset,size=struct.unpack_from('<II',disc.extent(e.lba,2048),8)
    self.assertEqual(offset%2048,0)
    s=sr.Source.__new__(sr.Source);s.exe=exe;s.data=disc.extent(e.lba+offset//2048,size)
    s.image=bytearray(sr.SB-sr.EB+len(s.data));s.image[:len(exe)]=exe;s.image[sr.SB-sr.EB:]=s.data
    bank=self.work/f'powerlines{stage}.jgs';ownership=bank.with_suffix('.jrs')
    self.assertEqual(self.lib.city(os.fsencode(source),stage,os.fsencode(self.work),os.fsencode(bank),os.fsencode(ownership)),0)
    data,meta=bank.read_bytes(),ownership.read_bytes()
    _,_,ns,nr,*_=struct.unpack_from('<4s7I',meta)
    records={}
    for i in range(nr):
     row=struct.unpack_from('<4H3fHHfI',meta,32+16*ns+32*i);records[row[-1]]=row
    ot,oo,ob,ov,oi,op=struct.unpack_from('<6I',data,24)
    table,near,far,stride,count,_=sr.TABLES[stage]['F']
    placements=[r for r in s.records(table,ns,stride,count) if r['pid'] in types]
    self.assertEqual(len(placements),expected_count)
    for record in placements:
     for lod,root_table in enumerate((near,far)):
      if not root_table:continue
      first,n=records[record['address']][lod*2:lod*2+2]
      if not n:continue
      with self.subTest(stage=stage,address=hex(record['address']),lod=lod):
       actual=[]
       for obj in range(first,first+n):
        center=struct.unpack_from('<4f',data,oo+32*obj)
        fb,nb=struct.unpack_from('<2I',data,oo+32*obj+16)
        for batch in range(fb,fb+nb):
         fv,fi,nv,ni,_,_=struct.unpack_from('<II4H',data,ob+16*batch)
         points=[struct.unpack_from('<3f',data,ov+24*(fv+i)+12) for i in range(nv)];actual.extend(points)
         self.assertTrue(all(math.dist(p,center[:3])<=center[3]+.001 for p in points))
         ids=struct.unpack_from('<'+'H'*ni,data,oi+fi*2)
         for i in range(0,ni,3):
          tri=ids[i:i+3]
          self.assertTrue(all(math.dist(points[tri[k]],points[tri[(k+1)%3]])<=120.001 for k in range(3)))
       root=s.words(root_table+record['pid']*4)[0];cache={};verified=0
       for node,matrix in sr.tree(s,root,sr.IDENTITY):
        if not node['model'] or node['ev']&8:continue
        vl,pl=s.words(node['model'],2)
        if not vl or not pl:continue
        cache.update(nl.parse_vlist(s.image,vl-sr.EB))
        used={i for _,_,_,tri in nl.parse_plist(s.image,pl-sr.EB) for i,_ in tri}
        for i in used:
         point=authored(nl.xform(matrix,cache[i][0]),record)
         self.assertLess(min(math.dist(point,p) for p in actual),.001)
         verified+=1
       self.assertGreater(verified,0)
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
