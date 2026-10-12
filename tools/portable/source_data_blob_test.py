#!/usr/bin/env python3
"""Asset-independent host checks for the typed BSS loading prototype."""
import ctypes
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
import zlib

import source_data_blob as blob

ROOT = Path(__file__).resolve().parents[2]


def fixture():
    text = []
    for mesh in range(3):
        # Deliberately distinct synthetic geometry, never original game data.
        rows = []
        for vertex in range(3):
            fields = [float(mesh + vertex / 4).hex() + "f",
                      float(vertex / 8).hex() + "f", "0x11223344u",
                      float(vertex).hex() + "f", "-0x1.0p+0f", "0x0.0p+0f"]
            rows.append("{" + ",".join(fields) + "}")
        text.append(f"static const BubbleVertex bubble_{mesh}[] __attribute__((aligned(16)))={{" + ",".join(rows) + "};")
        text.append(f"static const unsigned short bubble_idx_{mesh}[]={{0,1,2,2,1,0}};")
        text.append(f"static const BubbleBatch bubble_batch_{mesh}[]={{{{0,0,3,65535,0}},{{0,3,3,0,1}}}};")
    return "\n".join(text)


class BlobTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.result = blob.generate(fixture())
        compiler = shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("A host C compiler is required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="jgr-bss-test-")
        cls.root = Path(cls.tmp.name)
        for name, data in cls.result["source"].items():
            (cls.root / name).write_bytes(data)
        cls.library = cls.root / "prototype.so"
        subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared", "-DJGR_PORTABLE_ASSETS=1",
                        "-I" + str(ROOT / "psp/src"), str(cls.root / "source_data_bubble.c"),
                        str(ROOT / "psp/src/source_data_io.c"), "-o", str(cls.library)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        # A fresh module gives every case an independent zeroed BSS image.
        unique = self.root / (self._testMethodName + ".so")
        shutil.copyfile(self.library, unique)
        self.lib = ctypes.CDLL(str(unique))
        self.lib.source_data_bubble_load.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self.lib.source_data_bubble_load.restype = ctypes.c_int
        self.lib.source_data_bubble_ready.restype = ctypes.c_int
        self.lib.source_data_bubble_load_file.argtypes = [ctypes.c_char_p]
        self.lib.source_data_bubble_load_file.restype = ctypes.c_int

    def load(self, image):
        data = ctypes.create_string_buffer(bytes(image))
        return self.lib.source_data_bubble_load(data, len(image))

    def storage(self):
        return [bytes((ctypes.c_ubyte * row["bytes"]).in_dll(self.lib, row["name"]))
                for row in self.result["layout"]]

    def check_rejection(self, image):
        before = self.storage()
        self.assertLess(self.load(image), 0)
        self.assertEqual(before, self.storage())

    def reseal(self, image):
        struct.pack_into("<I", image, 20, zlib.crc32(image[176:]))
        return image

    def test_zero_bss_then_exact_success(self):
        self.assertEqual(self.lib.source_data_bubble_ready(), 0)
        self.assertTrue(all(not any(row) for row in self.storage()))
        self.assertEqual(self.load(self.result["blob"]), 0)
        self.assertEqual(self.lib.source_data_bubble_ready(), 1)
        for actual, section in zip(self.storage(), blob.parse_mesh(fixture())):
            self.assertEqual(actual, section["data"])
        for row in self.result["layout"]:
            address = ctypes.addressof((ctypes.c_ubyte * row["bytes"]).in_dll(self.lib, row["name"]))
            self.assertEqual(address % 16, 0)

    def test_corrupt_truncated_and_extra_keep_previous_asset(self):
        self.assertEqual(self.load(self.result["blob"]), 0)
        original = self.result["blob"]
        for offset in (0, 4, 8, 12, 16, 20, 24, 28, 32, len(original)-1):
            image = bytearray(original)
            image[offset] ^= 1
            self.check_rejection(image)
        self.check_rejection(original[:-1])
        self.check_rejection(original + b"\x00")
        self.assertEqual(self.lib.source_data_bubble_ready(), 1)

    def test_checksummed_invalid_geometry_does_not_load(self):
        image = bytearray(self.result["blob"])
        indices = self.result["layout"][1]
        struct.pack_into("<H", image, indices["offset"], 65535)
        self.check_rejection(self.reseal(image))
        for field, value in ((0, 0xffffffff), (4, 0xffffffff), (8, 0xffffffff), (8, 0)):
            image = bytearray(self.result["blob"])
            struct.pack_into("<I", image, self.result["layout"][2]["offset"] + field, value)
            self.check_rejection(self.reseal(image))
        image = bytearray(self.result["blob"])
        struct.pack_into("<I", image, self.result["layout"][0]["offset"], 0x7fc00001)
        self.check_rejection(self.reseal(image))
        self.assertEqual(self.lib.source_data_bubble_ready(), 0)

    def test_file_loader_requires_exact_size(self):
        path = self.root / "synthetic.jsd"
        self.assertLess(self.lib.source_data_bubble_load_file(str(path).encode()), 0)
        path.write_bytes(self.result["blob"])
        self.assertEqual(self.lib.source_data_bubble_load_file(str(path).encode()), 0)
        path.write_bytes(self.result["blob"] + b"trailing")
        before = self.storage()
        self.assertLess(self.lib.source_data_bubble_load_file(str(path).encode()), 0)
        self.assertEqual(before, self.storage())

    def test_memory_view_validates_mesh_before_copying(self):
        image = ctypes.create_string_buffer(self.result["blob"]); length = [len(self.result["blob"])]
        callback_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p,
                                         ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t))
        @callback_type
        def view(user, name, data, size):
            if name != b"memory-only.jsd": return -1
            data[0] = ctypes.addressof(image); size[0] = length[0]; return 1
        self.lib.source_data_io_view_begin.argtypes = [callback_type, ctypes.c_void_p]
        self.assertEqual(self.lib.source_data_io_view_begin(view, None), 0)
        try:
            self.assertEqual(self.lib.source_data_bubble_load_file(b"memory-only.jsd"), 0)
            expected = [row["data"] for row in blob.parse_mesh(fixture())]
            self.assertEqual(self.storage(), expected)
            length[0] -= 1
            self.assertLess(self.lib.source_data_bubble_load_file(b"memory-only.jsd"), 0)
            self.assertLess(self.lib.source_data_bubble_load_file(b"missing.jsd"), 0)
            self.assertEqual(self.storage(), expected)
        finally: self.lib.source_data_io_view_end()

    def test_parser_rejects_invalid_and_duplicate_declarations(self):
        text = fixture()
        for invalid in (text.replace("65535,0", "-1,0", 1), text + text,
                        text.replace("0,1,2,2,1,0", "0,1,65535,2,1,0", 1),
                        text.replace("0x0.0p+0f", "some_function()", 1)):
            with self.assertRaises(ValueError):
                blob.generate(invalid)

    def test_public_source_contains_storage_but_no_payload(self):
        source = b"\n".join(self.result["source"].values())
        self.assertNotIn(b"0x11223344", source)
        self.assertNotIn(b"-0x1.0p+0f", source)
        self.assertIn(b"extern BubbleVertex jgr_bubble_v0[3];", source)
        self.assertIn(b"BubbleVertex jgr_bubble_v0[3] __attribute__((aligned(16)));", source)
        self.assertIn(b"PRIVATE", blob.__doc__.encode())

    def test_output_refuses_overwrite_and_separates_private_blob(self):
        mesh = self.root / "input.inc"
        mesh.write_text(fixture())
        out = self.root / "out"
        manifest = blob.write_prototype(mesh, out)
        self.assertFalse(manifest["regenerated_from_disc"])
        self.assertTrue(manifest["data_is_not_public"])
        self.assertTrue((out / "PRIVATE-assets/spray_bubble.jsd").exists())
        self.assertFalse(list((out / "source").rglob("*.jsd")))
        with self.assertRaises(ValueError):
            blob.write_prototype(mesh, out)


if __name__ == "__main__":
    unittest.main()
