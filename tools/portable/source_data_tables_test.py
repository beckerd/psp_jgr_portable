#!/usr/bin/env python3
"""Synthetic executable checks; no original disc data is a test fixture."""
import hashlib
import struct
import unittest
import source_data_tables as tables


class TablesTests(unittest.TestCase):
    def image(self):
        image = bytearray(0x17f000)
        for i in range(112):
            struct.pack_into("<2i", image, 0x8c10a808 - tables.IMAGE_BASE + i * 16, i, -1)
        for i in range(21):
            struct.pack_into("<4i", image, 0x8c17e524 - tables.IMAGE_BASE + i * 16, 0, 0, 0, 1)
        return image

    def extract(self, image):
        return tables.extract(bytes(image), expected_sha256=hashlib.sha256(image).hexdigest())

    def test_identity_is_required(self):
        with self.assertRaisesRegex(ValueError, "Unsupported executable identity"):
            tables.extract(self.image())

    def test_output_uses_only_values_from_image(self):
        image = self.image()
        out = self.extract(image)
        self.assertEqual(set(out), {"motmap.h", "spraytab.h"})
        self.assertIn(b"MOTMAP[NMOTMAP] = {0,1,2,3,4", out["motmap.h"])
        self.assertIn(b"MOTMAP_ALT[NMOTMAP] = {-1,-1,-1", out["motmap.h"])
        self.assertIn(b"CUES[N_CUES][7] = {{0,0,0,0,0,0,0}", out["spraytab.h"])
        struct.pack_into("<i", image, 0x8c10a808 - tables.IMAGE_BASE, 1234)
        self.assertIn(b"MOTMAP[NMOTMAP] = {1234,1,2", self.extract(image)["motmap.h"])

    def test_bounds_and_runtime_domains_are_checked(self):
        with self.assertRaisesRegex(ValueError, "outside the supplied image"):
            self.extract(self.image()[:100])
        image = self.image()
        struct.pack_into("<i", image, 0x8c10a808 - tables.IMAGE_BASE, 32768)
        with self.assertRaisesRegex(ValueError, "short ABI"):
            self.extract(image)
        image = self.image()
        struct.pack_into("<i", image, 0x8c17e524 - tables.IMAGE_BASE, 11)
        with self.assertRaisesRegex(ValueError, "invalid cue"):
            self.extract(image)


if __name__ == "__main__":
    unittest.main()
