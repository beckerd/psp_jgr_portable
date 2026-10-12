"""Synthetic input tests; no recovered game light values or captures."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('baked_light_input_test', ROOT / 'decompile/tools/baked_light.py')
light = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(light)


def synthetic():
    data = bytearray(0x8c02f928 - light.EXE_BASE)
    struct.pack_into('<H', data, 0x8c02f74c - light.EXE_BASE, 0xf69d)
    for address, value in ((0x8c02f904, .23), (0x8c02f90c, .62),
                           (0x8c02f910, -.4), (0x8c02f914, -.83)):
        struct.pack_into('<f', data, address - light.EXE_BASE, value)
    for address, value in ((0x8c02f91c, 43), (0x8c02f920, 87), (0x8c02f924, 211)):
        struct.pack_into('<I', data, address - light.EXE_BASE, value)
    return data


class BakedLightInputTests(unittest.TestCase):
    def test_parameters_come_from_input(self):
        self.assertEqual(light.profile_from_executable(synthetic()), {
            'direction': (-.4, -.83, -.4), 'diffuse': .62, 'ambient': .23,
            'rgb': (1., 1., 1.), 'cel_direction': (-.4, -.4, -.4),
            'cel_colors': ((211, 211, 211), (87, 87, 43)),
        })

    def test_decimal_conversion_round_trips_binary32(self):
        for value in (.23, -.83, .62, 1e-20, 1e15, -0., 0.):
            raw = struct.pack('<f', value)
            self.assertEqual(struct.pack('<f', light._decimal32(raw)), raw)

    def test_rejects_missing_or_malformed_input(self):
        with self.assertRaises(ValueError):
            light.profile_from_executable(bytes(16))
        for address, fmt, value in ((0x8c02f74c, '<H', 0),
                                    (0x8c02f924, '<I', 256),
                                    (0x8c02f904, '<f', float('nan'))):
            data = synthetic()
            struct.pack_into(fmt, data, address - light.EXE_BASE, value)
            with self.subTest(address=address), self.assertRaises(ValueError):
                light.profile_from_executable(data)

    def test_import_option_selection_and_tuning_need_no_disc(self):
        light._load_profile.cache_clear()
        with patch.object(Path, 'read_bytes', side_effect=FileNotFoundError('no disc')) as read:
            self.assertIn('shibuya', light.PROFILES)
            self.assertNotIn('missing', light.PROFILES)
            self.assertEqual(list(light.PROFILES), ['shibuya'])
            self.assertEqual(light.character_shadow_lift('synthetic.bin'), 0)
            read.assert_not_called()
            with self.assertRaises(FileNotFoundError):
                light.PROFILES['shibuya']
            with self.assertRaises(KeyError):
                light.PROFILES['missing']


if __name__ == '__main__':
    unittest.main()
