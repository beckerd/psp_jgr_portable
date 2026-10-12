"""Synthetic fixtures for source-backed legacy converter texture bindings."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'decompile/tools'))
from texture_binding import resolve_texture_binding


class TextureBinding(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='jgr-texture-binding-')
        self.addCleanup(self.temp.cleanup)
        self.exe = Path(self.temp.name) / 'source.bin'
        self.base, self.array, self.texture_base = 0x1000, 0x1010, 0x8000
        chunk = b'GBIX' + struct.pack('<I', 8) + bytes(8)
        chunk += b'PVRT' + struct.pack('<I', 12) + bytes(12)
        self.texture = chunk * 3
        self.chunk_size = len(chunk)
        self.write_pointers((self.texture_base + self.chunk_size * 2,
                             self.texture_base,
                             self.texture_base + self.chunk_size))

    def write_pointers(self, pointers):
        self.exe.write_bytes(bytes(self.array-self.base) + struct.pack('<'+'I'*len(pointers), *pointers))

    def resolve(self, binding=None, texture=None):
        return resolve_texture_binding(
            binding or '1,1000,1010,3,8000,3',
            self.texture if texture is None else texture, self.exe)

    def test_reads_pointer_order_and_native_rotation(self):
        self.assertEqual(self.resolve(), [2, 0, 1])
        self.assertEqual(self.resolve('1,1000,1010,10003,8000,3'), [0, 1, 2])
        self.assertEqual(self.resolve('1,1000,1010,20003,8000,3'), [1, 2, 0])

    def test_mapping_changes_with_user_input(self):
        self.write_pointers((self.texture_base + self.chunk_size,
                             self.texture_base + self.chunk_size * 2,
                             self.texture_base))
        self.assertEqual(self.resolve(), [1, 2, 0])

    def test_rejects_bad_fields_readers_extents_and_rotation(self):
        for binding in ('1,1000', '1,1000,1010,no,8000,3',
                        '2,1000,1010,3,8000,3', '1,1000,1010,3,8000,2',
                        '1,-1,1010,3,8000,3', '1,1000,100000000,3,8000,3',
                        '1,1000,1011,3,8000,3', '1,1000,1010,0,8000,3',
                        '1,1000,1010,101,8000,3', '1,1000,1010,30003,8000,3',
                        '1,1000,ffc,3,8000,3', '1,1000,1014,3,8000,3',
                        '1,fffffff0,fffffffc,3,8000,3'):
            with self.subTest(binding=binding), self.assertRaises(ValueError):
                self.resolve(binding)

    def test_rejects_unknown_and_underflowing_texture_pointers(self):
        for address in (self.texture_base - 4, self.texture_base + 1,
                        self.texture_base + len(self.texture), 0xffffffff):
            self.write_pointers((address,))
            with self.subTest(address=address), self.assertRaisesRegex(ValueError, 'source GBIX'):
                self.resolve('1,1000,1010,1,8000,3')

    def test_rejects_truncated_or_invalid_texture_chunks(self):
        self.write_pointers((self.texture_base,))
        truncated_header = bytearray(self.texture[:self.chunk_size])
        struct.pack_into('<I', truncated_header, 4, 0xffffffff)
        wrong_magic = bytearray(self.texture[:self.chunk_size])
        wrong_magic[16:20] = b'nope'
        too_short = bytearray(self.texture[:self.chunk_size])
        struct.pack_into('<I', too_short, 20, 7)
        for texture in (b'GBIX', truncated_header, wrong_magic, too_short,
                        self.texture[:self.chunk_size-1]):
            with self.subTest(texture=bytes(texture)), self.assertRaises(ValueError):
                self.resolve('1,1000,1010,1,8000,3', texture)

    def test_missing_user_executable_fails(self):
        self.exe.unlink()
        with self.assertRaises(FileNotFoundError):
            self.resolve()


if __name__ == '__main__':
    unittest.main()
