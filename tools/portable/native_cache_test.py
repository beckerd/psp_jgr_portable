"""Generated replay scratch must be reused only by its live owner."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NativeCollisionCache(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="jgr-native-cache-")
        cls.root = Path(cls.temporary.name)
        bridge = cls.root / "bridge.c"
        bridge.write_text('''
#include "portable_world_convert.h"
static unsigned calls,fail;
void jgr_native_chars_pose_forget(void){}
int jgr_world_native_collision(JgrDisc *disc,unsigned stage,FILE *out)
{(void)disc;calls++;fprintf(out,"fixture-%u-%u",stage,calls);return fail?-1:0;}
unsigned count(void){return calls;}
void fail_next(int value){fail=value;}
''')
        library = cls.root / "cache.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-std=c99", "-D_XOPEN_SOURCE=700",
                        "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "psp/src"),
                        str(bridge), str(ROOT / "psp/src/portable_native_cache.c"),
                        "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.jgr_native_cache_create.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        cls.lib.jgr_native_cache_create.restype = ctypes.c_void_p
        cls.lib.jgr_native_cache_collision.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                                       ctypes.c_char_p, ctypes.c_uint,
                                                       ctypes.c_char_p, ctypes.c_size_t]
        cls.lib.jgr_native_cache_destroy.argtypes = [ctypes.c_void_p]

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=self.root)
        self.directory = Path(self.temp.name)
        self.name = str(self.directory).encode()
        self.lib.fail_next(0)
        self.cache = self.lib.jgr_native_cache_create(1, self.name)
        self.assertTrue(self.cache)

    def tearDown(self):
        self.lib.jgr_native_cache_destroy(self.cache)
        self.temp.cleanup()

    def load(self, stage=1, disc=1, directory=None, capacity=1200):
        out = ctypes.create_string_buffer(capacity)
        result = self.lib.jgr_native_cache_collision(self.cache, disc, directory or self.name,
                                                     stage, out, capacity)
        return result, out.value

    def test_reuse_is_per_session_and_cleanup_is_owned(self):
        first = self.directory / "native-stage1.col.tmp"
        untouched = self.directory / "native-stage3.col.tmp"
        first.write_bytes(b"old-run")
        untouched.write_bytes(b"untouched")
        before = self.lib.count()
        self.assertEqual(self.load()[0], 0)
        generated = first.read_bytes()
        self.assertNotEqual(generated, b"old-run")
        self.assertEqual(self.load()[0], 0)
        self.assertEqual(first.read_bytes(), generated)
        self.assertEqual(self.lib.count(), before + 1)
        self.lib.jgr_native_cache_destroy(self.cache)
        self.cache = None
        self.assertFalse(first.exists())
        self.assertEqual(untouched.read_bytes(), b"untouched")
        self.cache = self.lib.jgr_native_cache_create(1, self.name)
        self.assertEqual(self.load()[0], 0)
        self.assertEqual(self.lib.count(), before + 2)

    def test_kogane_collision_has_its_own_live_cache_entry(self):
        before=self.lib.count()
        result,path=self.load(stage=2)
        self.assertEqual(result,0)
        self.assertTrue(Path(path.decode()).is_file())
        self.assertEqual(self.load(stage=2),(result,path))
        self.assertEqual(self.lib.count(),before+1)
        # A stage2 cache entry must not alias either stage1 or stage3.
        first=self.load(stage=1)[1];third=self.load(stage=3)[1]
        self.assertEqual(len({path,first,third}),3)
        self.assertIn(b"fixture-2-",Path(path.decode()).read_bytes())
        self.assertEqual(self.load(stage=2),(result,path))
        self.assertEqual(self.lib.count(),before+3)

    def test_stages_binding_and_output_capacity(self):
        before = self.lib.count()
        self.assertEqual(self.load(stage=4)[0], -1)
        self.assertEqual(self.load(disc=2)[0], -1)
        self.assertEqual(self.load(directory=b"wrong")[0], -1)
        self.assertEqual(self.load(capacity=1)[0], -1)
        self.assertEqual(self.lib.count(), before)
        self.assertEqual(self.load(stage=1)[0], 0)
        self.assertEqual(self.load(stage=3)[0], 0)
        self.assertEqual(self.lib.count(), before + 2)

    def test_incomplete_conversion_is_not_cached(self):
        before = self.lib.count()
        self.lib.fail_next(1)
        self.assertEqual(self.load()[0], -1)
        self.assertFalse((self.directory / "native-stage1.col.tmp").exists())
        self.lib.fail_next(0)
        self.assertEqual(self.load()[0], 0)
        self.assertEqual(self.lib.count(), before + 2)

    def test_missing_or_truncated_file_is_regenerated(self):
        before = self.lib.count()
        self.assertEqual(self.load()[0], 0)
        target = self.directory / "native-stage1.col.tmp"
        target.write_bytes(b"")
        self.assertEqual(self.load()[0], 0)
        target.unlink()
        self.assertEqual(self.load()[0], 0)
        self.assertEqual(self.lib.count(), before + 3)

    def test_link_and_directory_cannot_be_overwritten(self):
        target = self.directory / "native-stage1.col.tmp"
        other = self.directory / "external"
        other.write_bytes(b"preserve")
        target.symlink_to(other)
        self.assertEqual(self.load()[0], -1)
        self.assertEqual(other.read_bytes(), b"preserve")
        target.unlink()
        target.mkdir()
        self.assertEqual(self.load()[0], -1)
        target.rmdir()
        linked = self.directory / "link"
        linked.symlink_to(self.directory, target_is_directory=True)
        self.assertFalse(self.lib.jgr_native_cache_create(1, str(linked).encode()))


if __name__ == "__main__":
    unittest.main()
