"""Streaming route serialization: authored bounds cases and opt-in observations.

JGR_REPLAY_PROOFS enables local native-observation parity; observations and
generated bind banks remain private test inputs, never distributed fixtures.
"""
import csv
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

class NativePose(C.Structure):
 _fields_=[('position',C.c_float*3),('scale',C.c_float*3),('angle',C.c_int32*3),
           ('evaluation',C.c_uint32),('keyed',C.c_uint32)]
class NativeEffects(C.Structure):
 _fields_=[(name,C.c_int32) for name in ('flags','motion','mirrored','grind_pose','dash','boost','obstruction')]+[
  ('frame',C.c_float),('speed',C.c_float),('external',C.c_float*3),('points',(C.c_float*3)*12),('travel_angles',C.c_int32*3)]
class NativeSample(C.Structure):
 _fields_=[('position',C.c_float*3),('angle',C.c_int32*3),('state',C.c_uint32),
  ('camera_position',C.c_float*3),('camera_direction',C.c_float*3),('camera_roll',C.c_int32),('camera_fov',C.c_int32),
  ('near_clip',C.c_float),('far_clip',C.c_float),('tick',C.c_uint32),('part',C.c_uint32),('cut',C.c_uint32),
  ('pose',NativePose*20),('effects',NativeEffects),('rival_target',C.POINTER(C.c_float))]

def native_observation(frame,bank,model=0,part=0,cut=0):
 """Private comparator adapter only. Production native generators never use this."""
 u=lambda at:struct.unpack_from('<I',frame,at)[0]
 si=lambda at:struct.unpack_from('<i',frame,at)[0]
 s=NativeSample();s.position[:]=struct.unpack_from('<3f',frame,0xb8);s.angle[:]=struct.unpack_from('<3i',frame,0x678);s.state=u(0x70)
 s.camera_position[:]=struct.unpack_from('<3f',frame,0x900+0x190);s.camera_direction[:]=struct.unpack_from('<3f',frame,0x900+0x19c)
 s.camera_roll=si(0x900+0x1a8);s.camera_fov=si(0x900+0x1ac)
 s.near_clip=-struct.unpack_from('<f',frame,0x900+0x1b0)[0];s.far_clip=-struct.unpack_from('<f',frame,0x900+0x1b4)[0]
 s.tick=u(0x98);s.part=part;s.cut=cut
 chars,bones=struct.unpack_from('<2I',bank,32);first=struct.unpack_from('<I',bank,chars+model*48)[0]
 pointer=u(0x594);table=u(0xc64)
 def observed(address,size):
  offset=address-pointer
  if offset<0 or offset+size>0x400:raise ValueError('pose outside fixture')
  return frame[0xc60+offset:0xc60+offset+size]
 for j,p in enumerate(s.pose):
  at=bones+(first+j)*48;p.position[:]=struct.unpack_from('<3f',bank,at+4);p.angle[:]=struct.unpack_from('<3i',bank,at+16)
  p.scale[:]=struct.unpack_from('<3f',bank,at+28);p.evaluation=struct.unpack_from('<H',bank,at+2)[0]
  flags,position,angles,scale=struct.unpack('<4I',observed(table+j*16,16))
  if flags&1:p.position[:]=struct.unpack('<3f',observed(position,12));p.evaluation&=~1;p.keyed|=1
  if flags&16:p.angle[:]=struct.unpack('<3h',observed(angles,6));p.evaluation&=~2;p.keyed|=2
  if flags&4:p.scale[:]=struct.unpack('<3f',observed(scale,12));p.evaluation&=~4;p.keyed|=4
 payload=b''.join(frame[a:a+4] for a in (0x5cc,0x78,0x1dc,0x1e4,0x108,0x10c,0x548))
 payload+=frame[0x5ac:0x5b0]+frame[0xdc:0xe0]+frame[0x820:0x82c]
 payload+=b''.join(frame[0x684+j*12:0x690+j*12] for j in (*range(8),*range(19,23)))
 payload+=b''.join(frame[a:a+4] for a in (0x16c,0x168,0x170));s.effects=NativeEffects.from_buffer_copy(payload)
 if s.effects.obstruction:
  target=(C.c_float*3)(*struct.unpack_from('<3f',frame,0x54c));s.rival_target=target;s._target=target
 return s

