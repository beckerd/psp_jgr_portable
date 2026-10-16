"""Synthetic bundle corruption, cache-repair, and native streamed-reader tests."""
import ctypes as C
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

import bundle

ROOT = Path(__file__).resolve().parents[2]


class Result(C.Structure):
    _fields_ = [('status', C.c_int), ('written', C.c_uint), ('reused', C.c_uint),
                ('entries', C.c_uint), ('index_crc', C.c_uint)]


class Stream(C.Structure):
    _fields_ = [('file', C.c_void_p), ('remaining', C.c_uint32), ('crc', C.c_uint32),
                ('expected_crc', C.c_uint32), ('error', C.c_int)]


Progress = C.CFUNCTYPE(None, C.c_uint, C.c_uint, C.c_char_p, C.c_void_p)


def prx(marker):
    data = bytearray(9000)
    data[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HH', data, 16, 0xffa0, 8)
    struct.pack_into('<H', data, 40, 52)
    data[52:] = bytes([marker]) * (len(data) - 52)
    return bytes(data)


def pbp(payload):
    return struct.pack('<10I', 0x50425000, 0x10000, *([40] * 8)) + payload


def index_change(data, offset, value):
    changed = bytearray(data)
    struct.pack_into('<I', changed, offset, value)
    count = struct.unpack_from('<I', changed, 12)[0]
    struct.pack_into('<I', changed, 20, zlib.crc32(changed[24:24 + 16 * count]) & 0xffffffff)
    return bytes(changed)


class BundleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='jgr-bundle-test-')
        library = Path(cls.build.name) / 'bundle.so'
        bridge = Path(cls.build.name) / 'bridge.c'
        bridge.write_text(r'''
#include <stdio.h>
#include <stddef.h>
#include <string.h>
static size_t read_bytes,write_bytes;
static int fail_receipt_write;
void test_reset_io(void){read_bytes=write_bytes=0;fail_receipt_write=0;}
size_t test_read_bytes(void){return read_bytes;}
size_t test_write_bytes(void){return write_bytes;}
void test_fail_receipt_write(void){fail_receipt_write=1;}
static size_t counted_read(void *p,size_t size,size_t count,FILE *f)
{size_t got=fread(p,size,count,f);read_bytes+=size*got;return got;}
static size_t counted_write(const void *p,size_t size,size_t count,FILE *f)
{
    if(fail_receipt_write && size*count>=8 && !memcmp(p,"JGRCCH1\0",8))return 0;
    size_t got=fwrite(p,size,count,f);write_bytes+=size*got;return got;
}
#define fread counted_read
#define fwrite counted_write
#include "portable_bundle.c"
''')
        subprocess.run(['cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'psp/src'), str(bridge),
                        '-o', str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.portable_bundle_prepare.argtypes = [C.c_char_p, C.POINTER(Result)]
        cls.lib.portable_bundle_try_cached.argtypes = [C.c_char_p, C.POINTER(Result)]
        cls.lib.portable_bundle_prepare_progress.argtypes = [C.c_char_p, C.POINTER(Result),
                                                            Progress, C.c_void_p]
        cls.lib.portable_bundle_path.argtypes = [C.c_uint, C.c_void_p, C.c_size_t]
        cls.lib.portable_bundle_open_entry.argtypes = [C.c_uint, C.POINTER(Stream)]
        cls.lib.portable_bundle_read.argtypes = [C.POINTER(Stream), C.c_void_p, C.c_size_t]
        cls.lib.portable_bundle_read.restype = C.c_size_t
        cls.lib.portable_bundle_close.argtypes = [C.POINTER(Stream)]
        cls.lib.test_read_bytes.restype = cls.lib.test_write_bytes.restype = C.c_size_t

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.lib.test_reset_io()
        self.temporary = tempfile.TemporaryDirectory(prefix='jgr-bundle-case-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.inputs = [self.root / name for name in ('me.prx', 'crash.prx', 'notice.txt', 'recipe.bin', 'setup.prx')]
        self.payloads = [prx(17), prx(42), b'Synthetic project notice\n', b'recipe metadata\n' * 70000, prx(61)]
        for path, data in zip(self.inputs, self.payloads):
            path.write_bytes(data)
        self.data, self.receipt = bundle.build(*self.inputs)
        self.game = self.root / 'game'
        self.game.mkdir()
        (self.game / 'EBOOT.PBP').write_bytes(pbp(self.data))

    def prepare(self, game=None):
        result = Result()
        status = self.lib.portable_bundle_prepare(str(game or self.game).encode(), C.byref(result))
        return status, result

    def try_cached(self, game=None):
        result = Result()
        status = self.lib.portable_bundle_try_cached(str(game or self.game).encode(), C.byref(result))
        return status, result

    def assert_unavailable(self):
        self.assertLess(self.lib.portable_bundle_path(1, C.create_string_buffer(768), 768), 0)
        self.assertLess(self.lib.portable_bundle_open_entry(1, C.byref(Stream())), 0)

    def path(self, role):
        output = C.create_string_buffer(768)
        self.assertEqual(self.lib.portable_bundle_path(role, output, len(output)), 0)
        return Path(output.value.decode())

    def prepare_progress(self):
        events = []
        callback = Progress(lambda current, total, name, user:
                            events.append((name.decode(), current, total, user)))
        result = Result()
        status = self.lib.portable_bundle_prepare_progress(str(self.game).encode(),
                                                           C.byref(result), callback, 1234)
        return status, result, events

    def test_build_is_deterministic_and_contains_only_named_inputs(self):
        second, receipt = bundle.build(*self.inputs)
        self.assertEqual(second, self.data)
        self.assertEqual(receipt, self.receipt)
        self.assertEqual(len(receipt['entries']), 5)
        for row, data in zip(receipt['entries'], self.payloads):
            self.assertEqual(row['sha256'], hashlib.sha256(data).hexdigest())
        for count in (3, 4):
            with self.subTest(count=count), self.assertRaises(ValueError):
                bundle.build(*self.inputs[:count])

    def test_cold_install_warm_cache_and_large_recipe_are_exact(self):
        status, result = self.prepare()
        self.assertEqual(status, 0)
        self.assertEqual((result.written, result.reused, result.entries), (5, 0, 5))
        paths = [self.path(i + 1) for i in range(5)]
        for path, data in zip(paths, self.payloads):
            self.assertTrue(path.is_absolute())
            self.assertEqual(path.read_bytes(), data)
        modified = [path.stat().st_mtime_ns for path in paths]
        status, result = self.prepare()
        self.assertEqual(status, 0)
        self.assertEqual((result.written, result.reused), (0, 5))
        self.assertEqual([path.stat().st_mtime_ns for path in paths], modified)

    def test_three_and_four_entry_bundles_are_rejected(self):
        for count in (3, 4):
            with self.subTest(count=count):
                offset = bundle.HEADER.size + count * bundle.ENTRY.size
                entries = []
                for index, payload in enumerate(self.payloads[:count]):
                    entries.append(bundle.ENTRY.pack(index + 1, offset, len(payload), zlib.crc32(payload)))
                    offset += len(payload)
                table = b''.join(entries)
                data = (bundle.HEADER.pack(bundle.MAGIC, 2, count, offset, zlib.crc32(table)) +
                        table + b''.join(self.payloads[:count]))
                with self.assertRaises(ValueError):
                    bundle.validate(data)
                (self.game / 'EBOOT.PBP').write_bytes(pbp(data))
                self.assertLess(self.prepare()[0], 0)
                self.assertFalse((self.game / 'data').exists())

    def test_large_notice_is_extracted_and_reused_exactly(self):
        notice = b'Synthetic copyright and license notice\n' * 3000
        self.assertGreater(len(notice), 64 * 1024)
        self.inputs[2].write_bytes(notice)
        data, _ = bundle.build(*self.inputs)
        (self.game / 'EBOOT.PBP').write_bytes(pbp(data))
        status, result = self.prepare()
        self.assertEqual((status, result.written), (0, 5))
        self.assertEqual(self.path(3).read_bytes(), notice)
        status, result = self.try_cached()
        self.assertEqual((status, result.reused), (0, 5))
        self.assertEqual(self.path(3).read_bytes(), notice)

    def test_notice_limit_is_enforced_by_packager_and_native_reader(self):
        notice = b'N' * (128 * 1024)
        self.inputs[2].write_bytes(notice)
        data, _ = bundle.build(*self.inputs)
        (self.game / 'EBOOT.PBP').write_bytes(pbp(data))
        self.assertEqual(self.prepare()[0], 0)
        self.assertEqual(self.path(3).read_bytes(), notice)

        self.inputs[2].write_bytes(notice + b'N')
        with self.assertRaisesRegex(ValueError, 'limit'):
            bundle.build(*self.inputs)
        # Construct a checksum-valid over-limit bundle independently of the
        # packager so the native reader must enforce the same bound.
        payloads = self.payloads[:]
        payloads[2] = notice + b'N'
        offset = bundle.HEADER.size + len(payloads) * bundle.ENTRY.size
        entries = []
        for index, payload in enumerate(payloads):
            entries.append(bundle.ENTRY.pack(index + 1, offset, len(payload), zlib.crc32(payload)))
            offset += len(payload)
        table = b''.join(entries)
        invalid = (bundle.HEADER.pack(bundle.MAGIC, 2, len(payloads), offset, zlib.crc32(table)) +
                   table + b''.join(payloads))
        with self.assertRaisesRegex(ValueError, 'entry'):
            bundle.validate(invalid)
        (self.game / 'EBOOT.PBP').write_bytes(pbp(invalid))
        self.assertEqual(self.prepare()[0], -2)
        self.assert_unavailable()

    def test_progress_covers_chunked_verification_extraction_and_warm_cache(self):
        roles = ('jgr_me.prx', 'jgr_crash.prx', 'crash_handler_license.txt', 'install.recipe', 'setup.prx')
        for cold in (True, False):
            status, result, events = self.prepare_progress()
            self.assertEqual(status, 0)
            self.assertEqual((result.written, result.reused), (5, 0) if cold else (0, 5))
            self.assertEqual(events[0], ('Reading application bundle', 0, 0, 1234))
            actions = ('Checking bundled', 'Extracting', 'Verifying extracted') if cold else (
                'Checking bundled', 'Checking cached')
            expected_names = {action + ' ' + role for action in actions for role in roles}
            self.assertEqual({event[0] for event in events[1:]}, expected_names)
            for action in actions:
                for role, data in zip(roles, self.payloads):
                    rows = [event for event in events if event[0] == action + ' ' + role]
                    self.assertEqual(rows[0][1], 0)
                    self.assertEqual(rows[-1][1], len(data))
                    self.assertTrue(all(row[2:] == (len(data), 1234) for row in rows))
                    self.assertTrue(all(0 < after[1] - before[1] <= 16 * 1024
                                        for before, after in zip(rows, rows[1:])))
            for role, data in zip(range(1, 6), self.payloads):
                self.assertEqual(self.path(role).read_bytes(), data)

    def test_progress_does_not_publish_damaged_bundle_or_claim_extraction(self):
        changed = bytearray(pbp(self.data));changed[-len(self.payloads[4])-1] ^= 1
        (self.game / 'EBOOT.PBP').write_bytes(changed)
        status, result, events = self.prepare_progress()
        self.assertEqual(status, -3)
        self.assertEqual((result.written, result.reused), (0, 0))
        self.assertTrue(any(row[0] == 'Checking bundled install.recipe' for row in events))
        self.assertFalse(any(row[0].startswith(('Extracting', 'Verifying extracted'))
                             for row in events))
        self.assertFalse((self.game / 'data').exists())

    def test_converter_module_is_streamed_verified_and_repaired(self):
        game=self.root/'setup.prx'
        content=prx(61)+bytes(400000)
        game.write_bytes(content)
        data,receipt=bundle.build(*self.inputs[:4],converter=game)
        self.assertEqual(len(receipt['entries']),5)
        (self.game/'EBOOT.PBP').write_bytes(pbp(data))
        status,result=self.prepare()
        self.assertEqual((status,result.entries,result.written),(0,5,5))
        installed=self.path(5)
        self.assertEqual(installed.read_bytes(),content)
        installed.write_bytes(b'interrupted converter module')
        self.assertEqual(self.prepare()[0],0)
        self.assertEqual(self.path(5).read_bytes(),content)
        game.write_bytes(b'not a project PRX')
        with self.assertRaisesRegex(ValueError,'PRX'):
            bundle.build(*self.inputs[:4],converter=game)
        with self.assertRaisesRegex(ValueError,'instructions'):
            bundle.build(*self.inputs[:3],converter=game)

    def test_corrupt_cached_helper_and_interrupted_temp_are_repaired(self):
        self.assertEqual(self.prepare()[0], 0)
        path = self.path(1)
        path.write_bytes(b'corrupt')
        temporary = path.with_name(path.name + '.tmp')
        temporary.write_bytes(b'interrupted')
        status, result = self.prepare()
        self.assertEqual(status, 0)
        self.assertEqual((result.written, result.reused), (1, 4))
        self.assertEqual(path.read_bytes(), self.payloads[0])
        self.assertFalse(temporary.exists())

    def test_changed_bundle_uses_new_cache_without_touching_old_or_saves(self):
        self.assertEqual(self.prepare()[0], 0)
        before = self.path(1)
        save = self.root / 'PSP/SAVEDATA/JGR_PORTABLE'
        save.mkdir(parents=True)
        (save / 'progress.sav').write_bytes(b'keep')
        self.inputs[0].write_bytes(prx(99))
        changed, _ = bundle.build(*self.inputs)
        (self.game / 'EBOOT.PBP').write_bytes(pbp(changed))
        self.assertEqual(self.prepare()[0], 0)
        self.assertNotEqual(self.path(1), before)
        self.assertEqual(before.read_bytes(), self.payloads[0])
        self.assertEqual((save / 'progress.sav').read_bytes(), b'keep')

    def test_invalid_bundle_is_rejected_before_cache_creation(self):
        cases = [self.data[:-1], self.data + b'\0', b'BADMAGIC' + self.data[8:],
                 self.data[:8] + struct.pack('<I', 1) + self.data[12:],
                 index_change(self.data, 12, 3), index_change(self.data, 12, 4),
                 index_change(self.data, 24, 4), index_change(self.data, 28, 0),
                 index_change(self.data, 32, 0xffffffff),
                 self.data[:-1] + bytes([self.data[-1] ^ 1])]
        for index, data in enumerate(cases):
            with self.subTest(index=index):
                game = self.root / ('bad' + str(index))
                game.mkdir()
                (game / 'EBOOT.PBP').write_bytes(pbp(data))
                self.assertLess(self.prepare(game)[0], 0)
                self.assertFalse((game / 'data').exists())
                self.assertLess(self.lib.portable_bundle_path(1, C.create_string_buffer(768), 768), 0)
                with self.assertRaises(ValueError):
                    bundle.validate(data)

    def test_pbp_offsets_and_truncation_are_rejected(self):
        good = pbp(self.data)
        for index, data in enumerate((good[:39], b'BAD!' + good[4:],
                                      good[:36] + struct.pack('<I', 0xffffffff) + good[40:],
                                      good[:8] + struct.pack('<I', 0) + good[12:])):
            game = self.root / ('pbp' + str(index))
            game.mkdir()
            (game / 'EBOOT.PBP').write_bytes(data)
            self.assertLess(self.prepare(game)[0], 0)

    def test_stream_is_bounded_and_checks_complete_read(self):
        self.assertEqual(self.prepare()[0], 0)
        stream = Stream()
        self.assertEqual(self.lib.portable_bundle_open_entry(4, C.byref(stream)), 0)
        output = C.create_string_buffer(len(self.payloads[3]) + 100)
        count = self.lib.portable_bundle_read(C.byref(stream), output, len(output))
        self.assertEqual(count, len(self.payloads[3]))
        self.assertEqual(output.raw[:count], self.payloads[3])
        self.assertEqual(self.lib.portable_bundle_read(C.byref(stream), output, 10), 0)
        self.assertEqual(self.lib.portable_bundle_close(C.byref(stream)), 0)
        self.assertEqual(self.lib.portable_bundle_open_entry(4, C.byref(stream)), 0)
        self.assertLess(self.lib.portable_bundle_close(C.byref(stream)), 0)

    def test_stream_detects_payload_changed_after_preparation(self):
        self.assertEqual(self.prepare()[0], 0)
        changed = bytearray(pbp(self.data));changed[-len(self.payloads[4])-1] ^= 1
        (self.game / 'EBOOT.PBP').write_bytes(changed)
        stream = Stream();output = C.create_string_buffer(len(self.payloads[3]))
        self.assertEqual(self.lib.portable_bundle_open_entry(4, C.byref(stream)), 0)
        self.assertEqual(self.lib.portable_bundle_read(C.byref(stream), output, len(output)), len(output))
        self.assertEqual(self.lib.portable_bundle_close(C.byref(stream)), -3)

    def test_short_path_buffer_and_unknown_role_are_rejected(self):
        self.assertEqual(self.prepare()[0], 0)
        self.assertLess(self.lib.portable_bundle_path(1, C.create_string_buffer(2), 2), 0)
        self.assertLess(self.lib.portable_bundle_path(6, C.create_string_buffer(768), 768), 0)
        self.assertLess(self.lib.portable_bundle_open_entry(6, C.byref(Stream())), 0)

    def test_symlink_cache_and_unwritable_parent_cannot_publish_paths(self):
        outside = self.root / 'outside';outside.mkdir()
        (self.game / 'data').symlink_to(outside, target_is_directory=True)
        self.assertLess(self.prepare()[0], 0)
        self.assertEqual(list(outside.iterdir()), [])
        (self.game / 'data').unlink();(self.game / 'data').write_bytes(b'not a directory')
        self.assertLess(self.prepare()[0], 0)

    def test_bad_module_and_oversize_inputs_rejected(self):
        self.inputs[0].write_bytes(b'not executable')
        with self.assertRaisesRegex(ValueError, 'PRX'):
            bundle.build(*self.inputs)
        self.inputs[0].write_bytes(prx(17))
        self.inputs[3].write_bytes(b'x' * (bundle.LIMITS[3] + 1))
        with self.assertRaisesRegex(ValueError, 'limit'):
            bundle.build(*self.inputs)

    def test_cached_launch_reads_only_index_receipt_and_preserves_files(self):
        game = self.root / 'setup.prx';game.write_bytes(prx(61) + bytes(400000))
        count = 5
        data, _ = bundle.build(*self.inputs[:4], converter=game)
        (self.game / 'EBOOT.PBP').write_bytes(pbp(data))
        self.assertEqual(self.prepare()[0], 0)
        paths = [self.path(role) for role in range(1, count + 1)]
        receipt = paths[0].parent / 'bundle.checked'
        expected = b'JGRCCH1\0' + pbp(data)[:40 + 24 + count * 16]
        self.assertEqual(receipt.read_bytes(), expected)
        files = [self.game / 'EBOOT.PBP', receipt, *paths]
        before = [(path.stat().st_mtime_ns, path.read_bytes()) for path in files]
        self.lib.test_reset_io()
        status, result = self.try_cached()
        self.assertEqual((status, result.written, result.reused, result.entries), (0, 0, count, count))
        self.assertEqual(self.lib.test_read_bytes(), 2 * (40 + 24 + count * 16) + 8)
        self.assertEqual(self.lib.test_write_bytes(), 0)
        self.assertEqual([(path.stat().st_mtime_ns, path.read_bytes()) for path in files], before)
        self.assertEqual([self.path(role) for role in range(1, count + 1)], paths)

    def test_missing_old_receipt_or_temporary_is_not_a_completed_cache(self):
        self.lib.test_reset_io()
        self.assertLess(self.try_cached()[0], 0)
        self.assertFalse((self.game / 'data').exists())
        self.assertEqual(self.lib.test_write_bytes(), 0)
        self.assert_unavailable()
        self.assertEqual(self.prepare()[0], 0)
        receipt = self.path(1).parent / 'bundle.checked'
        receipt.rename(receipt.with_name(receipt.name + '.tmp'))
        self.assertLess(self.try_cached()[0], 0)
        self.assert_unavailable()
        self.assertEqual(self.prepare()[0], 0)
        self.assertTrue(receipt.is_file())
        self.assertFalse(receipt.with_name(receipt.name + '.tmp').exists())
        self.assertEqual(self.try_cached()[0], 0)

    def test_receipt_damage_truncation_and_trailing_bytes_reject_cached_launch(self):
        self.assertEqual(self.prepare()[0], 0)
        receipt = self.path(1).parent / 'bundle.checked';good = receipt.read_bytes()
        damaged = [good[:n] for n in (0, 7, 8, 20, len(good) - 1)] + [good + b'\0']
        damaged += [good[:i] + bytes([good[i] ^ 1]) + good[i + 1:] for i in range(len(good))]
        for index, body in enumerate(damaged):
            with self.subTest(index=index):
                receipt.write_bytes(body)
                self.lib.test_reset_io()
                self.assertLess(self.try_cached()[0], 0)
                self.assertEqual(self.lib.test_write_bytes(), 0)
                self.assert_unavailable()
        receipt.write_bytes(good)
        self.assertEqual(self.try_cached()[0], 0)

    def test_every_missing_resized_or_nonregular_entry_forces_full_repair(self):
        game = self.root / 'setup.prx';game.write_bytes(prx(61))
        data, _ = bundle.build(*self.inputs[:4], converter=game)
        (self.game / 'EBOOT.PBP').write_bytes(pbp(data))
        self.assertEqual(self.prepare()[0], 0)
        for role in range(1, 6):
            for fault in ('missing', 'short', 'long', 'directory', 'symlink'):
                with self.subTest(role=role, fault=fault):
                    path = self.path(role);good = path.read_bytes();path.unlink()
                    if fault == 'short':path.write_bytes(good[:-1])
                    elif fault == 'long':path.write_bytes(good + b'\0')
                    elif fault == 'directory':path.mkdir()
                    elif fault == 'symlink':path.symlink_to(game)
                    self.lib.test_reset_io()
                    self.assertLess(self.try_cached()[0], 0)
                    self.assertEqual(self.lib.test_write_bytes(), 0)
                    self.assert_unavailable()
                    if fault == 'directory':path.rmdir()
                    elif fault == 'symlink':path.unlink()
                    self.assertEqual(self.prepare()[0], 0)
                    self.assertEqual(path.read_bytes(), good)
                    self.assertEqual(self.try_cached()[0], 0)

    def test_updated_index_and_wrong_receipt_never_select_old_executable(self):
        self.assertEqual(self.prepare()[0], 0)
        old_path = self.path(1);old_receipt = (old_path.parent / 'bundle.checked').read_bytes()
        self.inputs[0].write_bytes(prx(99));data, _ = bundle.build(*self.inputs)
        (self.game / 'EBOOT.PBP').write_bytes(pbp(data))
        self.assertLess(self.try_cached()[0], 0)
        self.assert_unavailable()
        self.assertEqual(self.prepare()[0], 0)
        new_path = self.path(1);receipt = new_path.parent / 'bundle.checked'
        self.assertNotEqual(new_path, old_path)
        good = receipt.read_bytes();receipt.write_bytes(old_receipt)
        self.assertLess(self.try_cached()[0], 0)
        self.assert_unavailable()
        receipt.write_bytes(good)
        self.assertEqual(self.try_cached()[0], 0)
        self.assertEqual(old_path.read_bytes(), self.payloads[0])

    def test_cached_launch_keeps_full_prepare_and_stream_damage_checks(self):
        self.assertEqual(self.prepare()[0], 0)
        path = self.path(1);bad = bytearray(path.read_bytes());bad[-1] ^= 1;path.write_bytes(bad)
        self.assertEqual(self.try_cached()[0], 0)  # Size-only contract is explicit.
        status, result = self.prepare()
        self.assertEqual((status, result.written, result.reused), (0, 1, 4))
        self.assertEqual(path.read_bytes(), self.payloads[0])
        changed = bytearray(pbp(self.data));changed[-len(self.payloads[4])-1] ^= 1
        (self.game / 'EBOOT.PBP').write_bytes(changed)
        self.assertEqual(self.try_cached()[0], 0)
        stream = Stream();output = C.create_string_buffer(len(self.payloads[3]))
        self.assertEqual(self.lib.portable_bundle_open_entry(4, C.byref(stream)), 0)
        self.assertEqual(self.lib.portable_bundle_read(C.byref(stream), output, len(output)), len(output))
        self.assertEqual(self.lib.portable_bundle_close(C.byref(stream)), -3)
        self.assertEqual(self.prepare()[0], -3)
        self.assert_unavailable()

    def test_failed_receipt_publish_does_not_leave_a_completed_cache(self):
        self.assertEqual(self.prepare()[0], 0)
        receipt = self.path(1).parent / 'bundle.checked'
        self.lib.test_fail_receipt_write()
        self.assertLess(self.prepare()[0], 0)
        self.lib.test_reset_io()
        self.assertFalse(receipt.exists())
        self.assertFalse(receipt.with_name(receipt.name + '.tmp').exists())
        self.assert_unavailable()
        self.assertLess(self.try_cached()[0], 0)
        self.assertEqual(self.prepare()[0], 0)
        self.assertEqual(self.try_cached()[0], 0)

    def test_failed_extraction_invalidates_previous_completion_before_mutation(self):
        self.assertEqual(self.prepare()[0], 0)
        path = self.path(2);receipt = path.parent / 'bundle.checked'
        path.unlink();path.mkdir();(path / 'blocker').write_bytes(b'keep')
        self.assertLess(self.prepare()[0], 0)
        self.assert_unavailable()
        self.assertFalse(receipt.exists())
        self.assertLess(self.try_cached()[0], 0)
        (path / 'blocker').unlink();path.rmdir()
        self.assertEqual(self.prepare()[0], 0)
        self.assertEqual(self.try_cached()[0], 0)

    def test_cached_path_rejects_ancestor_and_receipt_symlinks_without_writes(self):
        self.assertEqual(self.prepare()[0], 0)
        receipt = self.path(1).parent / 'bundle.checked'
        outside = self.root / 'outside';receipt.rename(outside);receipt.symlink_to(outside)
        self.lib.test_reset_io()
        self.assertLess(self.try_cached()[0], 0)
        self.assertEqual(self.lib.test_write_bytes(), 0)
        self.assert_unavailable()
        receipt.unlink();outside.rename(receipt)
        data = self.game / 'data';data.rename(outside);data.symlink_to(outside, target_is_directory=True)
        self.assertLess(self.try_cached()[0], 0)
        self.assertEqual(self.lib.test_write_bytes(), 0)
        self.assert_unavailable()

    def test_cached_launch_malformed_or_missing_eboot_clears_previous_ready_state(self):
        self.assertEqual(self.prepare()[0], 0)
        good = pbp(self.data)
        damaged = (good[:39], b'BAD!' + good[4:], good[:-1],
                   good[:36] + struct.pack('<I', 0xffffffff) + good[40:],
                   pbp(index_change(self.data, 24, 4)))
        for index, body in enumerate(damaged):
            with self.subTest(index=index):
                (self.game / 'EBOOT.PBP').write_bytes(body)
                self.lib.test_reset_io()
                self.assertLess(self.try_cached()[0], 0)
                self.assertEqual(self.lib.test_write_bytes(), 0)
                self.assert_unavailable()
                (self.game / 'EBOOT.PBP').write_bytes(good)
                self.assertEqual(self.try_cached()[0], 0)
        (self.game / 'EBOOT.PBP').unlink()
        self.assertLess(self.try_cached()[0], 0)
        self.assert_unavailable()


if __name__ == '__main__':
    unittest.main()
