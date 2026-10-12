"""Setup dependency lookup must not silently read unverified local assets."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SetupSourceLookup(unittest.TestCase):
    def test_scoped_resolution_missing_dependency_and_reset(self):
        with tempfile.TemporaryDirectory(prefix="jgr-source-io-") as temporary:
            directory = Path(temporary)
            bridge = directory / "bridge.c"
            bridge.write_text('''
#include "source_data_io.h"
#include <string.h>
static unsigned calls;
static FILE *verified(void *context,const char *name)
{calls++;return !strcmp(name,"verified.asset")?fopen(context,"rb"):NULL;}
int begin(const char *path){return source_data_io_begin(verified,(void *)path);}
unsigned count(void){return calls;}
int read_byte(const char *name)
{FILE *f=source_data_io_open(name);if(!f)return -1;int value=fgetc(f);fclose(f);return value;}
''')
            library = directory / "lookup.so"
            subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(ROOT / "psp/src"), str(bridge),
                            str(ROOT / "psp/src/source_data_io.c"), "-o", str(library)], check=True)
            lib = ctypes.CDLL(str(library))
            lib.begin.argtypes = [ctypes.c_char_p]
            lib.read_byte.argtypes = [ctypes.c_char_p]
            lib.source_data_io_begin.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
            verified = directory / "completed.asset"
            verified.write_bytes(b"V")
            local = directory / "unverified.asset"
            local.write_bytes(b"U")
            verified_path = str(verified).encode()
            local_path = str(local).encode()
            self.assertEqual(lib.source_data_io_begin(None, None), -1)
            self.assertEqual(lib.read_byte(local_path), ord("U"))
            self.assertEqual(lib.begin(verified_path), 0)
            self.assertEqual(lib.begin(local_path), -1)
            self.assertEqual(lib.read_byte(b"verified.asset"), ord("V"))
            self.assertEqual(lib.read_byte(local_path), -1)
            self.assertEqual(lib.count(), 2)
            lib.source_data_io_end()
            self.assertEqual(lib.read_byte(local_path), ord("U"))
            self.assertEqual(lib.read_byte(None), -1)
            self.assertEqual(lib.begin(local_path), 0)
            self.assertEqual(lib.read_byte(b"verified.asset"), ord("U"))
            lib.source_data_io_end()


if __name__ == "__main__":
    unittest.main()
