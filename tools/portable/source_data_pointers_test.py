import ctypes as C
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
import source_data_pointers as p

ROOT=Path(__file__).resolve().parents[2]
CC=shutil.which(os.environ.get('PSP_CC', 'psp-gcc'))

class Section(C.Structure):
    _fields_=[('id',C.c_uint),('offset',C.c_uint),('bytes',C.c_uint),('alignment',C.c_uint),('destination',C.c_void_p)]

@unittest.skipUnless(CC,'Set PSP_CC or put psp-gcc on PATH for actual ABI proof')
class PointerData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='jgr-pointer-tests-');cls.dir=Path(cls.temp.name)
        # Actual C validation/copy/fixups with a synthetic 32-bit target address
        # space. macOS arm64 reserves the first4GiB, so native pointer values
        # cannot model a PSP address. Production uses the direct uintptr cast.
        bridge=cls.dir/'bridge.c'
        bridge.write_text('#include <stdint.h>\nstatic uintptr_t host_base;\nvoid test_base(void *p){host_base=(uintptr_t)p;}\nstatic uintptr_t test_address(const void *p){uintptr_t a=(uintptr_t)p;return a>=host_base && a-host_base<1048576 ? 0x20000000u+(a-host_base):a;}\n#define SOURCE_DATA_POINTER_ADDRESS(p) test_address(p)\n#include "source_data_pointers.c"\n')
        library=cls.dir/'loader.dylib'
        subprocess.run(['cc','-std=c99','-shared','-fPIC','-DJGR_PORTABLE_ASSETS=1','-I'+str(ROOT/'psp/src'),str(bridge),str(ROOT/'psp/src/source_data_io.c'),'-o',str(library)],check=True)
        cls.lib=C.CDLL(str(library));cls.apply=cls.lib.source_data_pointers_apply
        cls.apply.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Section),C.c_uint,C.c_uint,C.c_uint,C.c_uint];cls.apply.restype=C.c_int
        cls.load=cls.lib.source_data_pointers_load_file
        cls.load.argtypes=[C.c_char_p,C.POINTER(Section),C.c_uint,C.c_uint,C.c_uint,C.c_uint];cls.load.restype=C.c_int
        cls.buffer=C.create_string_buffer(1048576+16);cls.address=(C.addressof(cls.buffer)+15)&~15
        cls.lib.test_base.argtypes=[C.c_void_p];cls.lib.test_base(cls.address)
        cls.fixture=p.generate_group('static const unsigned N[]={17,42};\nstatic const char *const S[]={"hello","world"};\nstatic const unsigned *const Q[]={N+1};\n','',str(CC),'test')
    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()
    def setUp(self):
        C.memset(self.address,0xa5,1048576);self.result=self.fixture
        self.sections=(Section*len(self.result['layout']))(*[Section(**r,destination=self.address+i*4096) for i,r in enumerate(self.result['layout'])])
    def test_memory_view_preserves_fixups_and_rejects_invalid_span(self):
        self.assertEqual(self.run_image(), 0)
        expected = C.string_at(self.address, 1048576)
        C.memset(self.address, 0xa5, 1048576)
        image = C.create_string_buffer(self.result['blob']); length = [len(self.result['blob'])]
        callback_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.POINTER(C.c_void_p), C.POINTER(C.c_size_t))
        @callback_type
        def view(user, name, data, size):
            if name != b'memory-only.jsd': return -1
            data[0] = C.addressof(image); size[0] = length[0]; return 1
        self.lib.source_data_io_view_begin.argtypes = [callback_type, C.c_void_p]
        self.assertEqual(self.lib.source_data_io_view_begin(view, None), 0)
        args = (self.sections, len(self.sections), len(self.result['blob']), struct.unpack_from('<I', self.result['blob'], 16)[0], len(self.result['fixups']))
        try:
            self.assertEqual(self.load(b'memory-only.jsd', *args), 0)
            self.assertEqual(C.string_at(self.address, 1048576), expected)
            length[0] -= 1
            self.assertLess(self.load(b'memory-only.jsd', *args), 0)
            self.assertLess(self.load(b'missing.jsd', *args), 0)
            self.assertEqual(C.string_at(self.address, 1048576), expected)
        finally: self.lib.source_data_io_view_end()

    def run_image(self,blob=None,crc=None):
        blob=self.result['blob'] if blob is None else bytes(blob);image=C.create_string_buffer(blob)
        return self.apply(image,len(blob),self.sections,len(self.sections),len(self.result['blob']),struct.unpack_from('<I',blob,16)[0] if crc is None else crc,len(self.result['fixups']))
    def unchanged(self):self.assertEqual(C.string_at(self.address,1048576),b'\xa5'*1048576)
    def recalc(self,blob):
        h=struct.unpack_from('<I',blob,24)[0];struct.pack_into('<I',blob,16,zlib.crc32(blob[40:h]));struct.pack_into('<I',blob,20,zlib.crc32(blob[h:]));return blob
    def test_real_compiler_internal_array_and_string_pointers(self):
        self.assertEqual(self.run_image(),0)
        self.assertEqual(struct.unpack('<2I',C.string_at(self.address,8)),(17,42))
        s0,s1=struct.unpack('<2I',C.string_at(self.address+4096,8));self.assertEqual(C.string_at(self.address+s0-0x20000000),b'hello');self.assertEqual(C.string_at(self.address+s1-0x20000000),b'world')
        q=struct.unpack('<I',C.string_at(self.address+8192,4))[0];self.assertEqual(q,0x20000004);self.assertEqual(C.c_uint.from_address(self.address+q-0x20000000).value,42)
        self.assertNotIn('hello',self.result['source']['source_data_ptr_test.c']);self.assertNotIn('world',self.result['source']['source_data_ptr_test.inc'])
    def test_corruption_and_wrong_identity_do_not_write(self):
        b=bytearray(self.result['blob']);b[-1]^=1;self.assertLess(self.run_image(b),0);self.unchanged()
        self.assertLess(self.run_image(crc=0),0);self.unchanged()
        self.assertLess(self.run_image(self.result['blob'][:-1]),0);self.unchanged()
    def test_bad_source_target_alignment_and_duplicate_fixups_do_not_write(self):
        count=len(self.sections);base=40+16*count
        for field,value in [(0,count),(4,1),(8,count),(12,0xffffffff)]:
            b=bytearray(self.result['blob']);struct.pack_into('<I',b,base+field,value);self.recalc(b)
            self.assertLess(self.run_image(b),0);self.unchanged()
        b=bytearray(self.result['blob']);b[base+16:base+32]=b[base:base+16];self.recalc(b);self.assertLess(self.run_image(b),0);self.unchanged()
    def test_nonzero_pointer_placeholder_rejected(self):
        b=bytearray(self.result['blob']);a,o,_,_=self.result['fixups'][0];struct.pack_into('<I',b,self.result['layout'][a]['offset']+o,123);self.recalc(b)
        self.assertLess(self.run_image(b),0);self.unchanged()
    def test_destination_registry_alias_alignment_and_overflow(self):
        old=self.sections[1].destination
        for address in [self.address,self.address+1,0x100000000,0xfffffff0]:
            self.sections[1].destination=address
            if address==0xfffffff0:self.sections[1].bytes=32
            self.assertLess(self.run_image(),0);self.unchanged()
        self.sections[1].destination=old
    def test_file_length_is_exact(self):
        path=self.dir/'fixture.jsd';blob=self.result['blob'];path.write_bytes(blob+b'EXTRA')
        args=(bytes(path),self.sections,len(self.sections),len(blob),struct.unpack_from('<I',blob,16)[0],len(self.result['fixups']))
        self.assertLess(self.load(*args),0);self.unchanged();path.write_bytes(blob);self.assertEqual(self.load(*args),0)
    def test_external_and_function_relocations_rejected(self):
        for text,pre in [('static const unsigned *const Q[]={outside};','extern const unsigned outside[];'),('static const void *const Q[]={(void*)outside};','extern void outside(void);')]:
            with self.assertRaisesRegex(ValueError,'External/function'):p.generate_group(text,pre,str(CC),'reject')
    def test_pointers_outside_array_rejected(self):
        with self.assertRaisesRegex(ValueError,'exceeds'):p.generate_group('static const unsigned N[]={1};\nstatic const unsigned *const Q[]={N+2};','',str(CC),'reject')
    def test_scalar_anonymous_record_and_lexical_string_braces(self):
        r=p.generate_group('static const int scalar=5;\nstatic const struct {int n;float f;} A[]={{3,2.f}};\nstatic const char text[]="{; }";','',str(CC),'syntax')
        self.assertEqual(len(r['layout']),3);self.assertEqual(r['fixups'],[]);self.assertIn('extern int scalar',r['source']['source_data_ptr_syntax.inc'])
    def test_all_current_groups_compiler_proof(self):
        recipes=json.loads(p.RECIPES.read_text());base=ROOT/'build/portable/pointer-inputs'
        if not base.is_dir():self.skipTest('Private baseline source inputs absent')
        for row in recipes['groups']:
            with self.subTest(row['name']):
                r=p.generate_group((base/row['input']).read_text(),row['preamble'],str(CC),row['name'])
                blob=r['blob'];self.assertEqual(blob[:8],b'JGSD\x02\0\0\0');self.assertTrue(r['layout'])
                for _,offset,_,_ in r['fixups']:self.assertEqual(offset%4,0)

if __name__=='__main__':unittest.main()
