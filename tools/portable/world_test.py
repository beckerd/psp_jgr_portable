"""Original-disc parity and bounded native world serializer checks."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
import unittest
from world_recipes import ROOT, emit, recipes
sys.path.insert(0, str(ROOT / "tools/portable/tests"))
from test_native_disc import fixture, record

HARNESS = r'''
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_world_convert.h"
#include <string.h>
int main(int argc,char **argv) {
    JgrDisc *disc=NULL;char error[192];int result;unsigned char buffer[4096];size_t n;
    if(argc!=4)return 2;
    if(strcmp(argv[2],"native-stage1") && !jgr_world_convert_known(argv[2]))return 3;
    if((!strcmp(argv[3],"fixture") ? jgr_disc_open_fixture(&disc,argv[1],error,sizeof error):jgr_disc_open(&disc,argv[1],error,sizeof error))){fprintf(stderr,"%s\n",error);return 4;}
    FILE *output=tmpfile();if(!output)return 6;
    result=!strcmp(argv[2],"native-stage1") ? jgr_world_native_collision(disc,1,output):jgr_world_convert(disc,argv[2],output);
    if(result)fprintf(stderr,"Conversion failed: %s\n",jgr_disc_error(disc));
    jgr_disc_close(disc);
    if(!result) {
        if(fseek(output,0,SEEK_SET))return 7;
        while((n=fread(buffer,1,sizeof buffer,output)))if(fwrite(buffer,1,n,stdout)!=n)return 8;
        if(ferror(output))return 9;
    }
    fclose(output);return result ? 5:fflush(stdout)!=0;
}
'''


class WorldTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-native-world-')
        folder = Path(cls.temp.name)
        harness = folder / 'harness.c'
        harness.write_text(HARNESS)
        cls.exe = folder / 'world'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-D_XOPEN_SOURCE=700', '-O2',
                        '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-DJGR_DISC_TESTING', '-DJGR_PORTABLE_ASSETS=1',
                        '-include', str(ROOT / 'psp/src/portable_assets.h'), '-I', str(ROOT / 'psp/src'),
                        str(harness), str(ROOT / 'psp/src/portable_disc.c'),
                        str(ROOT / 'psp/src/portable_world_convert.c'),
                        str(ROOT / 'psp/src/portable_mission_convert.c'), '-lm', '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_metadata_is_current(self):
        self.assertEqual(emit(), (ROOT / 'psp/src/portable_world_data.inc').read_text())
        self.assertEqual(len(recipes()), 39)
        for name, (op, source, params) in recipes().items():
            self.assertNotIn('/', name)
            self.assertEqual(op, 50)
            self.assertEqual(params, (0, 0, 0, 0))
            self.assertEqual(source, 'decompile/extracted/1ST_READ.BIN')

    def test_unknown_name_rejected(self):
        for name in ('../zones1.bin', 'zones0.bin', 'worldaudio1.bin/evil', 'Missions.bin'):
            result = subprocess.run([str(self.exe), 'missing.gdi', name, 'strict'], capture_output=True)
            self.assertEqual(result.returncode, 3)
            self.assertEqual(result.stdout, b'')

    def synthetic_disc(self, folder, exe_writes=(), stage_writes=(), mission_link=None):
        gdi, _ = fixture(folder)
        data = bytearray((folder / 'track data.bin').read_bytes()[:20 * 2048])
        exe = bytearray(0x200000)
        stage = bytearray(max([4096]+[address-0x8cb00000+len(values)*4 for address,values in stage_writes]))
        for address, values in exe_writes:
            struct.pack_into('<%dI' % len(values), exe, address - 0x8c010000, *values)
        for address, values in stage_writes:
            struct.pack_into('<%dI' % len(values), stage, address - 0x8cb00000, *values)
        def afs(name, payload):
            archive = bytearray(2048 + len(payload))
            archive[:4] = b'AFS\0'
            struct.pack_into('<5I', archive, 4, 1, 2048, len(payload), 32, 48)
            archive[32:32 + len(name)] = name.encode()
            archive[2048:] = payload
            return archive
        def add(payload):
            sector = len(data) // 2048
            data.extend(payload)
            data.extend(bytes(-len(data) % 2048))
            return 45000 + sector, len(payload)
        exe_at = add(exe)
        stage_at = add(afs('st1dat.bin', stage))
        root = record(b'\0', 45018, 2048, 2) + record(b'\1', 45018, 2048, 2)
        root += record(b'1ST_READ.BIN;1', *exe_at) + record(b'JETRADIO', 45019, 2048, 2)
        directory = record(b'\0', 45019, 2048, 2) + record(b'\1', 45018, 2048, 2)
        directory += record(b'STAGE1.AFS;1', *stage_at)
        if mission_link is not None:
            mission = bytearray(0xf0)
            mission[:4] = b'JMDc'
            struct.pack_into('<I', mission, 0xec, mission_link)
            at = add(afs('mission_data_s1m01.bin', mission))
            directory += record(b'E_MISSIONDATA.AFS;1', *at) + record(b'MISSIONDATA.AFS;1', *at)
        data[18 * 2048:20 * 2048] = bytes(4096)
        data[18 * 2048:18 * 2048 + len(root)] = root
        data[19 * 2048:19 * 2048 + len(directory)] = directory
        (folder / 'track data.bin').write_bytes(data)
        return gdi

    def reject_fixture(self, gdi, name):
        result = subprocess.run([str(self.exe), str(gdi), name, 'fixture'], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 5, result.stderr.decode())
        self.assertEqual(result.stdout, b'')

    def test_zone_count_volume_extent_and_grip_bounds(self):
        for stage_row in ((0x8cb00020, 4097), (0x8cb00020, 1)):
            with tempfile.TemporaryDirectory() as tmp:
                gdi = self.synthetic_disc(Path(tmp), [(0x8c105e3c, [0x8cb00000])],
                    [(0x8cb00000, stage_row), (0x8cb00020 + 24, [0x8cb00ff0, 1, 0, 0, 0, 0])])
                self.reject_fixture(gdi, 'zones1.bin')
        with tempfile.TemporaryDirectory() as tmp:
            gdi = self.synthetic_disc(Path(tmp), [(0x8c10781c, [0xffffffff])])
            self.reject_fixture(gdi, 'traffic1_grips.bin')

    def test_traffic_cycles_and_invalid_motion_counts(self):
        with tempfile.TemporaryDirectory() as tmp:
            gdi = self.synthetic_disc(Path(tmp), [(0x8c107744, [0x8cb00000])],
                [(0x8cb00000 + 44, [0x8cb00000, 0])])
            self.reject_fixture(gdi, 'traffic1.bin')
        with tempfile.TemporaryDirectory() as tmp:
            gdi = self.synthetic_disc(Path(tmp), [(0x8c107840, [0x8cb00000])],
                [(0x8cb00000, [0, 10000, 0x20003])])
            self.reject_fixture(gdi, 'traffic1.bin')

    def test_mission_chain_cycles_and_outside_links(self):
        for link in (0x8cde0000, 0x8cde00f0, 0xffffffff):
            with tempfile.TemporaryDirectory() as tmp:
                self.reject_fixture(self.synthetic_disc(Path(tmp), mission_link=link), 'missions.bin')

    def test_collision_invalid_indices_nonfinite_and_cyclic_hierarchy(self):
        base = 0x8cb00000
        # One synthetic mesh, patch and strip. The original index words are
        # signed; a negative vertex must not wrap into another source object.
        common = [(base, [6, base + 128]), (base + 36, [0x3f800000] * 3),
                  (base + 128, [base + 256, base + 512, 1 << 16 | 3]),
                  (base + 512 + 24, [base + 768, 1, 1]),
                  (base + 768, [0x0001ffff, 2])]
        with tempfile.TemporaryDirectory() as tmp:
            gdi = self.synthetic_disc(Path(tmp), [(0x8c107ae8, [base])], common)
            self.reject_fixture(gdi, 'stage1.sob')
        with tempfile.TemporaryDirectory() as tmp:
            writes = common[:-1] + [(base + 768, [0x00010000, 2]), (base + 256, [0x7f800000])]
            gdi = self.synthetic_disc(Path(tmp), [(0x8c107ae8, [base])], writes)
            self.reject_fixture(gdi, 'stage1.sob')
        with tempfile.TemporaryDirectory() as tmp:
            writes = [(base, [6]), (base + 64, [base])]
            gdi = self.synthetic_disc(Path(tmp), [(0x8c107ae8, [base])], writes)
            self.reject_fixture(gdi, 'stage1.sob')

    def test_native_collision_retains_authored_strip_order(self):
        base, grid = 0x8cb00000, 0x8cd76ad4
        bits = lambda value: struct.unpack('<I', struct.pack('<f', value))[0]
        # Two triangles alternate the source winding. Reordering their
        # stored vertices must keep the bit needed to reconstruct the raw
        # triangle for exact plane and stationary carry calculations.
        for winding in (0, 0x40000000):
            with tempfile.TemporaryDirectory() as tmp:
                points = [0., 0., 0., 4., 0., 0., 0., 0., 4., 4., 0., 4.]
                writes = [(base+256, [bits(v) for v in points]),
                    (base+512, [0, 0, 0, bits(4.), 0, bits(4.), base+768, winding|2, 6<<16|1]),
                    (base+768, [0x00010000, 0x00030002]),
                    (base+1024, [base+256, base+512, 0, 1<<16]),
                    (grid, [base+1024, 1]), (grid+8192-4, [0])]
                gdi = self.synthetic_disc(Path(tmp), stage_writes=writes)
                native = subprocess.run([str(self.exe), str(gdi), 'native-stage1', 'fixture'], capture_output=True)
                legacy = subprocess.run([str(self.exe), str(gdi), 'stage1.col', 'fixture'], capture_output=True)
                self.assertEqual(native.returncode, 0, native.stderr.decode())
                self.assertEqual(legacy.returncode, 0, legacy.stderr.decode())
                self.assertEqual(struct.unpack_from('<I', native.stdout, 4)[0], 2)
                for triangle in range(2):
                    at = 16+44*triangle
                    self.assertEqual(native.stdout[at:at+40], legacy.stdout[at:at+40])
                    expected = 2 if not (bool(winding) ^ bool(triangle)) else 0
                    self.assertEqual(struct.unpack_from('<H', native.stdout, at+40)[0], expected)
                    self.assertEqual(struct.unpack_from('<H', legacy.stdout, at+40)[0], 0)

    def test_psp_compile(self):
        cc = os.environ.get('PSP_CC') or shutil.which('psp-gcc')
        if not cc:
            self.skipTest('PSP compiler not configured')
        subprocess.run([cc, '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-ffp-contract=off', '-DJGR_PORTABLE_ASSETS=1', '-include', str(ROOT / 'psp/src/portable_assets.h'),
                        '-c', str(ROOT / 'psp/src/portable_world_convert.c'),
                        '-o', str(Path(self.temp.name) / 'world.o')], check=True)

    @unittest.skipUnless(os.environ.get('JGR_TEST_GDI'), 'Supported user disc not configured')
    def test_all_real_outputs_match_catalog(self):
        catalog = {r['path']: r for r in json.loads((ROOT / 'tools/portable/asset_catalog.json').read_text())['files']}
        for name in recipes():
            with self.subTest(output=name):
                result = subprocess.run([str(self.exe), os.environ['JGR_TEST_GDI'], name, 'strict'], capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                self.assertEqual(len(result.stdout), catalog[name]['bytes'])
                self.assertEqual(hashlib.sha256(result.stdout).hexdigest(), catalog[name]['sha256'])


if __name__ == '__main__':
    unittest.main()