def player_models(tree):
 """Read the original header identity in compact-bank archive order."""
 models={}
 for path in sorted((Path(tree)/'decompile/afs/PLAYER').glob('*.bin')):
  data=path.read_bytes()
  if data[:4]!=b'PLAY':continue
  if len(data)<12:raise ValueError('Truncated PLAY header')
  offset=struct.unpack_from('<I',data,8)[0]
  if offset>len(data)-4:raise ValueError('PLAY identity outside source')
  character=struct.unpack_from('<I',data,offset)[0]
  if character>=16 or character in models:raise ValueError('Invalid PLAY identity')
  models[character]=len(models)
 if set(models)!=set(range(16)):raise ValueError('Missing original PLAY identities')
 return models

class ReplayPack(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory(prefix='jgr-replay-pack-');cls.work=Path(cls.temp.name)
  bridge=cls.work/'bridge.c';bridge.write_text('''#include "portable_replay_pack.h"
#include <stdint.h>
static unsigned u32(const unsigned char*p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
int pack_mode(const char*raw,const char*meta,const char*normal,unsigned model,const char*spray,unsigned character,const char*out,unsigned sections){
 FILE*r=fopen(raw,"rb"),*m=fopen(meta,"rb"),*n=fopen(normal,"rb"),*s=spray?fopen(spray,"rb"):0,*o=fopen(out,"wb"),*t=tmpfile();
 JgrReplayPack*p=n&&t?jgr_replay_pack_create(t,n,model,s,character):0;int result=-1;unsigned char row[4192],flags[12];
 if(!r||!m||!n||!o||!t||!p||fseek(r,16,SEEK_SET))goto done;
 if(sections&&jgr_replay_pack_rival(p,sections))goto done;
 for(;;){size_t got=fread(row,1,sizeof row,r);if(!got)break;if(got!=sizeof row||fread(flags,1,12,m)!=12)goto done;
  if(jgr_replay_pack_sample(p,row,row+0x900,row+0xc60,u32(flags),u32(flags+4),u32(flags+8)))goto done;}
 if(ferror(r)||fgetc(m)!=EOF)goto done;result=jgr_replay_pack_finish(p,o);
 done:jgr_replay_pack_destroy(p);if(r)fclose(r);if(m)fclose(m);if(n)fclose(n);if(s)fclose(s);if(t)fclose(t);if(o&&fclose(o))result=-1;return result;
}
int pack(const char*raw,const char*meta,const char*normal,unsigned model,const char*spray,unsigned character,const char*out){return pack_mode(raw,meta,normal,model,spray,character,out,0);}
int pack_native(const JgrReplayNativeSample*samples,unsigned count,unsigned character,unsigned sections,const char*out){
 FILE*t=tmpfile(),*o=fopen(out,"wb");JgrReplayPack*p=t?jgr_replay_pack_create_native(t,character):0;int result=-1;
 if(!p||!o||(sections&&jgr_replay_pack_rival(p,sections)))goto done;
 for(unsigned i=0;i<count;i++)if(jgr_replay_pack_native_sample(p,samples+i)){
  /* Rejection poisons the stream; no valid sample or finish can revive it. */
  unsigned before=jgr_replay_pack_count(p);JgrReplayNativeSample good={0};
  if(jgr_replay_pack_native_sample(p,&good)!=-1||jgr_replay_pack_count(p)!=before||jgr_replay_pack_finish(p,o)!=-1)result=-7;
  goto done;
 }
 result=jgr_replay_pack_finish(p,o);
 if(!result&&(jgr_replay_pack_native_sample(p,samples)!=-1||jgr_replay_pack_finish(p,o)!=-1))result=-8;
 done:jgr_replay_pack_destroy(p);if(t)fclose(t);if(o&&fclose(o))result=-1;return result;
}
''')
  subprocess.run(['cc','-std=gnu99','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-I'+str(ROOT/'psp/src'),str(bridge),str(ROOT/'psp/src/portable_replay_pack.c'),'-o',str(cls.work/'pack.so')],check=True)
  cls.lib=C.CDLL(str(cls.work/'pack.so'));cls.lib.pack.argtypes=[C.c_char_p]*3+[C.c_uint,C.c_char_p,C.c_uint,C.c_char_p]
  cls.lib.pack_mode.argtypes=cls.lib.pack.argtypes+[C.c_uint]
  cls.lib.pack_native.argtypes=[C.POINTER(NativeSample),C.c_uint,C.c_uint,C.c_uint,C.c_char_p]
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def pack(self,raw,meta,normal,model=0,spray=None,character=0,sections=0):
  out=self.work/'route.bin'
  status=self.lib.pack_mode(os.fsencode(raw),os.fsencode(meta),os.fsencode(normal),model,os.fsencode(spray) if spray else None,character,os.fsencode(out),sections)
  return status,out.read_bytes()
 def pack_native(self,samples,character=0,sections=0):
  out=self.work/'native.bin';data=(NativeSample*len(samples))(*samples)
  status=self.lib.pack_native(data,len(samples),character,sections,os.fsencode(out));return status,out.read_bytes()
 def fixture(self):
  bank=bytearray(64+48+20*48);bank[:4]=b'JGC2';struct.pack_into('<I',bank,4,1);struct.pack_into('<2I',bank,32,64,112);struct.pack_into('<2I',bank,64,0,20)
  for j in range(20):struct.pack_into('<3f',bank,112+j*48+28,1,1,1)
  bind=self.work/'bind.jgc';bind.write_bytes(bank)
  frame=bytearray(4192);struct.pack_into('<I',frame,0x594,0x8c100000);struct.pack_into('<I',frame,0xc64,0x8c100008)
  raw=self.work/'capture.bin';meta=self.work/'metadata.bin'
  return bind,frame,raw,meta
 def test_constant_and_varying_channels(self):
  bind,frame,raw,meta=self.fixture();second=bytearray(frame)
  struct.pack_into('<2I',second,0xc68,1,0x8c100180);struct.pack_into('<3f',second,0xc60+0x180,2,0,0)
  raw.write_bytes(bytes(16)+frame+second);meta.write_bytes(bytes(24))
  status,data=self.pack(raw,meta,bind);self.assertEqual(status,0)
  self.assertEqual(struct.unpack_from('<4s7I',data),(b'TRT2',2,0,20,432,1,0,0))
  self.assertEqual(struct.unpack_from('<I',data,512),(0,))
  self.assertEqual(struct.unpack_from('<f',data,516+224),(0.,))
  self.assertEqual(struct.unpack_from('<f',data,516+432+224),(2.,))
  self.assertEqual(len(data),516+864)
 def test_invalid_pose_and_truncated_observation(self):
  bind,frame,raw,meta=self.fixture();meta.write_bytes(bytes(12))
  struct.pack_into('<I',frame,0xc64,0x8c100300)
  raw.write_bytes(bytes(16)+frame);self.assertEqual(self.pack(raw,meta,bind)[0],-1)
  raw.write_bytes(bytes(16)+frame[:100]);self.assertEqual(self.pack(raw,meta,bind)[0],-1)
 def test_native_complete_frames_equal_observed_trt2_and_trt3(self):
  bind,frame,raw,meta=self.fixture();bank=bytearray(bind.read_bytes())
  for j in range(20):
   at=112+j*48;struct.pack_into('<H',bank,at+2,31)
   struct.pack_into('<3f',bank,at+4,j+.25,j+.5,-j-.125)
   struct.pack_into('<3i',bank,at+16,j*1234,-j*4321,65536+j)
   struct.pack_into('<3f',bank,at+28,1+j/32,1-j/64,.75)
  bind.write_bytes(bank);frames=[]
  for i in range(3):
   a=bytearray(frame);struct.pack_into('<3f',a,0xb8,100+i,20-i,-42.5)
   struct.pack_into('<3i',a,0x678,-40000+i,80000+i,-123)
   struct.pack_into('<I',a,0x70,3+i);struct.pack_into('<I',a,0x98,20+i)
   struct.pack_into('<6f2i2f',a,0x900+0x190,17+i,24,100,0,0,-1,-70000,13500,-1.25,-900)
   for j in range(20):
    at=0x148+j*30;flags=(1 if j%3==i else 0)|(16 if j%3==(i+1)%3 else 0)|(4 if j%3==(i+2)%3 else 0)
    struct.pack_into('<4I',a,0xc68+j*16,flags,0x8c100000+at,0x8c100000+at+12,0x8c100000+at+18)
    struct.pack_into('<3f3h3f',a,0xc60+at,j*2.+i,-j*.5,17.25,-1234,32000-j,-100+j,.5+i*.25,2,1)
   for j,at in enumerate((0x5cc,0x78,0x1dc,0x1e4,0x108,0x10c,0x548)):
    struct.pack_into('<I',a,at,0x7f800000+j if j<6 else i%2)
   struct.pack_into('<f',a,0x5ac,12+i*.25);struct.pack_into('<f',a,0xdc,-3.25+i)
   struct.pack_into('<3f',a,0x820,-9,3+i,-.125)
   for j in (*range(8),*range(19,23)):struct.pack_into('<3f',a,0x684+j*12,j+i,j+.5,-j)
   for j,at in enumerate((0x16c,0x168,0x170)):struct.pack_into('<i',a,at,-100000+j*50000)
   struct.pack_into('<3f',a,0x54c,1.5,2.5,3.5);frames.append(a)
  raw.write_bytes(bytes(16)+b''.join(frames))
  for sections in (0,3):
   with self.subTest(sections=sections):
    parts=list(range(3)) if sections else [0,0,1];meta.write_bytes(b''.join(struct.pack('<3I',part,int(i!=1),0) for i,part in enumerate(parts)))
    status,expected=self.pack(raw,meta,bind,character=15,sections=sections);self.assertEqual(status,0)
    samples=[native_observation(a,bank,part=part,cut=int(i!=1)) for i,(a,part) in enumerate(zip(frames,parts))]
    status,data=self.pack_native(samples,character=15,sections=sections);self.assertEqual(status,0);self.assertEqual(data,expected)
    _,count,character,joints,stride,channels,first,second=struct.unpack_from('<4s7I',data)
    self.assertEqual((count,character,joints),(3,15,20));self.assertEqual((first,second),(1,2) if sections else (0,0))
    at=512+channels*4
    self.assertEqual(struct.unpack_from('<2f',data,at+48),(1.25,900))
    self.assertEqual(data[at+224+channels*4:at+428+channels*4],bytes(samples[0].effects))
    if sections:
     self.assertEqual(struct.unpack_from('<3f',data,at+428+channels*4),(0,0,0))
     self.assertEqual(struct.unpack_from('<3f',data,at+stride+428+channels*4),(1.5,2.5,3.5))
 def test_native_invalid_values_poison_stream(self):
  cases={
   'position':lambda s:s.position.__setitem__(2,float('nan')),
   'camera':lambda s:setattr(s,'far_clip',float('inf')),
   'pose':lambda s:s.pose[19].scale.__setitem__(2,float('nan')),
   'effect':lambda s:s.effects.points[11].__setitem__(2,float('inf')),
   'state':lambda s:setattr(s,'state',65536),
   'part':lambda s:setattr(s,'part',65536),
   'cut':lambda s:setattr(s,'cut',2),
   'keyed':lambda s:setattr(s.pose[0],'keyed',8),
   'evaluation':lambda s:setattr(s.pose[0],'evaluation',65536),
  }
  for name,mutate in cases.items():
   with self.subTest(name=name):
    sample=NativeSample();mutate(sample);self.assertEqual(self.pack_native([sample])[0],-1)
  self.assertEqual(self.pack_native([])[0],-1)
  self.assertEqual(self.pack_native([NativeSample()],character=16)[0],-1)
 def test_native_rival_sections_and_target_validation(self):
  first=NativeSample();last=NativeSample();last.part=1
  self.assertEqual(self.pack_native([first,last],sections=2)[0],0)
  self.assertEqual(self.pack_native([last,first],sections=2)[0],-1)
  self.assertEqual(self.pack_native([first,last,first],sections=2)[0],-1)
  self.assertEqual(self.pack_native([first],sections=2)[0],-1)
  first.pose[0].evaluation=8192;self.assertEqual(self.pack_native([first,last],sections=2)[0],-1)
  first.pose[0].evaluation=0;first.effects.obstruction=1
  self.assertEqual(self.pack_native([first,last],sections=2)[0],-1)
  target=(C.c_float*3)(1,float('nan'),2);first.rival_target=target
  self.assertEqual(self.pack_native([first,last],sections=2)[0],-1)
 def test_compact_rows_and_write_failures_under_sanitizers(self):
  flags=['cc','-std=c99','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
         '-I'+str(ROOT/'psp/src')]
  obj=self.work/'pack-sanitize.o';exe=self.work/'pack-sanitize'
  subprocess.run(flags+['-Dfwrite=pack_fwrite','-c',str(ROOT/'psp/src/portable_replay_pack.c'),'-o',str(obj)],check=True)
  subprocess.run(flags+[str(ROOT/'psp/tests/replay_pack_sanitize.c'),str(obj),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 def test_source_identity_mapping(self):
  with tempfile.TemporaryDirectory() as directory:
   base=Path(directory)/'decompile/afs/PLAYER';base.mkdir(parents=True)
   for member in range(16):
    (base/f'{member:03d}_fixture.bin').write_bytes(struct.pack('<4s3I',b'PLAY',0,12,15-member))
   self.assertEqual(player_models(directory),{15-i:i for i in range(16)})
   (base/'015_fixture.bin').write_bytes(struct.pack('<4s3I',b'PLAY',0,12,15))
   with self.assertRaises(ValueError):player_models(directory)
   (base/'015_fixture.bin').write_bytes(struct.pack('<4s3I',b'PLAY',0,99,0))
   with self.assertRaises(ValueError):player_models(directory)
 def test_observed_native_routes(self):
  where=os.environ.get('JGR_REPLAY_PROOFS')
  if not where:self.skipTest('Set JGR_REPLAY_PROOFS to local native route observations')
  proofs=Path(where);bank=ROOT/'build/portable/public-review/assets-final/chars.jgc'
  models=player_models(os.environ.get('JGR_TEST_SOURCE_TREE',ROOT/'build/portable/clean-disc-proof/tree'))
  cases=[(p/'comparison',False) for p in sorted((proofs/'api-all-demos').glob('demo_*'))]
  cases.append((proofs/'api-title-comparison',False));cases.append((proofs/'staff00-native',True))
  report=[]
  for source,staff in cases:
   with self.subTest(source=source.name):
    with (source/'title.csv').open() as stream:rows=list(csv.DictReader(stream))
    character=int(rows[0]['character'])
    frames=(source/'title_actor_camera.bin').read_bytes();meta=bytearray();part=0;last_tick=0;last_cam=None;previous_part=None
    for i,row in enumerate(rows):
     at=16+i*4192;tick=struct.unpack_from('<I',frames,at+0x98)[0];cam=struct.unpack_from('<I',frames,at+0xb74)[0]
     if staff:part=int(row['part'])
     elif tick<last_tick:part+=1
     cut=(part!=previous_part if staff else part>0 and tick==1) or (last_cam is not None and cam!=last_cam)
     meta+=struct.pack('<3I',part,int(cut),int(row['actor_variant']) if staff else 0)
     last_tick=tick;last_cam=cam;previous_part=part
    m=self.work/'meta.bin';m.write_bytes(meta)
    spray=bank.parent/f'spraymesh{models[character]:02d}.jgc' if staff else None
    status,data=self.pack(source/'title_actor_camera.bin',m,bank,models[character],spray,character)
    self.assertEqual(status,0)
    normal_bytes=bank.read_bytes();spray_bytes=spray.read_bytes() if spray else None
    samples=[]
    for i in range(len(rows)):
     part,cut,variant=struct.unpack_from('<3I',meta,i*12)
     samples.append(native_observation(frames[16+i*4192:16+(i+1)*4192],spray_bytes if variant else normal_bytes,
                                       0 if variant else models[character],part,cut))
    native_status,native_data=self.pack_native(samples,character=character)
    self.assertEqual(native_status,0);self.assertEqual(native_data,data)
    expected=(bank.parent/'staffroute00.bin').read_bytes() if staff else None
    digest=hashlib.sha256(data).hexdigest()
    if staff:self.assertEqual(data,expected)
    else:self.assertEqual(digest,json.loads((source/'verified.json').read_text())['route_sha256'])
    report.append({'source':str(source),'bytes':len(data),'sha256':digest,'native_api_equal':native_data==data})
  if os.environ.get('JGR_REPLAY_PACK_REPORT'):Path(os.environ['JGR_REPLAY_PACK_REPORT']).write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':unittest.main()
