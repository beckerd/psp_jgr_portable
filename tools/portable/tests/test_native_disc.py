"""Native reader tests use synthetic sectors; real-disc parity is opt-in.

JGR_TEST_GDI points at the developer's supported copy. No media is embedded.
PSP_CC enables the additional PSP/newlib compilation check.
"""
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/portable'))
from disc import Disc


class File(C.Structure):
    _fields_ = [('offset', C.c_uint64), ('size', C.c_uint32)]


class CacheStats(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in (
        'afs_validations', 'afs_rows', 'afs_hits', 'find_hits',
        'afs_cache_bytes', 'afs_cache_limit', 'afs_cache_entries', 'find_cache_entries')]


class ReadStats(C.Structure):
    _fields_ = [('physical_reads', C.c_uint32), ('sequential_reads', C.c_uint32),
                ('physical_bytes', C.c_uint64)]


class Sha(C.Structure):
    _fields_ = [('state', C.c_uint32 * 8), ('bytes', C.c_uint64),
                ('used', C.c_uint), ('block', C.c_ubyte * 64)]


def both32(data, off, value):
    struct.pack_into('<I', data, off, value)
    struct.pack_into('>I', data, off + 4, value)


def record(name, lba, size, flags=0):
    data = bytearray(33 + len(name) + (len(name) % 2 == 0))
    data[0] = len(data)
    both32(data, 2, lba)
    both32(data, 10, size)
    data[25] = flags
    data[28:32] = b'\x01\x00\x00\x01'
    data[32] = len(name)
    data[33:33 + len(name)] = name
    return data


