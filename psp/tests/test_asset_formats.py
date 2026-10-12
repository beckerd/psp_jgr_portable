"""Exercise the production loaders with current and unsupported asset formats."""
import ctypes as C
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def section(name, start, end):
    source = (ROOT / 'psp/src' / name).read_text()
    return source[source.index(start):source.index(end, source.index(start))]


class AssetFormats(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='jgr-format-test-')
        path = Path(cls.build.name)
        # Compile the actual loader bodies and ABI headers. Only platform IO,
        # rendering fences and progress callbacks are supplied by the harness.
        code = '''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <pspkernel.h>
#include "graffiti.h"
#include "npc.h"
void gfx_wait_idle(void){}
void asset_progress(void){}
void sceKernelDcacheWritebackAll(void){}
static void stage_prepared_forget(void){}
static void stage_capture_forget(void){}
'''
        code += section('canplay.c', 'void graffiti_can_drops_free', 'int graffiti_can_drop')
        code += section('canplay.c', 'int graffiti_load_cans', 'int graffiti_cans_select')
        code += section('graffiti.c', 'static int finite_words', 'void graffiti_free')
        code += section('font.c', 'static unsigned char *g_font', 'static void font_bind')
        code += section('npc.c', 'static int person_model', '/* people*.jgc')
        code += section('npc.c', 'int npcs_load(', '/* Rival / police squads')
        code += section('stage_instances.inc', 'static void stage_instance_point', 'static int stage_bind_instances')
        code += section('stage_visibility.inc', '#ifndef STAGE_ACTIVE_OCCLUDERS', 'static float visibility_dot')
        code += '''
int check_cans(const char *path){
    Graffiti g={0};int error=graffiti_load_cans(&g,path);
    free(g.cans);return error;
}
int check_spots(const char *path){
    Graffiti g;int error=graffiti_load(&g,path,NULL);free_spots(&g);return error;
}
int check_peds(const char *path){
    Npcs s;int error=npcs_load(&s,path,NULL,1,1);free(s.p);return error;
}
int check_font(const char *path){return font_real_load(path);}
int check_wide_font(const char *path){return font_real_load_wide(path);}
int check_visibility(const char *path){
    unsigned char base=0;JgsHeader h={.n_obj=1};
    Stage s={.base=&base,.h=&h};return stage_visibility_load(&s,path);
}
'''
        source = path / 'formats.c'; source.write_text(code)
        library = path / 'formats.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-std=gnu99',
                        '-Werror=implicit-function-declaration', '-DJGR_PORTABLE_ASSETS=1', '-Ipsp/src',
                        '-Ipsp/tests/stubs', '-Ipsp/tests/render_stubs',
                        str(source), '-lm', '-o', str(library)], cwd=ROOT, check=True)
        cls.lib = C.CDLL(str(library))
        for name in ('cans', 'spots', 'peds', 'font', 'wide_font', 'visibility'):
            getattr(cls.lib, 'check_' + name).argtypes = [C.c_char_p]

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def check(self, name, data):
        path = Path(self.build.name) / 'fixture.bin'; path.write_bytes(data)
        return getattr(self.lib, 'check_' + name)(os.fsencode(path))

    def test_current_formats_load_and_previous_formats_are_rejected(self):
        cans = struct.pack('<4sIIIIIfffI', b'CAN3', 1, 1, 0, 0, 0, 1., 2., 3., 3)
        spots = (struct.pack('<4sI', b'SPT6', 1) + struct.pack('<II40f', 0, 0, 1., *([0.] * 39)) +
                 struct.pack('<II', 0, 0) + bytes(88))
        peds = struct.pack('<4sIIIIIffI3f', b'PED3', 1, 0, 1, 0, 0, 0., 1., 0, 1., 2., 3.)
        font = struct.pack('<4sIIIII', b'FNT2', 512, 512, 12, 24, 42) + bytes(64 + 512 * 256)
        wide = struct.pack('<4sIIIII', b'FNT2', 512, 512, 24, 24, 21) + bytes(64 + 512 * 256)
        visibility = (struct.pack('<4sIIIiiIII', b'JVO5', 1, 0, 2166136261, 0, 0, 1, 1, 1) +
                      bytes(24) + b'\xff' * 128 + struct.pack('<4fHHH', 0., 0., 0., 0., 0, 1, 0))
        for name, data, previous in (
                ('cans', cans, (b'CAN1', b'CAN2')),
                ('spots', spots, tuple(('SPT' + str(i)).encode() for i in range(1, 6))),
                ('peds', peds, (b'PED1', b'PED2')),
                ('font', font, (b'FNT1',)), ('wide_font', wide, (b'FNT1',)),
                ('visibility', visibility, (b'JVO4',))):
            with self.subTest(loader=name):
                self.assertEqual(self.check(name, data), 0)
                for magic in previous:
                    # CAN2 allowed only paint, so use a paint record when
                    # checking its rejection independently of the kind limit.
                    body = data[4:-4] + bytes(4) if magic == b'CAN2' else data[4:]
                    self.assertLess(self.check(name, magic + body), 0, magic)

    def test_prepared_current_assets(self):
        supplied = os.environ.get('JGR_TEST_ASSETS')
        if not supplied:
            self.skipTest('Set JGR_TEST_ASSETS to check a prepared installation')
        checked = 0
        for path in Path(supplied).rglob('*'):
            if not path.is_file():
                continue
            with path.open('rb') as stream:
                magic = stream.read(4)
            name = {b'CAN3': 'cans', b'SPT6': 'spots', b'PED3': 'peds', b'FNT2': 'font'}.get(magic)
            if name:
                if name == 'font' and struct.unpack_from('<I', path.read_bytes(), 12)[0] == 24:
                    name = 'wide_font'
                with self.subTest(path=path.name):
                    self.assertEqual(getattr(self.lib, 'check_' + name)(os.fsencode(path)), 0)
                checked += 1
        self.assertGreater(checked, 0)


if __name__ == '__main__':
    unittest.main()
