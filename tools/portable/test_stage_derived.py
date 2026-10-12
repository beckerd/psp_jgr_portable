"""Native derived sidecar checks; real converted JGS inputs are opt-in fixtures.

JGR_TEST_ASSETS must contain the existing reviewed JGS/JRS reference banks.
This test does not claim the native base-stage provider generated those inputs.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import shutil
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
HARNESS=r'''
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_stage_derived.h"
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
    if(argc!=5)return 2;unsigned stage=(unsigned)strtoul(argv[3],NULL,10);
    JgrDisc *disc=NULL;char error[192];
    if(!strcmp(argv[4],"mips") && jgr_disc_open(&disc,argv[1],error,sizeof error)){fprintf(stderr,"%s\n",error);return 3;}
    FILE *jgs=fopen(argv[2],"rb");if(!jgs){jgr_disc_close(disc);return 4;}
    int result;
    if(!strcmp(argv[4],"mips"))result=jgr_stage_mips(disc,jgs,stage,stdout);
    else {char path[2048];snprintf(path,sizeof path,"%s",argv[2]);size_t n=strlen(path);if(n<4)return 6;memcpy(path+n-3,"jrs",4);FILE *jrs=fopen(path,"rb");if(!jrs)return 7;result=jgr_stage_visibility(jgs,jrs,stage,stdout);fclose(jrs);}
    fclose(jgs);jgr_disc_close(disc);return result ? 5:fflush(stdout)!=0;
}
'''
class DerivedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='jgr-native-derived-')
        cls.work=Path(cls.temp.name);harness=cls.work/'harness.c';harness.write_text(HARNESS)
        cls.exe=cls.work/'derived'
        subprocess.run(['cc','-std=c99','-D_XOPEN_SOURCE=700','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror',
            '-I'+str(ROOT/'psp/src'),str(harness),str(ROOT/'psp/src/portable_stage_derived.c'),
            str(ROOT/'psp/src/portable_disc.c'),str(ROOT/'psp/src/portable_indexed.c'),str(ROOT/'psp/src/portable_convert.c'),'-lm','-o',str(cls.exe)],check=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    @staticmethod
    def fnv(data):
        value=2166136261
        for b in data:value=((value^b)*16777619)&0xffffffff
        return value
    def fixture(self,triangles,alpha=False,moving=False,alias=False):
        objects=2 if alias else 1
        vertices=[p for triangle in triangles for p in triangle]
        ot,oo,ob=80,96,96+objects*32
        ov=ob+objects*16;oi=ov+len(vertices)*24;op=(oi+len(vertices)*2+15)&~15
        data=bytearray(op+1152)
        struct.pack_into('<4s11I',data,0,b'JGS1',1,objects,objects,len(vertices),len(vertices),ot,oo,ob,ov,oi,op)
        struct.pack_into('<4H2I',data,ot,16,8,0,0,op,op+1024)
        for i in range(objects):
            struct.pack_into('<4f4I',data,oo+i*32,50,0,50,80,i,1,2 if i else 0,1 if moving or alias else 0)
            struct.pack_into('<II4H',data,ob+i*16,0,0,len(vertices),len(vertices),0,int(alpha))
        for i,p in enumerate(vertices):
            struct.pack_into('<2fI3f',data,ov+i*24,0,0,0xffffffff,*p)
            struct.pack_into('<H',data,oi+i*2,i)
        records=1 if moving or alias else 0
        jrs=bytearray(32+16+32*records)
        struct.pack_into('<4s7I',jrs,0,b'JRS1',1,1,records,1,objects,objects,self.fnv(data[:ov]))
        if records:
            struct.pack_into('<4H3fHHfI',jrs,48,0,1,1,int(alias),0,0,0,0,9 if moving else 1,160,0)
        source=self.work/'synthetic.jgs';source.write_bytes(data);source.with_suffix('.jrs').write_bytes(jrs)
        return source
    def visibility(self,path,good=True):
        result=subprocess.run([str(self.exe),'unused',str(path),'1','visibility'],capture_output=True,timeout=5)
        if good:self.assertEqual(result.returncode,0,result.stderr.decode())
        else:
            self.assertEqual(result.returncode,5,result.stderr.decode())
            self.assertEqual(result.stdout,b'')
        return result.stdout
    def test_visibility_only_certifies_closed_opaque_immutable_coverage(self):
        quad=[((0,0,0),(100,0,0),(100,0,100)),((0,0,0),(100,0,100),(0,0,100))]
        data=self.visibility(self.fixture(quad))
        self.assertEqual(struct.unpack_from('<I',data,8)[0],1)
        record=struct.unpack_from('<19fHH',data,36+24)
        self.assertEqual(record[-2:],(0,4))
        self.assertTrue(all(2<=record[i]<=98 for i in (0,2,3,5,6,8,9,11)))
        for kwargs in ({'alpha':True},{'moving':True}):
            data=self.visibility(self.fixture(quad,**kwargs))
            self.assertEqual(struct.unpack_from('<I',data,8)[0],0)
        # Two independent islands in a plane must not create a blocker over
        # the unrendered gap between them.
        islands=quad+[tuple((x+300,y,z) for x,y,z in tri) for tri in quad]
        self.assertEqual(struct.unpack_from('<I',self.visibility(self.fixture(islands)),8)[0],0)
        # Repeated overlapping triangles are non-manifold, never a blocker.
        self.assertEqual(struct.unpack_from('<I',self.visibility(self.fixture(quad+quad)),8)[0],0)
    def test_verified_lod_pair_shares_one_blocker(self):
        quad=[((0,0,0),(100,0,0),(100,0,100)),((0,0,0),(100,0,100),(0,0,100))]
        data=self.visibility(self.fixture(quad,alias=True))
        self.assertEqual(struct.unpack_from('<I',data,8)[0],1)
        self.assertEqual(struct.unpack_from('<HH',data,36+48+76),(0,4|(2<<3)))
    def test_bad_ownership_and_geometry_rejected(self):
        tri=[((0,0,0),(100,0,0),(100,0,100))]
        source=self.fixture(tri,moving=True)
        jrs=bytearray(source.with_suffix('.jrs').read_bytes());jrs[28]^=1;source.with_suffix('.jrs').write_bytes(jrs)
        self.visibility(source,False)
        source=self.fixture(tri);data=bytearray(source.read_bytes());at=struct.unpack_from('<I',data,40)[0]
        struct.pack_into('<H',data,at,999);source.write_bytes(data);self.visibility(source,False)
        source=self.fixture(tri);data=bytearray(source.read_bytes());at=struct.unpack_from('<I',data,36)[0]
        struct.pack_into('<I',data,at+12,0x7f800000);source.write_bytes(data);self.visibility(source,False)
    def test_psp_compile(self):
        compiler=os.environ.get('PSP_CC') or shutil.which('psp-gcc')
        if not compiler:self.skipTest('PSP compiler not configured')
        subprocess.run([compiler,'-O2','-std=gnu99','-Wall','-Wextra','-Werror','-ffp-contract=off',
            '-DJGR_PORTABLE_ASSETS=1','-include',str(ROOT/'psp/src/portable_assets.h'),
            '-c',str(ROOT/'psp/src/portable_stage_derived.c'),'-o',str(self.work/'derived.o')],check=True)
    @unittest.skipUnless(os.environ.get('JGR_TEST_GDI') and os.environ.get('JGR_TEST_ASSETS'),'Original disc and reviewed JGS fixtures not configured')
    def test_authored_mips_match_catalog_from_reference_jgs(self):
        assets=Path(os.environ['JGR_TEST_ASSETS'])
        catalog={r['path']:r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']}
        for stage in (1,2,3,5,6):
            with self.subTest(stage=stage):
                result=subprocess.run([str(self.exe),os.environ['JGR_TEST_GDI'],str(assets/f'stage{stage}.jgs'),str(stage),'mips'],capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr.decode())
                expected=catalog[f'stage{stage}.jgm']
                self.assertEqual(len(result.stdout),expected['bytes'])
                self.assertEqual(hashlib.sha256(result.stdout).hexdigest(),expected['sha256'])
    @unittest.skipUnless(os.environ.get('JGR_TEST_GDI') and os.environ.get('JGR_TEST_ASSETS'),'Original disc and reviewed JGS fixtures not configured')
    def test_visibility_matches_catalog_from_reference_jgs_jrs(self):
        assets=Path(os.environ['JGR_TEST_ASSETS'])
        catalog={r['path']:r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']}
        for stage in (1,2,3,5,6):
            with self.subTest(stage=stage):
                result=subprocess.run([str(self.exe),os.environ['JGR_TEST_GDI'],str(assets/f'stage{stage}.jgs'),str(stage),'visibility'],capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr.decode())
                expected=catalog[f'stage{stage}.jvo']
                if os.environ.get('JGR_DERIVED_DEBUG'):
                    Path(os.environ['JGR_DERIVED_DEBUG'],f'stage{stage}.jvo').write_bytes(result.stdout)
                self.assertEqual(len(result.stdout),expected['bytes'])
                self.assertEqual(hashlib.sha256(result.stdout).hexdigest(),expected['sha256'])
if __name__=='__main__':unittest.main()
