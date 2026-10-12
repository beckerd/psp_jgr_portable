"""Street-map progression and routing with synthetic save state, no assets."""
from pathlib import Path
import ctypes as C
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StreetMap(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='jgr-street-map-')
        cls.addClassCleanup(cls.build.cleanup)
        source = Path(cls.build.name) / 'story_test.c'
        source.write_text('''
#define JGR_SOURCE_DATA_MIXED_STORY_C__Z_0_INC
#define JGR_SOURCE_DATA_MIXED_STORY_C__TBL_0_INC
#define JGR_SOURCE_DATA_MIXED_STORY_C__SCENES_0_INC
static int z[17],tbl[12];
static signed char scenes[15];
#include "story.c"
''')
        library = Path(cls.build.name) / 'story.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-DJGR_PORTABLE_ASSETS=1', '-Ipsp/src', '-Ipsp/tests/stubs',
                        str(source), '-lm', '-o', str(library)], cwd=ROOT, check=True)
        cls.lib = C.CDLL(str(library))
        cls.story = (C.c_ubyte * 1024).in_dll(cls.lib, 'story')
        cls.lib.story_file_name.restype = C.c_char_p

    def setUp(self):
        self.lib.story_reset()

    def available(self, grind=True):
        values = (C.c_ubyte * (9 if grind else 24))()
        (self.lib.story_grind_entries if grind else self.lib.story_entries)(values)
        return [i for i, value in enumerate(values) if value == 0]

    def test_story_unlocks_bantam_then_grind_square(self):
        self.assertEqual(self.available(), [])
        self.story[0x33e] = 1
        self.assertEqual(self.available(), [0])
        self.lib.story_mission_cleared(0x16, 0, 4, 0, 1)
        self.assertEqual(self.available(), [1])
        self.lib.story_mission_cleared(0x1b, 0, 5, 0, 1)
        self.assertEqual(self.available(), [])

    def test_completed_game_exposes_six_jet_missions_and_map_return(self):
        self.story[8] = 1
        self.assertIn(23, self.available(False))
        self.assertEqual(self.available(), list(range(2, 9)))
        self.story[0x33e] = 1
        self.assertEqual(self.available(), [0, *range(2, 9)])
        self.story[0x351] = 1
        self.assertEqual(self.available(), [1, *range(2, 9)])
        self.story[0x356] = 1
        self.assertEqual(self.available(), list(range(2, 9)))

    def test_all_grind_missions_route_to_their_own_chapter(self):
        expected = [(4, 0, -1, b'S5M01'), (5, 0, -1, b'S6M01'),
                    (4, 1, -1, b'S5M02'), (4, 2, -1, b'S5M03'),
                    (4, 3, 3, b'S5M04'), (5, 1, -1, b'S6M02'),
                    (5, 2, -1, b'S6M03'), (5, 3, 6, b'S6M04')]
        before = bytes(self.story)
        for entry, (stage, chapter, special, name) in enumerate(expected):
            with self.subTest(entry=entry):
                ch, sp = C.c_int(), C.c_int()
                st = self.lib.story_grind_entry_stage(entry, C.byref(ch), C.byref(sp))
                self.assertEqual((st, ch.value, sp.value), (stage, chapter, special))
                file = self.lib.story_chapter_file(st, ch)
                self.assertEqual(self.lib.story_file_name(file), name)
        self.assertEqual(bytes(self.story), before)

    def test_map_return_and_invalid_entries_never_resolve_to_a_mission(self):
        for entry in (-1, 8, 9, 23, 256):
            ch, sp = C.c_int(99), C.c_int(99)
            self.assertEqual(self.lib.story_grind_entry_stage(entry, C.byref(ch), C.byref(sp)), -1)
            self.assertEqual((ch.value, sp.value), (-1, -1))


class GrindMapFormat(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='jgr-grind-map-')
        cls.addClassCleanup(cls.build.cleanup)
        library = Path(cls.build.name) / 'map.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-Ipsp/src', 'psp/src/grind_map.c', '-o', str(library)], cwd=ROOT, check=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.grind_map_load.argtypes = [C.c_void_p, C.c_char_p]

    def fixture(self):
        import struct
        entries = b''.join(struct.pack('<ff32s64s', 80 + i * 30, 180, b'TEST', b'PROMPT') for i in range(9))
        return bytearray(b'GMP1' + struct.pack('<I', 9) + entries + bytes(64) + b'EST'.ljust(48, b'\0') + bytes(64 + 8192))

    def load(self, blob):
        path = Path(self.build.name) / 'map.bin'
        path.write_bytes(blob)
        target = C.create_string_buffer(16384)
        return self.lib.grind_map_load(target, str(path).encode()), target

    def test_current_format_loads(self):
        status, target = self.load(self.fixture())
        self.assertEqual(status, 0)
        self.assertEqual(bytes(target)[8:13], b'TEST\0')

    def test_corrupt_or_truncated_files_fail_without_partial_map(self):
        import struct
        good = self.fixture()
        variants = [good[:n] for n in (0, 7, 944, len(good) - 1)] + [good + b'x']
        for offset, data in [(0, b'BAD!'), (4, struct.pack('<I', 24)),
                             (8, struct.pack('<f', float('nan'))), (12, struct.pack('<f', 500)),
                             (16, b'x' * 32), (48, b'x' * 64), (944, b'\x08'),
                             (1008, b'x' * 48)]:
            bad = good.copy(); bad[offset:offset + len(data)] = data; variants.append(bad)
        for blob in variants:
            status, target = self.load(blob)
            self.assertEqual(status, -1)
            self.assertEqual(bytes(target), bytes(len(target)))


if __name__ == '__main__':
    unittest.main()
