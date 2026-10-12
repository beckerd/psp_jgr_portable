"""Converter binding tests use constructed data, not original asset fixtures."""
import importlib
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'decompile/tools'))
lod = importlib.import_module('enemy_lod_assets')


class ConverterBindingTests(unittest.TestCase):
    def test_reads_texture_order_from_user_list(self):
        data = bytearray(64)
        struct.pack_into('<I', data, 0, 0x8c010010)
        struct.pack_into('<4I', data, 20, 3, 0x8ca00030, 0x8ca00010, 0x8ca00020)
        with patch.object(lod, 'pvr_offsets', return_value=[16, 32, 48]):
            self.assertEqual(lod.far_texture_mapping(data, 0x8c010000, b'synthetic'), (2, 0, 1))

    def test_rejects_invalid_user_pointer_or_texture(self):
        with self.assertRaises(ValueError):
            lod.far_texture_mapping(bytes(4), 0x8c010008, b'synthetic')
        data = bytearray(64)
        struct.pack_into('<I', data, 0, 0x8c010010)
        struct.pack_into('<2I', data, 20, 1, 0x8ca00040)
        with patch.object(lod, 'pvr_offsets', return_value=[16, 32, 48]):
            with self.assertRaises(ValueError):
                lod.far_texture_mapping(data, 0x8c010000, b'synthetic')
            struct.pack_into('<I', data, 20, 4)
            with self.assertRaises(ValueError):
                lod.far_texture_mapping(data, 0x8c010000, b'synthetic')


if __name__ == '__main__':
    unittest.main()
