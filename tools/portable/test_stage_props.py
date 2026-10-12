"""Bounded native city/prop metadata validation; full-disc parity lives in
`test_native_install.py` and covers all twenty final city/sidecar assets.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
HARNESS=r'''
#include "portable_stage_props.c"
int main(int argc,char **argv) {
 if(argc!=3)return 2;
 FILE *geometry=fopen(argv[1],"rb"),*owners=fopen(argv[2],"rb");
 if(!geometry||!owners)return 3;
 Context c;memset(&c,0,sizeof c);c.stage=1;
 int bad=load_bank(&c.bank,geometry);
 if(!bad)bad=ownership(&c,owners);
 free_bank(&c.bank);free(c.ownership);fclose(geometry);fclose(owners);
 return bad?4:0;
}
'''
class PropTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='jgr-stage-props-')
        cls.work=Path(cls.temp.name);bridge=cls.work/'check.c';bridge.write_text(HARNESS)
        cls.exe=cls.work/'check'
        sources=['portable_stage','portable_stage_pack','portable_scratch','portable_disc','portable_models','portable_convert','portable_indexed']
        subprocess.run(['cc','-std=c99','-D_XOPEN_SOURCE=700','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'psp/src'),str(bridge),
            *(str(ROOT/'psp/src'/f'{s}.c') for s in sources),'-lm','-o',str(cls.exe)],check=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    @staticmethod
    def fnv(data):
        h=2166136261
        for b in data:h=((h^b)*16777619)&0xffffffff
        return h
    def fixture(self):
        geometry=bytearray(224)
        struct.pack_into('<4s11I',geometry,0,b'JGS1',0,1,1,3,3,80,80,112,128,208,224)
        struct.pack_into('<4f4I',geometry,80,0,0,0,1,0,1,0,0)
        struct.pack_into('<II4H',geometry,112,0,0,3,3,0,4)
        owners=bytearray(48)
        struct.pack_into('<4s7I',owners,0,b'JRS1',1,1,0,1,1,1,self.fnv(geometry[:128]))
        return geometry,owners
    def check(self,geometry,owners,good=False):
        a,b=self.work/'test.jgs',self.work/'test.jrs';a.write_bytes(geometry);b.write_bytes(owners)
        result=subprocess.run([str(self.exe),str(a),str(b)],capture_output=True,timeout=5)
        self.assertEqual(result.returncode,0 if good else 4,result.stderr.decode())
    def test_valid_streamed_metadata(self):self.check(*self.fixture(),good=True)
    def test_bad_ranges_counts_instance_flag_and_bounds(self):
        for offset,value,fmt in ((4,9000,'I'),(8,9000,'I'),(12,70000,'I'),(16,0x7fffffff,'I'),
                                 (24,81,'I'),(36,112,'I'),(44,208,'I'),(104,4,'I'),
                                 (96,1,'I'),(100,2,'I'),(112,1,'I'),(116,1,'I'),(92,float('nan'),'f')):
            with self.subTest(offset=offset,value=value):
                geometry,owners=self.fixture();struct.pack_into('<'+fmt,geometry,offset,value)
                self.check(geometry,owners)
    def test_wrong_ownership_fingerprint_shape_and_stage(self):
        for offset,value in ((4,2),(8,257),(12,1),(20,2),(24,2),(28,0)):
            with self.subTest(offset=offset):
                geometry,owners=self.fixture();struct.pack_into('<I',owners,offset,value)
                self.check(geometry,owners)
        geometry,owners=self.fixture();self.check(geometry,owners+b'\0')
        self.check(geometry[:100],owners)
    def test_psp_compile_with_asset_io_interposition(self):
        compiler=os.environ.get('PSP_CC') or shutil.which('psp-gcc')
        if not compiler:self.skipTest('Set PSP_CC to check the PSP/newlib compiler')
        subprocess.run([compiler,'-std=c99','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
            '-DJGR_PORTABLE_ASSETS=1','-include',str(ROOT/'psp/src/portable_assets.h'),
            '-I'+str(ROOT/'psp/src'),'-c',str(ROOT/'psp/src/portable_stage_props.c'),'-o',str(self.work/'props.o')],check=True)

if __name__=='__main__':unittest.main()
