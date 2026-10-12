"""Native mission serializers; optional complete user-disc catalog parity.

Set JGR_TEST_GDI, JGR_TEST_ASSETS and optionally JGR_MISSION_REPORT to run the
private 575-file comparison. The synthetic checks require no original inputs.
"""
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from semantic import cans_digest

ROOT = Path(__file__).resolve().parents[2]
KINDS = {name: index + 10 for index, name in enumerate(
    ('ctrl', 'graph', 'groups', 'obj', 'peds', 'police', 'rivals', 'waves', 'cans'))}


class File(C.Structure):
    _fields_ = [('offset', C.c_uint64), ('size', C.c_uint32)]


class MissionConvertTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='jgr-mission-test-')
        folder = Path(cls.build.name)
        bridge = folder / 'bridge.c'
        bridge.write_text('''#include "portable_mission_convert.h"
int convert_path(JgrDisc *disc,const JgrDiscFile *source,unsigned kind,unsigned sectors,const char *path){
 FILE *out=fopen(path,"wb");if(!out)return -1;
 int result=jgr_mission_convert(disc,source,kind,sectors,out);
 if(fclose(out))result=-1;return result;
}
''')
        stub = folder / 'stub.c'
        stub.write_text('''#include "portable_disc.h"
#include <string.h>
static const unsigned char *bank;static unsigned length;
void set_bank(const void *p,unsigned n){bank=p;length=n;}
int jgr_disc_read(JgrDisc *disc,const JgrDiscFile *file,uint32_t at,void *out,size_t size){
 (void)disc;(void)file;if(at>length || size>length-at)return -1;memcpy(out,bank+at,size);return 0;
}
int jgr_disc_find(JgrDisc *disc,const char *name,JgrDiscFile *out){(void)disc;(void)name;(void)out;return -1;}
''')
        common = ['cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                  '-shared', '-fPIC', '-I' + str(ROOT / 'psp/src'), str(bridge),
                  str(ROOT / 'psp/src/portable_mission_convert.c')]
        synthetic = folder / 'synthetic.so'
        subprocess.run(common + [str(stub), '-o', str(synthetic)], check=True)
        cls.synthetic = C.CDLL(str(synthetic))
        cls.synthetic.set_bank.argtypes = [C.c_void_p, C.c_uint]
        cls.synthetic.convert_path.argtypes = [C.c_void_p, C.POINTER(File), C.c_uint, C.c_uint, C.c_char_p]
        native = folder / 'native.so'
        subprocess.run(common + [str(ROOT / 'psp/src/portable_disc.c'), '-o', str(native)], check=True)
        cls.native = C.CDLL(str(native))
        cls.native.jgr_disc_open.argtypes = [C.POINTER(C.c_void_p), C.c_char_p, C.c_char_p, C.c_size_t]
        cls.native.jgr_disc_source.argtypes = [C.c_void_p, C.c_char_p, C.POINTER(File)]
        cls.native.jgr_disc_close.argtypes = [C.c_void_p]
        cls.native.convert_path.argtypes = cls.synthetic.convert_path.argtypes

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='jgr-mission-case-')
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name) / 'out.bin'

    def convert(self, bank, kind, sectors=0):
        memory = C.create_string_buffer(bytes(bank))
        self.synthetic.set_bank(memory, len(bank))
        result = self.synthetic.convert_path(C.c_void_p(1), C.byref(File(0, len(bank))),
                                             KINDS[kind], sectors, str(self.output).encode())
        return result, self.output.read_bytes()

    def test_legacy_empty_chained_families(self):
        bank = b'JMSb' + bytes(124)
        for kind, expected in [('ctrl', b'CTL2' + bytes(4)), ('obj', b'SOB1' + bytes(4)),
                               ('cans', b'CAN3' + bytes(8))]:
            with self.subTest(kind=kind):
                self.assertEqual(self.convert(bank, kind), (0, expected))

    def test_cycles_and_outside_chain_links_rejected(self):
        for next_address in (0x8cde6000, 0x8cde7000, 0x8cde6001):
            bank = bytearray(128);bank[:4] = b'JMSc'
            struct.pack_into('<I', bank, 0x50, next_address)
            for kind in ('ctrl', 'obj', 'cans'):
                with self.subTest(address=next_address, kind=kind):
                    self.assertLess(self.convert(bank, kind, 46)[0], 0)

    def test_can_source_identity_and_coordinates_preserved(self):
        bank = bytearray(256);bank[:4] = b'JMSc';base = 0x8cde6000
        struct.pack_into('<I', bank, 0x38, base + 0x60)
        struct.pack_into('<I', bank, 0x60, base + 0x80)
        struct.pack_into('<I', bank, 0x80, base + 0xa0)
        struct.pack_into('<3fI', bank, 0xa0, 1.25, -3.0, 7.5, 1)
        expected = struct.pack('<4sIIIII3fI', b'CAN3', 1, 1, 0, 0, base + 0xa0, 1.25, -3., 7.5, 1)
        self.assertEqual(self.convert(bank, 'cans'), (0, expected))
        struct.pack_into('<I', bank, 0xac, 5)
        self.assertLess(self.convert(bank, 'cans')[0], 0)

    def test_invalid_sector_count_and_source_bound_rejected(self):
        bank = b'JMSc' + bytes(124)
        self.assertLess(self.convert(bank, 'obj', 129)[0], 0)
        self.assertLess(self.convert(bank[:20], 'ctrl')[0], 0)

    def test_police_source_float_sentinels_use_ieee_classification(self):
        base = 0x8cde6000
        values = {
            0x00000000: True, 0x80000000: True,  # Both zero signs.
            0x00000001: True, 0x80000001: False,  # Subnormals.
            0x3f800000: True, 0xbf800000: False,
            0x7f7fffff: True, 0xff7fffff: False,
            0x7f800000: True, 0xff800000: False,  # Infinities.
            0x7fc00001: False, 0xffc00001: False,  # Quiet NaNs.
            0x7f800001: False, 0xff800001: False,  # Signalling NaNs.
            0xffff9873: False,  # Original set1 police sentinel.
        }
        for bits, accepted in values.items():
            bank = bytearray(1024); bank[:4] = b'JMSc'
            struct.pack_into('<I', bank, 0x24, base + 0x80)
            struct.pack_into('<2I', bank, 0x80, base + 0xa0, 0xffffffff)
            struct.pack_into('<3I', bank, 0xa0, base + 0x100, base + 0x200, base + 0x300)
            struct.pack_into('<I', bank, 0x100 + 60, bits)
            struct.pack_into('<I', bank, 0x100 + 68, base + 0x380)
            struct.pack_into('<I', bank, 0x100 + 72 + 60, 0xbf800000)
            struct.pack_into('<h', bank, 0x200 + 64, -1)
            bank[0x300 + 24] = 255
            expected = struct.pack('<4s6I', b'POL1', 1, int(accepted), 0, 0, 0, 0)
            if accepted:
                expected += bank[0x100:0x140] + bytes(8)
            with self.subTest(bits=hex(bits)):
                self.assertEqual(self.convert(bank, 'police'), (0, expected))

    def test_object_coordinates_reject_all_nonfinite_source_words(self):
        base = 0x8cde6000
        finite_words = (0, 0x80000000, 1, 0x80000001, 0x3f800000, 0xbf800000,
                        0x7f7fffff, 0xff7fffff)
        nonfinite_words = (0x7f800000, 0xff800000, 0x7fc00001, 0xffc00001,
                           0x7f800001, 0xff800001, 0xffff9873)
        for axis in range(3):
            for bits in finite_words + nonfinite_words:
                bank = bytearray(512); bank[:4] = b'JMSc'
                struct.pack_into('<I', bank, 0x34, base + 0x80)
                struct.pack_into('<I', bank, 0x80, base + 0xa0)
                struct.pack_into('<I', bank, 0xa0, base + 0xc0)
                struct.pack_into('<I', bank, 0xc4 + axis * 4, bits)
                with self.subTest(axis=axis, bits=hex(bits)):
                    result, output = self.convert(bank, 'obj', 1)
                    if bits in nonfinite_words:
                        self.assertLess(result, 0)
                    else:
                        expected = struct.pack('<4s3I', b'SOB1', 1, 1, 0)
                        expected += bytes(4) + bank[0xc4:0xd0] + bytes(4)
                        self.assertEqual((result, output), (0, expected))

    @unittest.skipUnless(os.environ.get('JGR_TEST_GDI') and os.environ.get('JGR_TEST_ASSETS'),
                         'Private supported disc and verified assets required')
    def test_all_catalog_mission_outputs_from_native_disc_reader(self):
        disc = C.c_void_p();error = C.create_string_buffer(256)
        self.assertEqual(self.native.jgr_disc_open(C.byref(disc), os.environ['JGR_TEST_GDI'].encode(), error, len(error)),
                         0, error.value.decode())
        rows = [];failures = []
        try:
            recipes = json.loads((ROOT / 'tools/portable/mission_sets.json').read_text())['files']
            expected = Path(os.environ['JGR_TEST_ASSETS'])
            for name, recipe in sorted(recipes.items()):
                kind = name.rsplit('_', 1)[1].split('.')[0]
                source = File()
                self.assertEqual(self.native.jgr_disc_source(disc, recipe['source'].encode(), C.byref(source)), 0, name)
                sectors = int(recipe['arguments'][1]) if recipe['arguments'] else 0
                result = self.native.convert_path(disc, C.byref(source), KINDS[kind], sectors, str(self.output).encode())
                actual, want = self.output.read_bytes(), (expected / name).read_bytes()
                exact = actual == want
                semantic = kind == 'cans' and result == 0 and cans_digest(actual) == cans_digest(want)
                row = dict(path=name, kind=kind, status=result, bytes=len(actual), expected_bytes=len(want),
                           exact=exact, semantic_equivalent=semantic,
                           sha256=hashlib.sha256(actual).hexdigest(), expected_sha256=hashlib.sha256(want).hexdigest())
                if result or not (exact or semantic):
                    row['first_difference'] = next((i for i, (a, b) in enumerate(zip(actual, want)) if a != b), None)
                    failures.append(row)
                rows.append(row)
        finally:
            self.native.jgr_disc_close(disc)
        report = {'files': rows, 'failed': failures, 'count': len(rows),
                  'exact': sum(row['exact'] for row in rows),
                  'semantic_only': sum(not row['exact'] and row['semantic_equivalent'] for row in rows)}
        if os.environ.get('JGR_MISSION_REPORT'):
            path = Path(os.environ['JGR_MISSION_REPORT']);path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(report, indent=2) + '\n')
        self.assertEqual(failures, [], json.dumps(failures[:12], indent=2))


if __name__ == '__main__':
    unittest.main()
