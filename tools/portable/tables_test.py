"""Native table serializer parity, metadata provenance and bounded-input tests."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

from tables_recipes import ROOT, emit, recipes
sys.path.insert(0, str(ROOT / 'tools/portable/tests'))
from test_native_disc import fixture, both32, record


HARNESS = r'''
#include "portable_tables.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    JgrDisc *disc=NULL;char error[192];int result;
    if(argc!=4)return 2;
    if(!jgr_convert_table_known(argv[2]))return 3;
    result=!strcmp(argv[3],"fixture") ? jgr_disc_open_fixture(&disc,argv[1],error,sizeof error) :
                                         jgr_disc_open(&disc,argv[1],error,sizeof error);
    if(result){fprintf(stderr,"%s\n",error);return 4;}
    result=jgr_convert_table(disc,argv[2],stdout);
    if(result)fprintf(stderr,"Conversion failed: %s\n",jgr_disc_error(disc));
    jgr_disc_close(disc);return result ? 5 : fflush(stdout)!=0;
}
'''


class TablesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-native-tables-')
        folder = Path(cls.temp.name)
        harness = folder / 'harness.c'
        harness.write_text(HARNESS)
        cls.exe = folder / 'tables'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-D_XOPEN_SOURCE=700',
                        '-Wall', '-Wextra', '-Werror', '-DJGR_DISC_TESTING', '-DJGR_PORTABLE_ASSETS=1',
                        '-include', str(ROOT / 'psp/src/portable_assets.h'), '-I', str(ROOT / 'psp/src'),
                        str(harness), str(ROOT / 'psp/src/portable_disc.c'),
                        str(ROOT / 'psp/src/portable_tables.c'), str(ROOT / 'psp/src/portable_convert.c'),
                        '-lm', '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_generated_metadata_is_current_and_has_no_literals(self):
        self.assertEqual(emit(), (ROOT / 'psp/src/portable_tables_data.inc').read_text())
        entries = recipes()
        self.assertEqual(len(entries), 137)
        self.assertEqual(len({r['output'] for r in entries}), len(entries))
        for row in entries:
            self.assertTrue(row['source'].startswith('decompile/'))
            for field in row['fields']:
                self.assertEqual(set(field), {'address', 'add', 'destination', 'kind', 'output_kind', 'flags', 'precision'})
                if not field['flags'] & 2:
                    self.assertGreaterEqual(field['address'], row['base'])

    def image(self, folder, writes):
        gdi, _ = fixture(folder)
        track = folder / 'track data.bin'
        data = bytearray(track.read_bytes())
        length = 3075648
        both32(data, 18 * 2048 + 68 + 10, length)
        data.extend(bytes(((20 * 2048 + length + 2047) // 2048) * 2048 - len(data)))
        for address, fmt, values in writes:
            struct.pack_into('<' + fmt, data, 20 * 2048 + address - 0x8c010000, *values)
        track.write_bytes(data)
        return gdi

    def run_table(self, gdi, name, good=True):
        result = subprocess.run([str(self.exe), str(gdi), name, 'fixture'], capture_output=True)
        if good:
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            data = result.stdout
            magic, version, count, length, directory_crc, payload_crc, header, reserved = struct.unpack_from('<4s7I', data)
            self.assertEqual((magic, version, length, reserved), (b'JGSD', 1, len(data), 0))
            self.assertEqual(directory_crc, zlib.crc32(data[32:header]))
            self.assertEqual(payload_crc, zlib.crc32(data[header:]))
            return data
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b'')

    def test_source_floats_round_using_reviewed_decimal_format(self):
        name = 'source_data_mixed_enemycombat_c__keikan_0.jsd'
        values = (7., .123456789, -.654321987, 9., 1.23456789, .987654321)
        with tempfile.TemporaryDirectory() as tmp:
            gdi = self.image(Path(tmp), [(0x8c1b6f28, '6f', values)])
            data = self.run_table(gdi, name)
            source = struct.unpack('<6f', struct.pack('<6f', *values))
            want = struct.pack('<6f', source[0], float(format(source[1], '.9f')), float(format(source[2], '.9f')),
                               source[3], float(format(source[4], '.9f')), float(format(source[5], '.4f')))
            self.assertEqual(data[48:], want)

    def test_pointer_formation_zero_fill_and_out_of_bounds(self):
        name = 'source_data_mixed_gang_c__para_ground_0.jsd'
        writes = []
        for row in range(5):
            pointer = 0x8c200000 + row * 128
            writes.append((0x8c1b8160 + row * 4, 'I', [pointer]))
            writes.append((pointer, '%df' % ((row + 1) * 3), list(range((row + 1) * 3))))
        with tempfile.TemporaryDirectory() as tmp:
            gdi = self.image(Path(tmp), writes)
            data = self.run_table(gdi, name)
            expected = []
            for row in range(5):
                expected.extend(list(range((row + 1) * 3)) + [0] * ((4 - row) * 3))
            self.assertEqual(data[48:], struct.pack('<75f', *expected))
        with tempfile.TemporaryDirectory() as tmp:
            gdi = self.image(Path(tmp), [(0x8c1b8160, 'I', [0xffffffff])])
            self.run_table(gdi, name, good=False)

    def test_nonfinite_source_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            gdi = self.image(Path(tmp), [(0x8c1b6f28, 'I', [0x7f800000])])
            self.run_table(gdi, 'source_data_mixed_enemycombat_c__keikan_0.jsd', good=False)

    def test_unknown_output_rejected(self):
        result = subprocess.run([str(self.exe), 'missing.gdi', '../unknown.jsd', 'fixture'], capture_output=True)
        self.assertEqual(result.returncode, 3)
        self.assertEqual(result.stdout, b'')

    def test_player_body_uses_character_descriptor_not_last_directory(self):
        # Two valid section-5 trees have deliberately different bind positions.
        # The normal character pointer must win even if the spray tree is last.
        metadata = (ROOT / 'psp/src/portable_tables_data.inc').read_text()
        sources = re.findall(r'"decompile/afs/PLAYER/(\d+)_(.*?)"', metadata.split('};', 1)[0])
        self.assertEqual(len(sources), 16)
        archive = bytearray(4096 + 33 * 4096)
        archive[:4] = b'AFS\0'
        struct.pack_into('<I', archive, 4, 33)
        struct.pack_into('<II', archive, 8 + 33 * 8, 2048, 33 * 48)
        expected = bytearray()
        for model, (index, name) in enumerate(sources):
            index = int(index);start = 4096 + index * 4096
            struct.pack_into('<II', archive, 8 + index * 8, start, 4096)
            encoded = name.encode();archive[2048+index*48:2048+index*48+len(encoded)] = encoded
            play = memoryview(archive)[start:start+4096]
            play[:4] = b'PLAY';struct.pack_into('<II', play, 4, 0x20, 0x80)
            struct.pack_into('<4I', play, 0x20, 5, 0x400, 5, 0xa00)
            struct.pack_into('<II', play, 0x80+0x6c, 0x400, 0xa00)
            for root, marker in ((0x400, 1.), (0xa00, 99.)):
                for bone in range(20):
                    address = root + bone * 68
                    struct.pack_into('<3f', play, address+12, model, bone, marker)
                    struct.pack_into('<3f', play, address+36, 1, 1, 1)
                    struct.pack_into('<I', play, address+60, address+68 if bone<19 else 0)
                    if root == 0x400:
                        expected.extend(struct.pack('<ii4f3f3i3f', bone-1, 0, 0, 0, 0, 0,
                                                    model, bone, marker, 0, 0, 0, 1, 1, 1))
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);gdi,_=fixture(folder)
            track=folder/'track data.bin';image=bytearray(track.read_bytes())
            entries=record(b'\0',45019,2048,2)+record(b'\1',45018,2048,2)+record(b'PLAYER.AFS;1',45100,len(archive))
            image[19*2048:20*2048]=bytes(2048)
            image[19*2048:19*2048+len(entries)]=entries
            image.extend(archive);track.write_bytes(image)
            name='source_data_ptr_player_body.jsd'
            result=subprocess.run([str(self.exe),str(gdi),name,'fixture'],capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr.decode())
            self.assertEqual(struct.unpack_from('<II',result.stdout,4),(2,1))
            offset,size=struct.unpack_from('<II',result.stdout,44)
            self.assertEqual(result.stdout[offset:offset+size],expected)
            # A corrupt character pointer must fail, without falling back to
            # the otherwise-valid directory's spray tree or writing a blob.
            first=100*2048+4096
            struct.pack_into('<I',image,first+8,0xfffffff0);track.write_bytes(image)
            self.run_table(gdi,name,good=False)

    def test_psp_compilation(self):
        compiler = os.environ.get('PSP_CC') or shutil.which('psp-gcc')
        if not compiler:
            self.skipTest('PSP compiler not configured')
        subprocess.run([compiler, '-std=gnu99', '-Os', '-Wall', '-Wextra', '-Werror', '-D__PSP__',
                        '-DJGR_PORTABLE_ASSETS=1', '-include', str(ROOT / 'psp/src/portable_assets.h'),
                        '-c', str(ROOT / 'psp/src/portable_tables.c'),
                        '-o', str(Path(self.temp.name) / 'tables.o')], check=True)

    @unittest.skipUnless(os.environ.get('JGR_TEST_GDI'), 'Supported user disc not configured')
    def test_every_real_disc_table_matches_runtime_catalog(self):
        catalog = {r['path']: r for r in json.loads((ROOT / 'tools/portable/asset_catalog.json').read_text())['files']}
        for row in recipes():
            if row['special'] >= 63:
                continue  # Verified by test_tables_cached against generated geometry.
            with self.subTest(output=row['output']):
                result = subprocess.run([str(self.exe), os.environ['JGR_TEST_GDI'], row['output'], 'strict'], capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                self.assertEqual(len(result.stdout), catalog[row['output']]['bytes'])
                self.assertEqual(hashlib.sha256(result.stdout).hexdigest(), catalog[row['output']]['sha256'])


if __name__ == '__main__':
    unittest.main()
