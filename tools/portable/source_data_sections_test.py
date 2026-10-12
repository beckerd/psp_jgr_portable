#!/usr/bin/env python3
"""Synthetic compiler-ABI and generic loader checks, without game fixtures."""
import ctypes
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import source_data_sections as sections

PREAMBLE = "typedef struct {float x;unsigned value;} Cell;\n"
SOURCE = """/* Synthetic values, unrelated to game assets. */
static const Cell samples[2]={{0x1.8p+0f,123},{-0x1.0p+2f,456}};
static const unsigned short order[]={2,1,0};
static const float grid[2][2]={{0x0.0p+0f,0x1.0p+0f},{0x1.0p+1f,0x1.8p+1f}};
"""
ROOT = Path(__file__).resolve().parents[2]


class GrammarTests(unittest.TestCase):
    def test_unknown_values_and_declarations_fail_closed(self):
        for invalid in (SOURCE.replace("123", "UNKNOWN"), SOURCE + "const char *s=\"payload\";\n",
                        SOURCE + "static const int *const pointers[]={0};\n",
                        SOURCE.replace("grid[2][2]", "grid[SOME_COUNT][2]"),
                        SOURCE + "static const int samples[]={4};\n"):
            with self.assertRaises(ValueError): sections.arrays(invalid)

    def test_non_object_elf_is_rejected(self):
        for image in (b"", b"\x7fELF" + bytes(100), bytes(52)):
            with self.assertRaises(ValueError): sections.elf_arrays(image, sections.arrays(SOURCE))

    def test_local_multiple_declarations_and_constant_expressions(self):
        parsed = sections.arrays("const float first[2]={254.f/255.f,-2.f},second[]={1.f,2.f};")
        self.assertEqual([row["name"] for row in parsed], ["first", "second"])


class CompilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("psp-gcc")
        candidate = Path.home() / "pspdev/bin/psp-gcc"
        if not cls.compiler and candidate.exists(): cls.compiler = str(candidate)
        if not cls.compiler or not shutil.which("cc"): raise unittest.SkipTest("PSP and host C compilers required")
        cls.result = sections.generate(SOURCE, PREAMBLE, cls.compiler, [], "synthetic")
        cls.temp = tempfile.TemporaryDirectory(prefix="jgr-section-tests-")
        cls.root = Path(cls.temp.name)
        for name, data in cls.result["source"].items(): (cls.root / name).write_bytes(data)
        wrapper = PREAMBLE + '#include "source_data_synthetic.inc"\n'
        for i, row in enumerate(cls.result["rows"]):
            wrapper += f'void *address_{i}(void){{return {row["name"]};}}\n'
        (cls.root / "bridge.c").write_text(wrapper)
        cls.library = cls.root / "prototype.so"
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                        "-DJGR_PORTABLE_ASSETS=1", "-I", str(ROOT / "psp/src"), "-I", str(cls.root),
                        str(ROOT / "psp/src/source_data_sections.c"), str(ROOT / "psp/src/source_data_io.c"),
                        str(cls.root / "source_data_synthetic.c"),
                        str(cls.root / "bridge.c"), "-o", str(cls.library)], check=True)

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def setUp(self):
        unique = self.root / (self._testMethodName + ".so")
        shutil.copyfile(self.library, unique)
        self.lib = ctypes.CDLL(str(unique))
        self.destinations = []
        for i, row in enumerate(self.result["rows"]):
            fn = getattr(self.lib, "address_" + str(i)); fn.restype = ctypes.c_void_p
            self.destinations.append(fn())
        class Entry(ctypes.Structure):
            _fields_ = [("id", ctypes.c_uint), ("offset", ctypes.c_uint), ("bytes", ctypes.c_uint),
                        ("alignment", ctypes.c_uint), ("destination", ctypes.c_void_p)]
        self.entries = (Entry * len(self.result["layout"]))(
            *[Entry(row["id"], row["offset"], row["bytes"], row["alignment"], pointer)
              for row, pointer in zip(self.result["layout"], self.destinations)])
        self.lib.source_data_sections_apply.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                                       ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
        self.lib.source_data_sections_apply.restype = ctypes.c_int

    def load(self, image):
        memory = ctypes.create_string_buffer(bytes(image))
        crc = struct.unpack_from("<I", self.result["blob"], 16)[0]
        return self.lib.source_data_sections_apply(memory, len(image), self.entries, len(self.entries),
                                                   len(self.result["blob"]), crc)

    def bytes(self):
        return [ctypes.string_at(pointer, row["bytes"])
                for pointer, row in zip(self.destinations, self.result["rows"])]

    def test_exact_compiler_bytes_reach_aligned_zeroed_bss(self):
        self.assertTrue(all(not any(row) for row in self.bytes()))
        self.assertEqual(self.load(self.result["blob"]), 0)
        self.assertEqual(self.bytes(), [row["data"] for row in self.result["rows"]])
        self.assertEqual(self.result["rows"][1]["count"], 3)
        self.assertTrue(all(pointer % 16 == 0 for pointer in self.destinations))

    def test_memory_view_runs_original_validation_without_file_fallback(self):
        image = ctypes.create_string_buffer(self.result["blob"])
        length = [len(self.result["blob"])]
        callback_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p,
                                         ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t))
        @callback_type
        def view(user, name, data, size):
            if name != b"memory-only.jsd": return -1
            data[0] = ctypes.addressof(image); size[0] = length[0]; return 1
        self.lib.source_data_io_view_begin.argtypes = [callback_type, ctypes.c_void_p]
        self.assertEqual(self.lib.source_data_io_view_begin(view, None), 0)
        load = self.lib.source_data_sections_load
        load.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
        args = (self.entries, len(self.entries), len(self.result["blob"]), struct.unpack_from("<I", self.result["blob"], 16)[0])
        try:
            self.assertEqual(load(b"memory-only.jsd", *args), 0)
            expected = [row["data"] for row in self.result["rows"]]
            self.assertEqual(self.bytes(), expected)
            length[0] -= 1
            self.assertLess(load(b"memory-only.jsd", *args), 0)
            self.assertLess(load(b"missing.jsd", *args), 0)
            self.assertEqual(self.bytes(), expected)
        finally: self.lib.source_data_io_view_end()

    def test_failure_does_not_partially_replace_previous_data(self):
        self.assertEqual(self.load(self.result["blob"]), 0)
        previous = self.bytes()
        for offset in (0, 4, 8, 12, 16, 20, 24, 28, 32, len(self.result["blob"])-1):
            image = bytearray(self.result["blob"]); image[offset] ^= 1
            self.assertLess(self.load(image), 0)
            self.assertEqual(self.bytes(), previous)
        self.assertLess(self.load(self.result["blob"][:-1]), 0)
        self.entries[2].offset = 0  # Even trusted registry bounds are checked.
        self.assertLess(self.load(self.result["blob"]), 0)
        self.assertEqual(self.bytes(), previous)

    def test_public_source_has_external_abi_without_original_literals(self):
        public = b"\n".join(self.result["source"].values())
        self.assertNotIn(b"0x1.8p+0f", public)
        self.assertNotIn(b"123", public)
        self.assertNotIn(b"456", public)
        self.assertIn(b"extern unsigned short order[3]", public)
        self.assertIn(b'__asm__("jgr_data_synthetic_samples")', public)

    def test_pointer_relocation_is_never_copied_as_raw_numeric_data(self):
        source = self.root / "relocation.c"
        source.write_text('extern int other;\nstatic const int *references[1] __attribute__((used,section(".jgr_asset_references")))={&other};\nconst unsigned stride __attribute__((used,section(".jgr_stride_references")))=sizeof references[0];\n')
        obj = self.root / "relocation.o"
        subprocess.run([self.compiler, "-O0", "-G0", "-c", str(source), "-o", str(obj)], check=True)
        rows = sections.arrays("static const unsigned references[1]={0};")
        with self.assertRaisesRegex(ValueError, "Pointer/code relocation"):
            sections.elf_arrays(obj.read_bytes(), rows)


if __name__ == "__main__": unittest.main()
