"""The compiled-data audit must reject initialized or malformed asset storage."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('browser_provenance', Path(__file__).resolve().parents[1]/'provenance.py')
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def integer(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128); value >>= 7
    result.append(value)
    return bytes(result)


def object_bytes(payload, flags=0):
    prefix = b'' if flags == 1 else b'\x41\0\x0b'
    if flags == 2:
        prefix = b'\0' + prefix
    section = b'\1' + integer(flags) + prefix + integer(len(payload)) + payload
    return b'\0asm\1\0\0\0' + b'\x0b' + integer(len(section)) + section


def evidence(segment='.bss.jgr_data_test_values', offset=0, size=4):
    return '''Sections [
      Segment {
        Name: %s
        Size: 4
        Offset: 0
      }
]
Symbols [
  Symbol {
    Name: jgr_data_test_values
    Type: DATA (0x1)
    Flags [ (0x4)
      VISIBILITY_HIDDEN (0x4)
    ]
    Offset: 0x%x
    Segment: 0x0
    Size: 0x%x
  }
]
''' % (segment, offset, size)


class ProvenanceTests(unittest.TestCase):
    def test_wasm_active_passive_and_explicit_memory_segments(self):
        for flags in (0, 1, 2):
            with self.subTest(flags=flags):
                self.assertEqual(AUDIT.wasm_data(object_bytes(b'\0\0\0\0', flags)), [b'\0\0\0\0'])

    def test_malformed_data_fails_closed(self):
        good = object_bytes(b'\0\0\0\0')
        for bad in (b'', b'notwasm', good[:-1], object_bytes(b'\0', 3)):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                AUDIT.wasm_data(bad)

    def test_external_assets_must_be_zero_bss_with_bounded_ranges(self):
        with tempfile.TemporaryDirectory() as temporary:
            obj = Path(temporary)/'test.o'; obj.write_bytes(object_bytes(b'\0'*4))
            self.assertEqual(AUDIT.zero_asset_storage(obj, evidence())['jgr_data_test_values']['bytes'], 4)
            for text in (evidence('.rodata.values'), evidence(offset=1), evidence(size=5), evidence(size=0)):
                with self.subTest(text=text), self.assertRaises(ValueError):
                    AUDIT.zero_asset_storage(obj, text)
            obj.write_bytes(object_bytes(b'\0\0\x01\0'))
            with self.assertRaisesRegex(ValueError, 'initialized values'):
                AUDIT.zero_asset_storage(obj, evidence())

    def test_dependency_paths_with_spaces_and_wrong_object(self):
        with tempfile.TemporaryDirectory(prefix='browser audit ') as temporary:
            root = Path(temporary).resolve(); obj = root/'test.o'; source = root/'source.c'; dep = root/'test.d'
            obj.write_bytes(b'object'); source.write_text('int value;\n')
            escape = lambda path: str(path).replace(' ', '\\ ')
            dep.write_text(escape(obj)+': \\\n  '+escape(source)+'\n')
            self.assertEqual(AUDIT.dependencies(dep, obj), [source])
            with self.assertRaisesRegex(ValueError, 'target differs'):
                AUDIT.dependencies(dep, root/'wrong.o')


if __name__ == '__main__':
    unittest.main()
