"""Optional disc-derived warning: source crop parity and failed-cache isolation.

All ordinary fixtures are synthetic. A local original source, when available,
adds independent VQ/image parity without copying its payload into the project.
"""
import ctypes as C
from fractions import Fraction
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'psp/src'
RELATIVE = 'data/setup/startup-warning.jbg'
VQ_BYTES = 2048 + 1024 * 1024 // 4


def morton(x, y):
    return sum(((y >> bit) & 1) << (2 * bit) |
               ((x >> bit) & 1) << (2 * bit + 1) for bit in range(10))


def synthetic_source():
    gbix = b'GBIX' + struct.pack('<I', 8) + bytes(8)
    first = gbix + b'PVRT' + struct.pack('<IBBHHH', 8 + 512 * 512 * 2, 1, 1, 0, 512, 512)
    first += bytes(512 * 512 * 2)
    palette = bytearray()
    for entry in range(256):
        for pixel in range(4):
            palette += struct.pack('<H', ((entry + pixel * 3) % 32 << 11) |
                                   ((entry * 7 + pixel * 11) % 64 << 5) |
                                   ((entry * 13 + pixel * 5) % 32))
    indices = bytes(((i * 73) ^ (i >> 9)) & 255 for i in range(1024 * 1024 // 4))
    return first + gbix + b'PVRT' + struct.pack('<IBBHHH', 8 + 1024 * 1024 + 2048,
                                               1, 3, 0, 1024, 1024) + palette + indices


def reference_image(source):
    """Independent source decoder and rational center-sampled bilinear image."""
    start = source.find(b'PVRT', source.find(b'PVRT') + 4) + 16
    raw = source[start:]
    colors = [((p >> 11) * 255 // 31, ((p >> 5) & 63) * 255 // 63,
               (p & 31) * 255 // 31) for (p,) in struct.iter_unpack('<H', raw[:2048])]
    spread = [morton(i, 0) for i in range(512)]

    def sample(x, y):
        entry = raw[2048 + spread[x // 2] + (spread[y // 2] >> 1)]
        return colors[entry * 4 + (x % 2) * 2 + y % 2]

    # Fraction derives independent exact axis weights; combine as integers to
    # avoid floating-point rounding in this byte-for-byte output reference.
    def axis(index, source, output):
        at = Fraction(2 * index + 1, 2) * Fraction(source, output) - Fraction(1, 2)
        lo = at.numerator // at.denominator
        weight = at - lo
        return lo, min(lo + 1, source - 1), weight.numerator, weight.denominator

    axes = [axis(x, 640, 360) for x in range(360)]
    out = bytearray(480 * 272 * 2)
    for y in range(270):
        y0, y1, fy, dy = axis(y, 480, 270)
        for x, (x0, x1, fx, dx) in enumerate(axes):
            a, b, c, d = sample(x0, y0), sample(x1, y0), sample(x0, y1), sample(x1, y1)
            divisor = dx * dy
            rgb = [((a[k] * (dx-fx) + b[k] * fx) * (dy-fy) +
                    (c[k] * (dx-fx) + d[k] * fx) * fy + divisor // 2) // divisor
                   for k in range(3)]
            struct.pack_into('<H', out, 2*((y+1)*480+x+60),
                             (rgb[0] >> 3) | ((rgb[1] >> 2) << 5) | ((rgb[2] >> 3) << 11))
    return struct.pack('<8s4I', b'JGRBG01\0', 480, 272, len(out), zlib.crc32(out)) + out


FAULTS = r'''
#include <stdio.h>
#include <stdlib.h>
void *warning_test_malloc(size_t);
void warning_test_free(void *);
size_t warning_test_write(const void *,size_t,size_t,FILE *);
int warning_test_flush(FILE *);
int warning_test_close(FILE *);
int warning_test_rename(const char *,const char *);
#define malloc(n) warning_test_malloc(n)
#define free(p) warning_test_free(p)
#define fwrite(p,s,n,f) warning_test_write(p,s,n,f)
#define fflush(f) warning_test_flush(f)
#define fclose(f) warning_test_close(f)
#define JGR_STARTUP_WARNING_RENAME warning_test_rename
'''
BRIDGE = r'''
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include "portable_disc.h"
static const unsigned char *source;
static unsigned source_size;
unsigned warning_alloc_calls,warning_live,warning_fail_alloc,warning_read_calls,warning_fail_read;
unsigned warning_source_calls,warning_io_failure,warning_rename_failure,warning_rename_calls;
unsigned warning_asset_io;
void warning_source(const void *data,unsigned bytes){source=data;source_size=bytes;}
void warning_reset(void){warning_alloc_calls=warning_fail_alloc=warning_read_calls=warning_fail_read=0;
 warning_source_calls=warning_io_failure=warning_rename_failure=warning_rename_calls=warning_asset_io=0;}
void *warning_test_malloc(size_t n){warning_alloc_calls++;if(warning_alloc_calls==warning_fail_alloc)return NULL;
 void *p=(malloc)(n);if(p)warning_live++;return p;}
void warning_test_free(void *p){if(p){warning_live--;}(free)(p);}
size_t warning_test_write(const void *p,size_t s,size_t n,FILE *f){
 if(warning_io_failure==1 && n){(fwrite)(p,s,n-1,f);return n-1;}return (fwrite)(p,s,n,f);}
int warning_test_flush(FILE *f){int r=(fflush)(f);return warning_io_failure==2?EOF:r;}
int warning_test_close(FILE *f){int r=(fclose)(f);return warning_io_failure==3?EOF:r;}
int warning_test_rename(const char *from,const char *to){warning_rename_calls++;
 if(warning_rename_failure==2 || (warning_rename_failure==1 && warning_rename_calls==1)){
  errno=warning_rename_failure==1?EEXIST:EACCES;return -1;}return (rename)(from,to);}
/* Supplemental paths use raw IO. Any forced-include alias reaching the
 * verified game-asset layer is a regression, even if that layer could happen
 * to resolve this particular path on one platform. */
FILE *portable_assets_fopen(const char *p,const char *m){(void)p;(void)m;warning_asset_io++;errno=EIO;return NULL;}
int portable_assets_stat(const char *p,struct stat *s){(void)p;(void)s;warning_asset_io++;errno=EIO;return -1;}
int portable_assets_remove(const char *p){(void)p;warning_asset_io++;errno=EIO;return -1;}
int portable_assets_rename(const char *p,const char *q){(void)p;(void)q;warning_asset_io++;errno=EIO;return -1;}
int jgr_disc_source(JgrDisc *d,const char *name,JgrDiscFile *out){warning_source_calls++;
 if(!d || strcmp(name,"decompile/afs/U_SEGALOGO/003_start_titletxr.bin"))return -1;
 out->offset=0;out->size=source_size;return 0;}
int jgr_disc_read(JgrDisc *d,const JgrDiscFile *f,uint32_t at,void *out,size_t n){warning_read_calls++;
 if(!d || warning_read_calls==warning_fail_read || at>source_size || n>source_size-at || f->size!=source_size)return -1;
 memcpy(out,source+at,n);return 0;}
'''


class StartupWarning(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='jgr-warning-build-')
        folder = Path(cls.build.name)
        (folder/'faults.h').write_text(FAULTS)
        (folder/'bridge.c').write_text(BRIDGE)
        for portable in (False, True):
            library = folder/('portable.so' if portable else 'warning.so')
            flags = ['-DJGR_PORTABLE_ASSETS=1', '-DJGR_DIAGNOSTICS=0',
                     '-include', str(SRC/'portable_assets.h')] if portable else []
            subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                            *flags, '-include', str(folder/'faults.h'), '-I'+str(SRC),
                            str(folder/'bridge.c'), str(SRC/'portable_convert.c'),
                            str(SRC/'portable_startup_warning.c'), '-lm', '-o', str(library)], check=True)
            lib = C.CDLL(str(library))
            lib.warning_source.argtypes = [C.c_void_p, C.c_uint]
            lib.portable_startup_warning_prepare.argtypes = [C.c_void_p, C.c_char_p]
            lib.portable_startup_warning_valid.argtypes = [C.c_char_p]
            if portable:
                cls.portable_lib = lib
            else:
                cls.lib = lib
        cls.original = synthetic_source()
        cls.expected = reference_image(cls.original)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='jgr-warning-case-')
        self.root = Path(self.temp.name)
        self.target = self.root/RELATIVE
        self.lib.warning_reset()
        self.source(self.original)

    def tearDown(self):
        self.assertEqual(self.counter('live').value, 0)
        self.temp.cleanup()

    def counter(self, name):
        return C.c_uint.in_dll(self.lib, 'warning_'+name)

    def source(self, data):
        self.memory = C.create_string_buffer(bytes(data))
        self.lib.warning_source(self.memory, len(data))

    def prepare(self, disc=1):
        return self.lib.portable_startup_warning_prepare(disc, bytes(self.root))

    def write(self, data):
        self.target.parent.mkdir(parents=True, exist_ok=True)
        self.target.write_bytes(data)

    def test_synthetic_exact_source_crop_bilinear_colors_and_header(self):
        self.assertEqual(self.prepare(), 0)
        self.assertEqual(self.target.read_bytes(), self.expected)
        pixels = struct.unpack('<'+str(480*272)+'H', self.target.read_bytes()[24:])
        occupied = [(index % 480, index // 480) for index, color in enumerate(pixels) if color]
        left, right = min(x for x, _ in occupied), max(x for x, _ in occupied)
        top, bottom = min(y for _, y in occupied), max(y for _, y in occupied)
        self.assertEqual((left, top, right, bottom), (60, 1, 419, 270))
        self.assertEqual((right-left+1)*480, (bottom-top+1)*640)
        self.assertEqual(left, 479-right)
        self.assertEqual(top, 271-bottom)
        self.assertEqual(self.lib.portable_startup_warning_valid(bytes(self.root)), 1)
        self.assertEqual(self.counter('read_calls').value, 5)
        self.assertFalse(Path(str(self.target)+'.tmp').exists())
        self.assertFalse((self.root/'data/assets').exists())
        self.assertFalse((self.root/'data/cache').exists())

    def test_release_force_include_uses_raw_io_with_existing_setup_directories(self):
        self.lib = self.portable_lib
        self.lib.warning_reset()
        self.source(self.original)
        self.write(b'invalid old optional cache')
        Path(str(self.target)+'.tmp').write_bytes(b'interrupted old temporary')
        self.counter('rename_failure').value = 1
        self.assertEqual(self.prepare(), 0)
        self.assertEqual(self.target.read_bytes(), self.expected)
        self.assertEqual(self.lib.portable_startup_warning_valid(bytes(self.root)), 1)
        self.assertEqual(self.counter('asset_io').value, 0)
        self.assertEqual(self.counter('rename_calls').value, 2)

    def test_local_original_matches_independently_decoded_reference(self):
        source = ROOT/'decompile/afs/U_SEGALOGO/003_start_titletxr.bin'
        if not source.exists():
            self.skipTest('Optional original source is not available')
        data = source.read_bytes()
        self.source(data)
        self.assertEqual(self.prepare(), 0)
        self.assertEqual(self.target.read_bytes(), reference_image(data))

    def test_valid_existing_needs_no_disc_or_allocation_and_is_never_replaced(self):
        self.write(self.expected)
        before = self.target.stat().st_mtime_ns
        self.counter('fail_alloc').value = 1
        self.counter('rename_failure').value = 2
        self.assertEqual(self.prepare(None), 0)
        self.assertEqual(self.counter('source_calls').value, 0)
        self.assertEqual(self.counter('alloc_calls').value, 0)
        self.assertEqual(self.counter('rename_calls').value, 0)
        self.assertEqual(self.target.stat().st_mtime_ns, before)
        self.assertEqual(self.target.read_bytes(), self.expected)

    def test_missing_disc_and_bad_paths_are_optional_failures(self):
        self.assertEqual(self.prepare(None), -1)
        for root in (None, b'', b'x'*1024):
            self.assertEqual(self.lib.portable_startup_warning_prepare(1, root), -1)
            self.assertEqual(self.lib.portable_startup_warning_valid(root), 0)
        self.assertFalse(self.target.exists())

    def test_rejects_bad_image_lengths_headers_and_checksums(self):
        cases = [b'', self.expected[:-1], self.expected+b'X']
        for offset in (0, 8, 12, 16, 20, 24, len(self.expected)-1):
            data = bytearray(self.expected);data[offset] ^= 1;cases.append(data)
        for data in cases:
            with self.subTest(length=len(data)):
                self.write(data)
                self.assertEqual(self.lib.portable_startup_warning_valid(bytes(self.root)), 0)

    def test_rejects_malformed_or_truncated_source_without_publication(self):
        second = self.original.find(b'PVRT', self.original.find(b'PVRT')+4)
        offsets = (0,4,16,20,24,25,28,30,second,second+4,second+8,
                   second+9,second+10,second+12,second+14)
        cases = [b'', self.original[:-1], self.original+b'X']
        for offset in offsets:
            data=bytearray(self.original);data[offset] ^= 1;cases.append(data)
        for data in cases:
            with self.subTest(length=len(data)):
                self.lib.warning_reset();self.source(data)
                self.assertEqual(self.prepare(), -1)
                self.assertFalse(self.target.exists())
                self.assertEqual(self.counter('live').value, 0)

    def test_all_source_reads_and_allocations_fail_without_leaks(self):
        for kind, limit in (('fail_read', 5), ('fail_alloc', 3)):
            for value in range(1, limit+1):
                with self.subTest(kind=kind, value=value):
                    self.lib.warning_reset();self.counter(kind).value=value
                    self.assertEqual(self.prepare(), -1)
                    self.assertFalse(self.target.exists())
                    self.assertEqual(self.counter('live').value, 0)

    def test_write_flush_close_and_rename_failures_publish_nothing(self):
        for kind, values in (('io_failure', (1,2,3)), ('rename_failure', (2,))):
            for value in values:
                with self.subTest(kind=kind, value=value):
                    self.lib.warning_reset();self.counter(kind).value=value
                    self.assertEqual(self.prepare(), -1)
                    self.assertFalse(self.target.exists())
                    self.assertFalse(Path(str(self.target)+'.tmp').exists())

    def test_psp_style_existing_invalid_file_can_be_repaired(self):
        self.write(b'invalid optional cache')
        self.counter('rename_failure').value=1
        self.assertEqual(self.prepare(), 0)
        self.assertEqual(self.target.read_bytes(), self.expected)
        self.assertEqual(self.counter('rename_calls').value, 2)

    def test_directory_in_place_of_temporary_is_retained(self):
        temporary=Path(str(self.target)+'.tmp');temporary.mkdir(parents=True)
        (temporary/'sentinel').write_bytes(b'keep')
        self.assertEqual(self.prepare(), -1)
        self.assertEqual((temporary/'sentinel').read_bytes(), b'keep')
        self.assertFalse(self.target.exists())

    def test_directory_in_place_of_target_is_retained(self):
        self.target.mkdir(parents=True)
        self.assertEqual(self.prepare(), -1)
        self.assertTrue(self.target.is_dir())
        self.assertFalse(Path(str(self.target)+'.tmp').exists())


if __name__ == '__main__':
    unittest.main()
