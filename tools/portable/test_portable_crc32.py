"""Shared IEEE CRC regression: alignment, tails and streaming chunk boundaries."""
import ctypes
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]


class CrcParity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc = shutil.which('cc')
        if not cc:
            raise unittest.SkipTest('A host C compiler is required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='jgr-crc-')
        folder = Path(cls.tmp.name)
        source = folder / 'crc.c'
        source.write_text('#include "portable_crc32.h"\n'
                          'uint32_t crc_add(uint32_t crc,const void *p,size_t n)'
                          '{return jgr_crc32_add(crc,p,n);}\n')
        library = folder / 'crc.so'
        subprocess.run([cc, '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'psp/src'), str(source), '-o', str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.add = cls.lib.crc_add
        cls.add.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t]
        cls.add.restype = ctypes.c_uint32

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_known_vectors_and_all_unaligned_tails(self):
        rng = random.Random(0x4a4752)
        payload = bytes(rng.randrange(256) for _ in range(8192))
        for offset in range(16):
            data = ctypes.create_string_buffer(b'X' * offset + payload)
            address = ctypes.addressof(data) + offset
            for length in list(range(65)) + [127, 128, 129, 255, 256, 257, 4095, 4096, 4097, 8192]:
                self.assertEqual(self.add(0xffffffff, address, length) ^ 0xffffffff,
                                 zlib.crc32(payload[:length]), (offset, length))
        for text, expected in [(b'', 0), (b'123456789', 0xcbf43926)]:
            self.assertEqual(self.add(0xffffffff, text, len(text)) ^ 0xffffffff, expected)

    def test_streaming_random_chunks_and_empty_updates(self):
        rng = random.Random(0xc32)
        payload = bytes(rng.randrange(256) for _ in range(131071))
        data = ctypes.create_string_buffer(b'X' + payload)
        address = ctypes.addressof(data) + 1
        crc, offset = 0xffffffff, 0
        while offset < len(payload):
            self.assertEqual(self.add(crc, address + offset, 0), crc)
            count = min(rng.randrange(1, 1025), len(payload) - offset)
            crc = self.add(crc, address + offset, count)
            offset += count
        self.assertEqual(crc ^ 0xffffffff, zlib.crc32(payload))


if __name__ == '__main__':
    unittest.main()