def fixture(folder, raw=False, mutate=None, prefix=0):
    data = bytearray(100 * 2048)
    data[:16] = b'SEGA SEGAKATANA '
    data[64:80] = b'MK-51058  V1.005'
    p = 16 * 2048
    data[p:p + 7] = b'\x01CD001\x01'
    data[p + 128:p + 132] = b'\x00\x08\x08\x00'
    data[p + 156:p + 190] = record(b'\0', 45018, 2048, 2)
    payload = bytes((i * 131 + i // 7) & 255 for i in range(90007))
    entries = (record(b'\0', 45018, 2048, 2) + record(b'\1', 45018, 2048, 2)
               + record(b'1ST_READ.BIN;1', 45020, 7)
               + record(b'BIG.BIN;1', 45021, len(payload))
               + record(b'JETRADIO', 45019, 2048, 2)
               + record(b'EMPTY.BIN;1', 45100, 0))
    data[18 * 2048:18 * 2048 + len(entries)] = entries
    entries = (record(b'\0', 45019, 2048, 2) + record(b'\1', 45018, 2048, 2)
               + record(b'SOUND.AFS;1', 45070, 5000))
    data[19 * 2048:19 * 2048 + len(entries)] = entries
    data[20 * 2048:20 * 2048 + 7] = b'fixture'
    data[21 * 2048:21 * 2048 + len(payload)] = payload
    a = 70 * 2048
    data[a:a + 4] = b'AFS\0'
    struct.pack_into('<7I', data, a + 4, 2, 2048, 1900, 4000, 23, 1024, 96)
    data[a + 1024:a + 1033] = b'voice.adx'
    data[a + 1072:a + 1079] = b'cue.bin'
    data[a + 2048:a + 3948] = payload[:1900]
    data[a + 4000:a + 4023] = payload[-23:]
    if mutate:
        mutate(data)
    if raw:
        data = b''.join(b'\0' + b'\xff' * 10 + b'\0\0\0\0\1'
                        + data[i:i + 2048] + bytes(288)
                        for i in range(0, len(data), 2048))
    (folder / 'track data.bin').write_bytes(bytes(prefix) + data)
    gdi = folder / 'game.gdi'
    gdi.write_text(f'1\n1 45000 4 {2352 if raw else 2048} "track data.bin" {prefix}\n')
    return gdi, payload


class NativeDiscTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-native-disc-')
        cls.library = Path(cls.temp.name) / 'disc.so'
        compiler = os.environ.get('CC', 'cc')
        subprocess.run([compiler, '-std=c99', '-D_XOPEN_SOURCE=700', '-Wall', '-Wextra',
                        '-Werror', '-DJGR_DISC_TESTING', '-DJGR_PORTABLE_ASSETS=1',
                        '-include', str(ROOT / 'psp/src/portable_assets.h'), '-fPIC', '-shared',
                        str(ROOT / 'psp/src/portable_disc.c'), '-o', str(cls.library)], check=True)
        cls.lib = C.CDLL(str(cls.library))
        cls.lib.jgr_disc_open.argtypes = [C.POINTER(C.c_void_p), C.c_char_p, C.c_char_p, C.c_size_t]
        cls.lib.jgr_disc_open_fixture.argtypes = cls.lib.jgr_disc_open.argtypes
        cls.lib.jgr_disc_close.argtypes = [C.c_void_p]
        cls.lib.jgr_disc_error.argtypes = [C.c_void_p]
        cls.lib.jgr_disc_error.restype = C.c_char_p
        for name in ('jgr_disc_find', 'jgr_disc_source'):
            getattr(cls.lib, name).argtypes = [C.c_void_p, C.c_char_p, C.POINTER(File)]
        cls.lib.jgr_disc_afs.argtypes = [C.c_void_p, C.POINTER(File), C.c_uint, C.POINTER(File)]
        cls.lib.jgr_disc_afs_named.argtypes = cls.lib.jgr_disc_afs.argtypes + [C.c_char_p]
        cls.lib.jgr_disc_cache_stats.argtypes = [C.c_void_p, C.POINTER(CacheStats)]
        cls.lib.jgr_disc_read.argtypes = [C.c_void_p, C.POINTER(File), C.c_uint32, C.c_void_p, C.c_size_t]
        cls.lib.jgr_disc_read_sequential.argtypes = cls.lib.jgr_disc_read.argtypes + [C.c_size_t]
        cls.lib.jgr_disc_read_stats.argtypes = [C.c_void_p, C.POINTER(ReadStats)]
        cls.lib.jgr_disc_sector_bounds.argtypes = [C.c_void_p, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)]
        cls.lib.jgr_disc_sectors.argtypes = [C.c_void_p, C.c_uint32, C.c_uint, C.c_void_p, C.c_size_t]
        cls.lib.jgr_sha256_init.argtypes = [C.POINTER(Sha)]
        cls.lib.jgr_sha256_update.argtypes = [C.POINTER(Sha), C.c_void_p, C.c_size_t]
        cls.lib.jgr_sha256_final.argtypes = [C.POINTER(Sha), C.c_void_p]
        cls.lib.jgr_sha256_hex.argtypes = [C.c_void_p, C.c_char_p]
        cls.callback = C.CFUNCTYPE(C.c_int, C.c_char_p, C.POINTER(File), C.c_void_p)
        cls.lib.jgr_disc_visit.argtypes = [C.c_void_p, cls.callback, C.c_void_p]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def open(self, path, strict=False):
        handle = C.c_void_p()
        error = C.create_string_buffer(192)
        fn = self.lib.jgr_disc_open if strict else self.lib.jgr_disc_open_fixture
        status = fn(C.byref(handle), os.fsencode(path), error, len(error))
        self.assertEqual(status, 0, error.value.decode())
        self.addCleanup(self.lib.jgr_disc_close, handle)
        return handle

    def rejected(self, path, strict=False):
        handle = C.c_void_p()
        error = C.create_string_buffer(192)
        fn = self.lib.jgr_disc_open if strict else self.lib.jgr_disc_open_fixture
        self.assertLess(fn(C.byref(handle), os.fsencode(path), error, len(error)), 0)
        self.assertFalse(handle.value)
        self.assertTrue(error.value)
        return error.value.decode()

    def source(self, handle, name):
        file = File()
        self.assertEqual(self.lib.jgr_disc_source(handle, name.encode(), C.byref(file)), 0,
                         self.lib.jgr_disc_error(handle))
        return file

    def read(self, handle, file, offset=0, size=None):
        size = file.size - offset if size is None else size
        output = C.create_string_buffer(max(1, size))
        self.assertEqual(self.lib.jgr_disc_read(handle, C.byref(file), offset, output, size), 0,
                         self.lib.jgr_disc_error(handle))
        return output.raw[:size]

    def stats(self, handle):
        result = CacheStats()
        self.lib.jgr_disc_cache_stats(handle, C.byref(result))
        return result

    def afs(self, handle, archive, index):
        result = File()
        self.assertEqual(self.lib.jgr_disc_afs(handle, C.byref(archive), index, C.byref(result)), 0,
                         self.lib.jgr_disc_error(handle))
        return result

    def test_archive_index_and_iso_path_reuse_raw_and_cooked(self):
        for raw in (False, True):
            with self.subTest(raw=raw), tempfile.TemporaryDirectory() as tmp:
                gdi, payload = fixture(Path(tmp), raw=raw)
                handle = self.open(gdi)
                for index in range(200):
                    name, expected = (('000_voice.adx', payload[:1900]) if index % 2 == 0
                                      else ('001_cue.bin', payload[-23:]))
                    entry = self.source(handle, 'decompile/afs/SOUND/' + name)
                    self.assertEqual(self.read(handle, entry), expected)
                stats = self.stats(handle)
                self.assertEqual((stats.afs_validations, stats.afs_rows, stats.afs_hits), (1, 2, 199))
                self.assertEqual((stats.find_hits, stats.find_cache_entries), (199, 1))
                self.assertEqual((stats.afs_cache_bytes, stats.afs_cache_entries), (16, 1))
                # A successful cached lookup never makes an invalid path valid.
                for path in (b'JETRADIO/../SOUND.AFS', b'JETRADIO/SOUND.AFS/', b'JETRADIO'):
                    output = File()
                    self.assertLess(self.lib.jgr_disc_find(handle, path, C.byref(output)), 0)

    def test_unrequested_bad_extent_never_publishes_partial_index(self):
        for raw in (False, True):
            with self.subTest(raw=raw), tempfile.TemporaryDirectory() as tmp:
                def bad_last(data):
                    struct.pack_into('<II', data, 70 * 2048 + 16, 4999, 2)
                gdi, _ = fixture(Path(tmp), raw=raw, mutate=bad_last)
                handle = self.open(gdi)
                archive = self.source(handle, 'decompile/extracted/JETRADIO/SOUND.AFS')
                for _ in range(2):
                    output = File()
                    self.assertLess(self.lib.jgr_disc_afs(handle, C.byref(archive), 0, C.byref(output)), 0)
                    self.assertIn(b'extent', self.lib.jgr_disc_error(handle))
                stats = self.stats(handle)
                self.assertEqual((stats.afs_validations, stats.afs_rows), (2, 4))
                self.assertEqual((stats.afs_cache_bytes, stats.afs_cache_entries, stats.afs_hits), (0, 0, 0))

    def test_cached_index_still_checks_names_indices_and_archive_bounds(self):
        for bad_name in (b'../voice.adx', b'bad/name.adx', b'bad\\name.adx', b'bad\x01.adx'):
            with self.subTest(name=bad_name), tempfile.TemporaryDirectory() as tmp:
                def rename(data):
                    data[70 * 2048 + 1024:70 * 2048 + 1056] = bad_name.ljust(32, b'\0')
                gdi, _ = fixture(Path(tmp), mutate=rename)
                handle = self.open(gdi)
                archive = self.source(handle, 'decompile/extracted/JETRADIO/SOUND.AFS')
                self.assertEqual(self.afs(handle, archive, 0).size, 1900)
                result = File(); name = C.create_string_buffer(33)
                for _ in range(2):
                    self.assertLess(self.lib.jgr_disc_afs_named(handle, C.byref(archive), 0, C.byref(result), name), 0)
                    self.assertIn(b'Unsafe AFS file name', self.lib.jgr_disc_error(handle))
                self.assertLess(self.lib.jgr_disc_afs(handle, C.byref(archive), 2, C.byref(result)), 0)
                # The same offset with a different declared extent must miss the cache.
                smaller = File(archive.offset, 4022)
                self.assertLess(self.lib.jgr_disc_afs(handle, C.byref(smaller), 0, C.byref(result)), 0)
                self.assertIn(b'extent', self.lib.jgr_disc_error(handle))
                self.assertEqual(self.stats(handle).afs_cache_entries, 1)
                self.assertEqual(self.afs(handle, archive, 1).size, 23)

    def test_archive_lru_slot_and_memory_limits_with_streaming_fallback(self):
        for raw in (False, True):
            with self.subTest(raw=raw), tempfile.TemporaryDirectory() as tmp:
                def archives(data):
                    # Eighteen tiny indexes exhaust the slot limit independently
                    # of the byte limit. Payloads are unique for alias detection.
                    for index in range(18):
                        start = (72 + index) * 2048
                        data[start:start + 8] = b'AFS\0' + struct.pack('<I', 1)
                        struct.pack_into('<II', data, start + 8, 32, 1)
                        data[start + 32] = index
                gdi, _ = fixture(Path(tmp), raw=raw, mutate=archives)
                handle = self.open(gdi)
                for index in range(18):
                    entry = self.afs(handle, File((72 + index) * 2048, 1024), 0)
                    self.assertEqual(self.read(handle, entry), bytes([index]))
                self.assertEqual(self.stats(handle).afs_cache_entries, 16)
                self.afs(handle, File(89 * 2048, 1024), 0)
                self.afs(handle, File(72 * 2048, 1024), 0)
                stats = self.stats(handle)
                self.assertEqual((stats.afs_validations, stats.afs_hits, stats.afs_cache_bytes), (19, 1, 128))
            with self.subTest(raw=raw, budget=True), tempfile.TemporaryDirectory() as tmp:
                def large_archives(data):
                    for sector, count in ((21, 1500), (42, 1500), (70, 2050)):
                        start = sector * 2048
                        data[start:start + 8] = b'AFS\0' + struct.pack('<I', count)
                        data[start + 8:start + 8 + count * 8] = struct.pack('<II', 19999, 1) * count
                        data[start + 19999] = sector
                gdi, _ = fixture(Path(tmp), raw=raw, mutate=large_archives)
                handle = self.open(gdi)
                for sector, count in ((21, 1500), (42, 1500), (21, 1500), (70, 2050), (70, 2050)):
                    entry = self.afs(handle, File(sector * 2048, 20000), count - 1)
                    self.assertEqual(self.read(handle, entry), bytes([sector]))
                    stats = self.stats(handle)
                    self.assertLessEqual(stats.afs_cache_bytes, stats.afs_cache_limit)
                # Large indexes remain valid but are revalidated on each request;
                # fallback does not evict the smaller last-used resident index.
                self.afs(handle, File(21 * 2048, 20000), 0)
                stats = self.stats(handle)
                self.assertEqual((stats.afs_validations, stats.afs_rows, stats.afs_hits), (5, 8600, 1))
                self.assertEqual((stats.afs_cache_bytes, stats.afs_cache_entries), (12000, 1))

    def test_successful_path_cache_evicts_and_leaves_long_paths_supported(self):
        with tempfile.TemporaryDirectory() as tmp:
            long_name = b'L' * 180 + b'.BIN'
            def directory(data):
                rows = (record(b'\0', 45018, 2048, 2) + record(b'\1', 45018, 2048, 2)
                        + b''.join(record(f'F{index:02}.BIN;1'.encode(), 45020, 7) for index in range(18))
                        + record(long_name + b';1', 45020, 7))
                data[18 * 2048:19 * 2048] = rows.ljust(2048, b'\0')
            gdi, _ = fixture(Path(tmp), mutate=directory)
            handle = self.open(gdi)
            for index in range(18):
                self.assertEqual(self.source(handle, f'decompile/extracted/F{index:02}.BIN').size, 7)
            self.assertEqual(self.stats(handle).find_cache_entries, 16)
            self.source(handle, 'decompile/extracted/F17.BIN')
            self.source(handle, 'decompile/extracted/F00.BIN')
            self.assertEqual(self.stats(handle).find_hits, 1)
            for _ in range(2):
                self.assertEqual(self.source(handle, 'decompile/extracted/' + long_name.decode()).size, 7)
            self.assertEqual((self.stats(handle).find_hits, self.stats(handle).find_cache_entries), (1, 16))

    def test_index_allocation_failure_retains_complete_validation(self):
        # Inject malloc failure only for this independent reader instance. The
        # production reader has no fault switches or relaxed validation mode.
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp); shim = folder / 'allocation.c'; library = folder / 'failure.so'
            shim.write_text("#undef malloc\n#include <stdlib.h>\n"
                            "static int fail;\nvoid fail_allocations(int enabled){fail=enabled;}\n"
                            "void *jgr_disc_test_malloc(size_t n){return fail ? NULL : malloc(n); }\n")
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-D_XOPEN_SOURCE=700',
                            '-Wall', '-Wextra', '-Werror', '-DJGR_DISC_TESTING',
                            '-Dmalloc=jgr_disc_test_malloc', '-fPIC', '-shared',
                            str(ROOT / 'psp/src/portable_disc.c'), str(shim), '-o', str(library)], check=True)
            lib = C.CDLL(str(library))
            lib.jgr_disc_open_fixture.argtypes = self.lib.jgr_disc_open_fixture.argtypes
            lib.jgr_disc_source.argtypes = self.lib.jgr_disc_source.argtypes
            lib.jgr_disc_cache_stats.argtypes = self.lib.jgr_disc_cache_stats.argtypes
            lib.jgr_disc_close.argtypes = [C.c_void_p]
            lib.fail_allocations.argtypes = [C.c_int]
            for malformed in (False, True):
                with self.subTest(malformed=malformed):
                    def mutate(data):
                        if malformed:
                            struct.pack_into('<II', data, 70 * 2048 + 16, 4999, 2)
                    gdi, _ = fixture(folder, raw=True, mutate=mutate)
                    handle = C.c_void_p(); error = C.create_string_buffer(192)
                    lib.fail_allocations(0)
                    self.assertEqual(lib.jgr_disc_open_fixture(C.byref(handle), os.fsencode(gdi), error, len(error)), 0)
                    try:
                        lib.fail_allocations(1)
                        for _ in range(2):
                            output = File()
                            status = lib.jgr_disc_source(handle, b'decompile/afs/SOUND/000_voice.adx', C.byref(output))
                            self.assertEqual(status, -1 if malformed else 0)
                            if not malformed:
                                self.assertEqual(output.size, 1900)
                        stats = CacheStats(); lib.jgr_disc_cache_stats(handle, C.byref(stats))
                        self.assertEqual((stats.afs_validations, stats.afs_rows, stats.afs_cache_bytes), (2, 4, 0))
                        lib.fail_allocations(0)
                        status = lib.jgr_disc_source(handle, b'decompile/afs/SOUND/000_voice.adx', C.byref(output))
                        self.assertEqual(status, -1 if malformed else 0)
                        lib.jgr_disc_cache_stats(handle, C.byref(stats))
                        self.assertEqual(stats.afs_cache_entries, 0 if malformed else 1)
                    finally:
                        lib.jgr_disc_close(handle)

    def test_archive_cache_is_owned_by_each_open_disc(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            original, _ = fixture(Path(first))
            def different(data):
                struct.pack_into('<II', data, 70 * 2048 + 8, 4000, 7)
            alternate, _ = fixture(Path(second), mutate=different)
            one, two = self.open(original), self.open(alternate)
            for handle, expected in ((one, 1900), (two, 7), (one, 1900), (two, 7)):
                self.assertEqual(self.source(handle, 'decompile/afs/SOUND/000_voice.adx').size, expected)
                self.assertEqual(self.stats(handle).afs_validations, 1)
            # A subsequent open also starts empty, even with identical extents.
            three = self.open(alternate)
            self.assertEqual(self.stats(three).afs_cache_entries, 0)
            self.assertEqual(self.source(three, 'decompile/afs/SOUND/000_voice.adx').size, 7)
            self.assertEqual(self.stats(three).afs_hits, 0)

    def read_stats(self, handle):
        result = ReadStats()
        self.lib.jgr_disc_read_stats(handle, C.byref(result))
        return result

    def sequential(self, handle, file, offset, size, capacity=513 * 2352):
        # Guard both sides, including the raw workspace beyond returned payload.
        output = C.create_string_buffer(b'!' * (capacity + 16), capacity + 16)
        before = self.read_stats(handle)
        self.assertEqual(self.lib.jgr_disc_read_sequential(handle, C.byref(file), offset,
                         C.byref(output, 8), size, capacity), 0, self.lib.jgr_disc_error(handle))
        after = self.read_stats(handle)
        self.assertEqual(output.raw[:8] + output.raw[-8:], b'!' * 16)
        return output.raw[8:8 + size], (after.physical_reads - before.physical_reads,
                after.sequential_reads - before.sequential_reads,
                after.physical_bytes - before.physical_bytes)

    @staticmethod
    def large_sequential_fixture(folder, raw):
        start = 128 * 2048 + 17
        payload = bytes((i * 197 + i // 11) & 255 for i in range(1024 * 1024 + 32791))
        def extend(data):
            data.extend(bytes(768 * 2048 - len(data)))
            data[start:start + len(payload)] = payload
        gdi, _ = fixture(folder, raw=raw, prefix=123, mutate=extend)
        return gdi, File(start, len(payload)), payload

    def test_sequential_raw_cooked_unaligned_and_maximum_batch(self):
        for raw in (False, True):
            with self.subTest(raw=raw), tempfile.TemporaryDirectory() as tmp:
                gdi, file, payload = self.large_sequential_fixture(Path(tmp), raw)
                handle = self.open(gdi)
                # offset2030 starts at the last byte of a raw sector, requiring
                # exactly513 sectors of workspace for one MiB of payload.
                for offset, size in ((0, 0), (0, 1), (1, 2048), (2030, 1024 * 1024),
                                     (0, 1024 * 1024), (17, 131072), (4095, 131079),
                                     (len(payload) - 7, 7), (len(payload), 0)):
                    with self.subTest(offset=offset, size=size):
                        result, stats = self.sequential(handle, file, offset, size)
                        self.assertEqual(result, payload[offset:offset + size])
                        physical = (((file.offset + offset) % 2048 + size + 2047) // 2048) * 2352 if raw else size
                        self.assertEqual(stats, (1, 1, physical) if size else (0, 0, 0))
                        self.assertEqual(self.read(handle, file, offset, min(size, 97)), payload[offset:offset + min(size, 97)])
                # Read to the very end of the physical track, without rounding
                # past its final sector or requiring a following sector.
                whole = File(0, 768 * 2048)
                result, stats = self.sequential(handle, whole, whole.size - 3, 3)
                self.assertEqual(result, bytes(3))
                self.assertEqual(stats, (1, 1, 2352 if raw else 3))

    def test_sequential_workspace_fallback_and_input_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            gdi, file, payload = self.large_sequential_fixture(Path(tmp), True)
            handle = self.open(gdi)
            size = 1024 * 1024
            result, stats = self.sequential(handle, file, 0, size, capacity=size)
            self.assertEqual(result, payload[:size])
            self.assertEqual(stats[1], 0)
            self.assertGreater(stats[0], 1)
            result, stats = self.sequential(handle, file, 0, size + 1, capacity=513 * 2352)
            self.assertEqual(result, payload[:size + 1])
            self.assertEqual(stats[1], 0)
            output = C.create_string_buffer(32)
            invalid = ((file, 0, 33, 32), (file, file.size, 1, 32),
                       (file, file.size + 1, 0, 32), (file, 0xffffffff, 1, 32),
                       (File(0xfffffffffffffff0, 30), 0, 0, 32),
                       (File(768 * 2048 - 1, 2), 0, 1, 32))
            for source, offset, length, capacity in invalid:
                before = self.read_stats(handle)
                self.assertLess(self.lib.jgr_disc_read_sequential(handle, C.byref(source), offset,
                                output, length, capacity), 0)
                self.assertEqual(self.read_stats(handle).physical_reads, before.physical_reads)
            self.assertLess(self.lib.jgr_disc_read_sequential(handle, C.byref(file), 0, None, 1, 32), 0)
            self.assertEqual(self.lib.jgr_disc_read_sequential(handle, C.byref(file), file.size, None, 0, 0), 0)

    def test_sequential_validates_each_consumed_raw_header(self):
        # First, middle, and final headers must be checked even though none of
        # the header bytes is part of the requested cooked payload. A corrupt
        # following sector must not invalidate an otherwise valid short read.
        for sector, byte, value, rejected in ((128, 15, 2, True), (400, 0, 1, True),
                (640, 11, 1, True), (640, 6, 0, True), (641, 15, 2, False)):
            with self.subTest(sector=sector, byte=byte), tempfile.TemporaryDirectory() as tmp:
                folder = Path(tmp); gdi, file, payload = self.large_sequential_fixture(folder, True)
                track = folder / 'track data.bin'; data = bytearray(track.read_bytes())
                data[123 + sector * 2352 + byte] = value; track.write_bytes(data)
                handle = self.open(gdi); output = C.create_string_buffer(513 * 2352)
                status = self.lib.jgr_disc_read_sequential(handle, C.byref(file), 0, output, 1024 * 1024, len(output))
                self.assertEqual(status, -1 if rejected else 0)
                if not rejected:
                    self.assertEqual(output.raw[:1024 * 1024], payload[:1024 * 1024])

    def test_sha_streaming_padding_and_large_input(self):
        for length in (0, 1, 3, 55, 56, 63, 64, 65, 4097, 1000000):
            with self.subTest(length=length):
                data = bytes((i * 17 + 2) & 255 for i in range(length))
                state = Sha()
                self.lib.jgr_sha256_init(C.byref(state))
                for start in range(0, length, 37):
                    chunk = data[start:start + 37]
                    self.lib.jgr_sha256_update(C.byref(state), chunk, len(chunk))
                digest = C.create_string_buffer(32)
                self.lib.jgr_sha256_final(C.byref(state), digest)
                self.assertEqual(digest.raw, hashlib.sha256(data).digest())
                text = C.create_string_buffer(65)
                self.lib.jgr_sha256_hex(digest, text)
                self.assertEqual(text.value.decode(), hashlib.sha256(data).hexdigest())

    def test_sha_large_unaligned_blocks_after_partial_updates(self):
        data=bytes((i*197+i//9)&255 for i in range(1048613))
        backing=C.create_string_buffer(b'x'+data)
        for first in (0,1,63,64,65):
            with self.subTest(first=first):
                state=Sha();self.lib.jgr_sha256_init(C.byref(state))
                self.lib.jgr_sha256_update(C.byref(state),C.byref(backing,1),first)
                self.lib.jgr_sha256_update(C.byref(state),C.byref(backing,first+1),len(data)-first)
                digest=C.create_string_buffer(32);self.lib.jgr_sha256_final(C.byref(state),digest)
                self.assertEqual(digest.raw,hashlib.sha256(data).digest())

    def test_raw_cooked_slices_and_afs(self):
        for raw in (False, True):
            with self.subTest(raw=raw), tempfile.TemporaryDirectory() as tmp:
                gdi, data = fixture(Path(tmp), raw=raw, prefix=123)
                handle = self.open(gdi)
                file = self.source(handle, 'decompile/extracted/big.bin')
                self.assertEqual(self.read(handle, file), data)
                for offset, size in ((0, 0), (2047, 33001), (65535, 17017), (90006, 1), (90007, 0)):
                    self.assertEqual(self.read(handle, file, offset, size), data[offset:offset + size])
                for name, want in (('000_voice.adx', data[:1900]), ('001_cue.bin', data[-23:])):
                    entry = self.source(handle, 'decompile/afs/SOUND/' + name)
                    self.assertEqual(self.read(handle, entry), want)
                empty = self.source(handle, 'decompile/extracted/EMPTY.BIN')
                self.assertEqual(self.read(handle, empty), b'')
                for offset, length in ((90007, 1), (90008, 0), (0xffffffff, 2)):
                    self.assertLess(self.lib.jgr_disc_read(handle, C.byref(file), offset, None, length), 0)
                forged = File(0xfffffffffffffff0, 30)
                self.assertLess(self.lib.jgr_disc_read(handle, C.byref(forged), 0, None, 0), 0)

    def test_supported_revision_is_mandatory(self):
        with tempfile.TemporaryDirectory() as tmp:
            gdi, _ = fixture(Path(tmp))
            self.assertIn('executable', self.rejected(gdi, strict=True))
            track = Path(tmp) / 'track data.bin'
            data = bytearray(track.read_bytes())
            both32(data, 18 * 2048 + 68 + 10, 3075648)
            data.extend(bytes(((20 * 2048 + 3075648 + 2047) // 2048) * 2048 - len(data)))
            track.write_bytes(data)
            self.assertIn('SHA-256', self.rejected(gdi, strict=True))
            data[74:80] = b'V9.999'
            track.write_bytes(data)
            self.assertIn('revision', self.rejected(gdi, strict=True))

    def test_interleaved_sector_pages_and_eviction(self):
        # Mix all thirteen pages, partial final pages and cross-page requests.
        # More pages than cache ways ensures comparison also covers eviction.
        for raw in (False, True):
            with self.subTest(raw=raw), tempfile.TemporaryDirectory() as tmp:
                gdi, _ = fixture(Path(tmp), raw=raw, prefix=123)
                disk=(Path(tmp)/'track data.bin').read_bytes()[123:]
                cooked=(b''.join(disk[i+16:i+2064] for i in range(0,len(disk),2352))
                        if raw else disk)
                handle=self.open(gdi)
                order=(0,24,80,8,96,32,16,64,48,72,40,56,88,0,96,24,8)
                output=C.create_string_buffer(32*2048)
                for base in order*3:
                    for offset,count in ((0,min(8,100-base)),(3,1)):
                        sector=base+offset
                        self.assertEqual(self.lib.jgr_disc_sectors(handle,45000+sector,count,output,len(output)),0)
                        self.assertEqual(output.raw[:count*2048],cooked[sector*2048:(sector+count)*2048])
                for sector in (7,15,23,63,87,95):
                    self.assertEqual(self.lib.jgr_disc_sectors(handle,45000+sector,5,output,len(output)),0)
                    self.assertEqual(output.raw[:5*2048],cooked[sector*2048:(sector+5)*2048])

    def test_large_reads_then_unaligned_random_slices(self):
        # Cross a complete cache and the partial final track page, then mix
        # small requests with large reads from different starting offsets.
        for raw in (False, True):
            with self.subTest(raw=raw), tempfile.TemporaryDirectory() as tmp:
                folder=Path(tmp);gdi,_=fixture(folder,raw=raw,prefix=123)
                disk=(folder/'track data.bin').read_bytes()[123:]
                cooked=(b''.join(disk[i+16:i+2064] for i in range(0,len(disk),2352))
                        if raw else disk)
                handle=self.open(gdi);whole=File(0,len(cooked))
                for offset,size in ((0,len(cooked)),(1,131073),(19*2048+7,160000),
                                    (99*2048+11,2037),(0,33),(29*2048+2047,131073),
                                    (3,32768),(97*2048+2030,18)):
                    self.assertEqual(self.read(handle,whole,offset,size),cooked[offset:offset+size])

    def test_large_raw_read_checks_consumed_sector_headers(self):
        for sector,byte,value in ((30,15,2),(63,4,0)):
            with self.subTest(sector=sector), tempfile.TemporaryDirectory() as tmp:
                folder=Path(tmp);gdi,_=fixture(folder,raw=True,prefix=123)
                track=folder/'track data.bin';data=bytearray(track.read_bytes())
                data[123+sector*2352+byte]=value;track.write_bytes(data)
                handle=self.open(gdi);file=self.source(handle,'decompile/extracted/BIG.BIN')
                output=C.create_string_buffer(file.size)
                self.assertLess(self.lib.jgr_disc_read(handle,C.byref(file),0,output,file.size),0)
        # A prefetched sector outside the requested extent is not interpreted.
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);gdi,payload=fixture(folder,raw=True)
            track=folder/'track data.bin';data=bytearray(track.read_bytes())
            data[70*2352+15]=2;track.write_bytes(data)
            handle=self.open(gdi);file=self.source(handle,'decompile/extracted/BIG.BIN')
            self.assertEqual(self.read(handle,file),payload)

    def test_sector_bios_access_is_bounded_and_cooked(self):
        for raw in (False, True):
            with self.subTest(raw=raw), tempfile.TemporaryDirectory() as tmp:
                gdi, payload = fixture(Path(tmp), raw=raw, prefix=123)
                handle = self.open(gdi)
                start, count = C.c_uint32(), C.c_uint32()
                self.assertEqual(self.lib.jgr_disc_sector_bounds(handle, C.byref(start), C.byref(count)), 0)
                self.assertEqual((start.value, count.value), (45000, 100))
                data = C.create_string_buffer(65536)
                self.assertEqual(self.lib.jgr_disc_sectors(handle, 45000, 1, data, len(data)), 0)
                self.assertEqual(data.raw[:16], b'SEGA SEGAKATANA ')
                self.assertEqual(self.lib.jgr_disc_sectors(handle, 45021, 32, data, len(data)), 0)
                self.assertEqual(data.raw, payload[:65536])
                self.assertEqual(self.lib.jgr_disc_sectors(handle, 45099, 1, data, len(data)), 0)
                self.assertEqual(data.raw[:2048], bytes(2048))
                self.assertEqual(self.lib.jgr_disc_sectors(handle, 45100, 0, None, 0), 0)
                for lba, sectors, capacity in ((44999, 1, 65536), (45100, 1, 65536),
                                                (45101, 0, 65536), (0xffffffff, 2, 65536),
                                                (45000, 33, 65536), (45000, 0xffffffff, 65536),
                                                (45099, 2, 65536), (45021, 32, 65535)):
                    before = data.raw
                    self.assertLess(self.lib.jgr_disc_sectors(handle, lba, sectors, data, capacity), 0)
                    self.assertEqual(data.raw, before)
                self.assertLess(self.lib.jgr_disc_sectors(handle, 45000, 1, None, 2048), 0)

    def test_inventory_and_callback_abort(self):
        with tempfile.TemporaryDirectory() as tmp:
            gdi, _ = fixture(Path(tmp))
            handle = self.open(gdi)
            result = {}
            def collect(name, file, user):
                result[name.decode()] = (file.contents.offset, file.contents.size)
                return 0
            self.assertEqual(self.lib.jgr_disc_visit(handle, self.callback(collect), None), 0)
            self.assertEqual(set(result), {'1ST_READ.BIN', 'BIG.BIN', 'JETRADIO/SOUND.AFS', 'EMPTY.BIN'})
            self.assertLess(self.lib.jgr_disc_visit(handle, self.callback(lambda *args: 1), None), 0)

    def test_bad_iso_structures(self):
        def change_record(data, transform):
            transform(data, 18 * 2048 + 68)
        mutations = {
            'extent': lambda b: both32(b, 16 * 2048 + 158, 45100),
            'endian': lambda b: b.__setitem__(16 * 2048 + 158, 1),
            'cycle': lambda b: both32(b, 19 * 2048 + 68 + 2, 45018) or b.__setitem__(19 * 2048 + 68 + 25, 2),
            'unsafe': lambda b: change_record(b, lambda b, at: b.__setitem__(slice(at + 33, at + 45), b'../EVIL.BIN;1')),
            'embedded_nul': lambda b: change_record(b, lambda b, at: b.__setitem__(at + 34, 0)),
            'short': lambda b: change_record(b, lambda b, at: b.__setitem__(at, 1)),
            'extended': lambda b: change_record(b, lambda b, at: b.__setitem__(at + 1, 1)),
            'interleaved': lambda b: change_record(b, lambda b, at: b.__setitem__(at + 26, 1)),
            'multi_extent': lambda b: change_record(b, lambda b, at: b.__setitem__(at + 25, 128)),
        }
        for name, mutation in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as tmp:
                gdi, _ = fixture(Path(tmp), mutate=mutation)
                self.rejected(gdi)

    def test_duplicate_case_insensitive_names(self):
        def mutate(data):
            first = 18 * 2048 + 68
            n = data[first]
            second = first + n
            replacement = record(b'1st_read.bin;1', 45020, 7)
            data[second:second + len(replacement)] = replacement
            data[second + len(replacement):19 * 2048] = bytes(19 * 2048 - second - len(replacement))
        with tempfile.TemporaryDirectory() as tmp:
            gdi, _ = fixture(Path(tmp), mutate=mutate)
            self.assertIn('Duplicate', self.rejected(gdi))

    def test_gdi_paths_counts_alignment_and_raw_mode(self):
        for change in ('../track data.bin', '/tmp/track.bin', 'a/./b', 'a//b', 'a\\b'):
            with self.subTest(path=change), tempfile.TemporaryDirectory() as tmp:
                gdi, _ = fixture(Path(tmp))
                gdi.write_text(f'1\n1 45000 4 2048 "{change}" 0\n')
                self.rejected(gdi)
        for change in ('2\n1 45000 4 2048 "track data.bin" 0\n',
                       '1\n1 45000 4 2048 "track data.bin" 0 "',
                       '1\n1 45000 4 2048 "track data.bin" -1\n'):
            with self.subTest(gdi=change), tempfile.TemporaryDirectory() as tmp:
                gdi, _ = fixture(Path(tmp))
                gdi.write_text(change)
                self.rejected(gdi)
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            gdi, _ = fixture(folder, raw=True)
            track = folder / 'track data.bin'
            data = bytearray(track.read_bytes())
            data[15] = 2
            track.write_bytes(data)
            self.assertIn('MODE1', self.rejected(gdi))
            track.write_bytes(data[:-1])
            self.assertIn('unaligned', self.rejected(gdi))

    def test_afs_bounds_indices_and_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            gdi, _ = fixture(Path(tmp))
            handle = self.open(gdi)
            for name in ('002_bad', '000_other.adx', '000', '../voice.adx', '999999999999999999999999999_'):
                result = File()
                self.assertLess(self.lib.jgr_disc_source(handle, ('decompile/afs/SOUND/' + name).encode(), C.byref(result)), 0)
        for start, size in ((4999, 2), (0xffffffff, 2)):
            with self.subTest(start=start), tempfile.TemporaryDirectory() as tmp:
                gdi, _ = fixture(Path(tmp), mutate=lambda b: struct.pack_into('<II', b, 70 * 2048 + 8, start, size))
                handle = self.open(gdi)
                result = File()
                self.assertLess(self.lib.jgr_disc_source(handle, b'decompile/afs/SOUND/000_voice.adx', C.byref(result)), 0)

    def test_host_track_symlink_cannot_escape(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as other:
            gdi, _ = fixture(Path(tmp))
            track = Path(tmp) / 'track data.bin'
            outside = Path(other) / 'track.bin'
            track.rename(outside)
            track.symlink_to(outside)
            self.rejected(gdi)

    def test_psp_compilation(self):
        compiler = os.environ.get('PSP_CC') or shutil.which('psp-gcc')
        if not compiler:
            self.skipTest('PSP compiler not configured')
        subprocess.run([compiler, '-std=gnu99', '-Os', '-Wall', '-Wextra', '-Werror',
                        '-D__PSP__', '-DJGR_PORTABLE_ASSETS=1',
                        '-include', str(ROOT / 'psp/src/portable_assets.h'), '-c', str(ROOT / 'psp/src/portable_disc.c'),
                        '-o', str(Path(self.temp.name) / 'disc.o')], check=True)

    @unittest.skipUnless(os.environ.get('JGR_TEST_GDI'), 'Supported user disc not configured')
    def test_real_disc_inventory_and_every_direct_asset_hash(self):
        gdi = Path(os.environ['JGR_TEST_GDI'])
        handle = self.open(gdi, strict=True)
        actual = {}
        def collect(name, file, user):
            actual[name.decode()] = (file.contents.offset, file.contents.size)
            return 0
        self.assertEqual(self.lib.jgr_disc_visit(handle, self.callback(collect), None), 0)
        with Disc(gdi) as reference:
            expected = {e.name: ((e.lba - reference.data.lba) * 2048, e.size) for e in reference.entries}
        self.assertEqual(actual, expected)
        raw = json.loads((ROOT / 'tools/portable/raw_assets.json').read_text())['files']
        catalog = {r['path']: r for r in json.loads((ROOT / 'tools/portable/asset_catalog.json').read_text())['files']}
        for output, source in raw.items():
            file = self.source(handle, source)
            self.assertEqual(file.size, catalog[output]['bytes'], output)
            digest = hashlib.sha256()
            for offset in range(0, file.size, 32768):
                digest.update(self.read(handle, file, offset, min(32768, file.size - offset)))
            self.assertEqual(digest.hexdigest(), catalog[output]['sha256'], output)
            # The COPY path must match the ordinary reader and catalog across
            # every source extent, including AFS members with byte alignment.
            bulk = hashlib.sha256()
            buffer = C.create_string_buffer(513 * 2352)
            for offset in range(0, file.size, 1024 * 1024):
                amount = min(1024 * 1024, file.size - offset)
                self.assertEqual(self.lib.jgr_disc_read_sequential(handle, C.byref(file), offset,
                                 buffer, amount, len(buffer)), 0, output)
                bulk.update(buffer.raw[:amount])
            self.assertEqual(bulk.hexdigest(), catalog[output]['sha256'], output)


if __name__ == '__main__':
    unittest.main()
