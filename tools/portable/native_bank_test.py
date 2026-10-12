"""Synthetic pose-bank bounds and sparse-loading regressions."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

class NativeBank(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='jgr-native-bank-')
        cls.root=Path(cls.temp.name)
        bridge=cls.root/'bridge.c'
        bridge.write_text(r'''
#include "portable_native_world.h"
#include <stdarg.h>
#include <string.h>
static unsigned read_bytes;
size_t bank_fread(void *p,size_t size,size_t count,FILE *f)
{size_t n=fread(p,size,count,f);read_bytes+=(unsigned)(n*size);return n;}
int jgr_native_error(const JgrNativeConfig *c,const char *s){(void)c;(void)s;return -1;}
void jgr_native_chars_pose_forget(void){}
int jgr_native_cache_bank_borrow(JgrNativeCache *c,JgrDisc *d,JgrTableDependency f,void *u,Chars *b)
{(void)c;(void)d;(void)f;(void)u;(void)b;return 0;}
void jgr_native_cache_bank_adopt(JgrNativeCache *c,JgrDisc *d,JgrTableDependency f,void *u,Chars *b)
{(void)c;(void)d;(void)f;(void)u;(void)b;}
int jgr_native_cache_bank_release(JgrNativeCache *c,Chars *b){(void)c;(void)b;return 0;}
static FILE *dependency(void *p,const char *name){(void)name;return fopen(p,"rb");}
int inspect(const char *path,unsigned out[5])
{
 JgrNativeConfig config={0};config.dependency=dependency;config.dependency_user=(void*)path;
 Chars bank={0};read_bytes=0;int result=jgr_native_bank_load(&config,&bank,"fixture.jgc");
 if(!result){out[0]=bank.resident_bytes;out[1]=read_bytes;out[2]=bank.h->n_motions;
  out[3]=bank.base!=NULL;out[4]=bank.motion_data!=NULL;
  if(bank.h->n_motions){const JgcChan *ch=(const JgcChan*)chars_data(&bank,bank.motions[0].chan0);
   float value;memcpy(&value,chars_data(&bank,bank.h->off_keys)+ch->off_pos,4);
   if(value!=12.5f)result=-2;
  }
 }
 jgr_native_bank_free(&config,&bank);jgr_native_bank_free(&config,&bank);
 if(bank.base||bank.motion_data||bank.h)result=-3;return result;
}
''')
        flags=['cc','-std=c99','-O1','-fPIC','-ffunction-sections','-fdata-sections',
               '-I'+str(ROOT/'psp/src'),'-I'+str(ROOT/'psp/tests/render_stubs'),'-I'+str(ROOT/'psp/tests/stubs')]
        obj=cls.root/'bank.o'
        subprocess.run(flags+['-Dfread=bank_fread','-c',str(ROOT/'psp/src/portable_native_bank.c'),'-o',str(obj)],check=True)
        so=cls.root/'bank.so'
        subprocess.run(flags+['-shared',str(bridge),str(obj),'-o',str(so)],check=True)
        cls.lib=C.CDLL(str(so));cls.lib.inspect.argtypes=[C.c_char_p,C.POINTER(C.c_uint)]

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def fixture(self,motions=True):
        # Deliberately leave a large geometry hole, and trailing pixels.
        n=1 if motions else 0;start=65536;keys=start+8*n+24*n;pixels=keys+12*n
        data=bytearray(pixels+4096)
        struct.pack_into('<4s16I',data,0,b'JGC2',1,1,0,0,0,0,n,
                         80,128,176,176,176,176,start,keys,pixels)
        struct.pack_into('<4I',data,80,0,1,0,n)
        struct.pack_into('<hH3f3i3f2I',data,128,-1,0,1,2,3,0,0,0,1,1,1,0,0)
        if motions:
            struct.pack_into('<2I',data,start,1,start+8)
            struct.pack_into('<6I',data,start+8,1,0,0,0,0,0)
            struct.pack_into('<3f',data,keys,12.5,2,3)
        return data

    def load(self,data):
        path=self.root/'fixture.jgc';path.write_bytes(data);out=(C.c_uint*5)()
        return self.lib.inspect(str(path).encode(),out),list(out)

    def test_keeps_only_pose_sections_and_preserves_offsets(self):
        data=self.fixture();result,out=self.load(data)
        self.assertEqual(result,0)
        self.assertEqual(out,[176+44,68+176+44,1,1,1])
        self.assertLess(out[1],len(data)//100)

    def test_motionless_spray_bank_needs_only_its_bind_pose(self):
        result,out=self.load(self.fixture(False));self.assertEqual(result,0)
        self.assertEqual(out,[176,68+176,0,1,0])

    def test_rejects_truncated_or_overlapping_sections(self):
        base=self.fixture()
        mutations=[(32,0),(36,84),(56,65537),(60,65535),(64,len(base)+1),
                   (8,0xffffffff),(28,0xffffffff),(84,2),(92,2)]
        for offset,value in mutations:
            with self.subTest(offset=offset):
                bad=bytearray(base);struct.pack_into('<I',bad,offset,value)
                self.assertEqual(self.load(bad)[0],-1)
        for n in (0,16,67,65540):
            with self.subTest(length=n):self.assertEqual(self.load(base[:n])[0],-1)

    def test_rejects_channel_and_key_ranges(self):
        base=self.fixture()
        for offset,value in [(65540,65535),(65540,65580),(65540,65545),
                             (65544,0xffffffff),(65548,12),(65548,1),
                             (65552,3),(65560,2)]:
            with self.subTest(offset=offset,value=value):
                bad=bytearray(base);struct.pack_into('<I',bad,offset,value)
                self.assertEqual(self.load(bad)[0],-1)
        for count_at,offset_at in ((65552,65556),(65560,65564)):
            bad=bytearray(base);struct.pack_into('<I',bad,count_at,1)
            struct.pack_into('<I',bad,offset_at,1)
            self.assertEqual(self.load(bad)[0],-1)

if __name__=='__main__':unittest.main()
