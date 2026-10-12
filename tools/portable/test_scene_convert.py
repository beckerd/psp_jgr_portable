"""Native scene archive/text/staff-card parity against private original inputs."""
import hashlib
import ctypes as C
import struct
import zlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import scene_recipes
ROOT=Path(__file__).resolve().parents[2]
DRIVER=r'''
#include "portable_scene_convert.h"
#include "portable_convert.h"
#include <stdlib.h>
int main(int argc,char **argv){
 if(argc!=5)return 2;JgrDisc *d=NULL;char error[256];JgrDiscFile source;
 if(jgr_disc_open(&d,argv[1],error,sizeof error)){fprintf(stderr,"%s\n",error);return 3;}
 unsigned op=(unsigned)atoi(argv[2]);int r=jgr_disc_source(d,argv[3],&source);
 if(!r){FILE *out=fopen(argv[4],"wb");r=out?(op==2?jgr_convert_tex2d(d,&source,512,out):jgr_scene_convert(d,&source,op,out)):-1;if(out)fclose(out);}
 jgr_disc_close(d);return r?4:0;
}
'''
class SceneConvert(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  if not shutil.which('cc'):raise unittest.SkipTest('C compiler unavailable')
  cls.tmp=tempfile.TemporaryDirectory(prefix='jgr-scene-convert-');cls.work=Path(cls.tmp.name);cls.driver=cls.work/'driver';source=cls.work/'driver.c';source.write_text(DRIVER)
  subprocess.run(['cc','-std=gnu99','-O2','-ffp-contract=off','-I',str(ROOT/'psp/src'),str(source),*[str(ROOT/'psp/src'/name) for name in ('portable_scene_convert.c','portable_convert.c','portable_disc.c')],'-lm','-o',str(cls.driver)],check=True)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_metadata(self):
  jobs=scene_recipes.recipes();self.assertEqual(len(jobs),23)
  for op,source,args in jobs.values():self.assertIn(op,(2,80,81,82));self.assertTrue(source.startswith('decompile/'));self.assertEqual(len(args),4)
 @unittest.skipUnless(os.environ.get('JGR_TEST_GDI') and os.environ.get('JGR_TEST_ASSETS'),'Private original input not supplied')
 def test_native_scene_catalog(self):
  rows=[]
  for name,(op,source,args) in scene_recipes.recipes().items():
   with self.subTest(name=name):
    output=self.work/name;run=subprocess.run([self.driver,os.environ['JGR_TEST_GDI'],str(op),source,output],capture_output=True,text=True)
    self.assertEqual(run.returncode,0,run.stderr);data=output.read_bytes();self.assertEqual(data,(Path(os.environ['JGR_TEST_ASSETS'])/name).read_bytes())
    rows.append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
  if os.environ.get('JGR_SCENE_REPORT'):Path(os.environ['JGR_SCENE_REPORT']).write_text(json.dumps(dict(kind='native-disc-scene-parity',cases=rows),indent=2)+'\n')

# Synthetic archives exercise exact deduplication without original game files.
# The two eight-byte words have the same CRC32 but different contents.
SYNTHETIC=r"""
#include "portable_scene_convert.h"
#include <stdlib.h>
#include <string.h>
struct JgrDisc {unsigned unused;};
static const char *const names[]={"EVENT","ENEMY","E_SND_SE","SND_SE","E_SND_MUSEN","SND_MUSEN"};
static const unsigned char *payload[8];static unsigned lengths[8],number;
static unsigned long long read_bytes;
static void put(unsigned char *p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
int jgr_disc_find(JgrDisc *d,const char *path,JgrDiscFile *out){
 (void)d;for(unsigned i=0;i<6;i++){char p[64];snprintf(p,sizeof p,"JETRADIO/%s.AFS",names[i]);
 if(!strcmp(path,p)){out->offset=i+1;out->size=8+number*8;return 0;}}return -1;
}
int jgr_disc_read(JgrDisc *d,const JgrDiscFile *f,uint32_t at,void *out,size_t n){
 (void)d;if(f->offset<=6){unsigned char h[8]={'A','F','S',0};put(h+4,f->offset==1?number:0);
 if(at>8||n>8-at)return -1;memcpy(out,h+at,n);return 0;}
 unsigned i=(unsigned)f->offset-100;if(i>=number||at>lengths[i]||n>lengths[i]-at)return -1;
 memcpy(out,payload[i]+at,n);read_bytes+=n;return 0;
}
int jgr_disc_afs(JgrDisc *d,const JgrDiscFile *f,unsigned index,JgrDiscFile *out){
 (void)d;if(f->offset!=1||index>=number)return -1;out->offset=100+index;out->size=lengths[index];return 0;
}
int jgr_disc_afs_named(JgrDisc *d,const JgrDiscFile *f,unsigned i,JgrDiscFile *o,char name[33])
{if(name)name[0]=0;return jgr_disc_afs(d,f,i,o);}
int fixture(const char *path,const unsigned char *const *data,const unsigned *sizes,unsigned count,unsigned *crc){
 if(count>8)return -1;number=count;read_bytes=0;
 for(unsigned i=0;i<count;i++){payload[i]=data[i];lengths[i]=sizes[i];}
 FILE *f=fopen(path,"wb");if(!f)return -1;struct JgrDisc d={0};
 int result=jgr_scene_dependencies_crc(&d,f,crc);if(fclose(f))result=-1;return result;
}
unsigned long long fixture_read_bytes(void){return read_bytes;}
"""
class SceneDedup(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(prefix='jgr-scene-dedup-');cls.work=Path(cls.tmp.name)
  source=cls.work/'driver.c';source.write_text(SYNTHETIC)
  lib=cls.work/'scene.so'
  subprocess.run(['cc','-shared','-fPIC','-std=c99','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'psp/src'),str(source),str(ROOT/'psp/src/portable_scene_convert.c'),'-o',str(lib)],check=True)
  cls.lib=C.CDLL(str(lib));cls.lib.fixture.argtypes=[C.c_char_p,C.POINTER(C.c_char_p),C.POINTER(C.c_uint),C.c_uint,C.POINTER(C.c_uint)]
  cls.lib.fixture_read_bytes.restype=C.c_ulonglong
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def run_case(self,payloads):
  data=(C.c_char_p*len(payloads))(*payloads);sizes=(C.c_uint*len(payloads))(*(len(p) for p in payloads));crc=C.c_uint()
  output=self.work/'out.bin'
  self.assertEqual(self.lib.fixture(os.fsencode(output),data,sizes,len(payloads),C.byref(crc)),0)
  value=output.read_bytes();self.assertEqual(crc.value,zlib.crc32(value))
  expected=bytearray(b'CAS1'+struct.pack('<I',len(payloads)));body=bytearray();offsets={};cursor=8+32*len(payloads)
  for i,p in enumerate(payloads):
   if p not in offsets:offsets[p]=cursor+len(body);body.extend(p)
   expected.extend(b'EVENT'.ljust(16,b'\0')+struct.pack('<4I',i,offsets[p],len(p),0))
  expected.extend(body);self.assertEqual(value,expected)
  return self.lib.fixture_read_bytes()
 def test_crc_collision_never_merges_different_contents(self):
  a,b=b'plumless',b'buckeroo';self.assertEqual(zlib.crc32(a),zlib.crc32(b))
  self.run_case([a,b,a,b,b'',b''])
 def test_unique_sizes_read_once_and_large_blocks_are_exact(self):
  values=[b'a',b'bc',bytes(range(256))*1101,b'q'*133117]
  self.assertEqual(self.run_case(values),sum(map(len,values)))
 def test_duplicate_spanning_transfer_blocks(self):
  a=bytes(range(256))*1101
  self.run_case([a,a,bytes([a[0]^1])+a[1:],a])
 def test_empty_archives(self):self.assertEqual(self.run_case([]),0)

if __name__=='__main__':unittest.main()
