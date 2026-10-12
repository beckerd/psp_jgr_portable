"""City packing keeps pixel payloads off heap and borrows resident ordering keys."""
import ctypes as C
import errno
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def align(value):
    return (value + 15) & ~15


def fixture(kogane=False, pixel_bytes=1024 * 1024, vertices=3):
    textures = 243 if kogane else 1
    offsets = [80, 80 + textures * 16]
    offsets += [offsets[-1], offsets[-1] + 32]
    offsets += [offsets[-1] + vertices * 48]
    offsets += [align(offsets[-1] + vertices * 4)]
    data = bytearray(offsets[-1])
    data[:4] = b'JGS1'
    struct.pack_into('<5I6I', data, 4, textures, 0, 2, vertices * 2, vertices * 2, *offsets)
    vertex = b''.join(struct.pack('<6f', i, i + 1, 0, i + 2, i + 3, i + 4) for i in range(vertices))
    data[offsets[3]:offsets[3] + vertices * 48] = vertex * 2
    struct.pack_into('<' + str(vertices * 2) + 'H', data, offsets[4], *(list(range(vertices)) * 2))
    for i in range(2):
        struct.pack_into('<IIHHHH', data, offsets[2] + i * 16, i * vertices, i * vertices, vertices, vertices, 215 if kogane else 0, 0)
    if kogane:
        for i in range(textures):
            clut = align(len(data)); data.extend(bytes(clut - len(data)))
            data.extend(bytes([i]) * 1024)
            pixel = align(len(data)); data.extend(bytes(pixel - len(data)))
            data.extend(bytes([255 - i]) * 16)
            struct.pack_into('<HHIII', data, offsets[0] + i * 16, 4, 4, 0, clut, pixel)
    else:
        data.extend(bytes(range(256)) * (pixel_bytes // 256))
        struct.pack_into('<HHIII', data, offsets[0], 4, 4, 0, offsets[5], offsets[5] + 1024)
    return bytes(data), vertex * 2


BRIDGE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
static size_t limit,live_limit,live,peak,largest,reads,stop_at,fail_write,deny_begin,deny_end;
static int last_error;
static FILE *input,*output;
static struct {void *p;size_t bytes;} blocks[32];
static void *pack_malloc(size_t n){
 if(n>largest)largest=n;if((limit&&n>limit)||(live_limit&&n>live_limit-live)){errno=0;return NULL;}
 void *p=malloc(n);if(p)for(unsigned i=0;i<32;i++)if(!blocks[i].p){blocks[i].p=p;blocks[i].bytes=n;live+=n;if(live>peak)peak=live;return p;}
 return p;
}
static void *pack_calloc(size_t n,size_t s){void *p=pack_malloc(n*s);if(p)memset(p,0,n*s);return p;}
static void pack_free(void *p){if(!p)return;for(unsigned i=0;i<32;i++)if(blocks[i].p==p){live-=blocks[i].bytes;blocks[i].p=NULL;free(p);errno=ERANGE;return;}abort();}
static size_t pack_fread(void *p,size_t n,size_t c,FILE *f){
 if(f==input){long at=ftell(f);if(at<0)return 0;if(stop_at&&(size_t)at+n*c>stop_at)return 0;if((size_t)at<deny_end&&(size_t)at+n*c>deny_begin)return 0;reads+=n*c;}
 return fread(p,n,c,f);
}
static size_t pack_fwrite(const void *p,size_t n,size_t c,FILE *f){
 if(f==output&&fail_write&&ftell(f)>=(long)fail_write)return 0;
 return fwrite(p,n,c,f);
}
#define malloc pack_malloc
#define calloc pack_calloc
#define free pack_free
#define fread pack_fread
#define fwrite pack_fwrite
#include "portable_stage_pack.c"
#undef malloc
#undef calloc
#undef free
#undef fread
#undef fwrite
int run(const char *src,const char *keys,const char *dest,int kogane,int view,size_t cap,size_t stop,size_t write_failure,size_t view_bytes){
 limit=cap;live=peak=largest=reads=0;stop_at=stop;fail_write=write_failure;memset(blocks,0,sizeof blocks);
 input=fopen(src,"rb");output=fopen(dest,"w+b");FILE *kf=keys?fopen(keys,"rb"):NULL;
 void *borrowed=NULL;size_t bytes=0;if(kf&&view){fseek(kf,0,SEEK_END);bytes=ftell(kf);rewind(kf);borrowed=malloc(bytes?bytes:1);if(fread(borrowed,1,bytes,kf)!=bytes)abort();}
 int result;
#ifdef JGR_TEST_OLD
 result=jgr_stage_pack(input,kf,output,kogane);
#else
 result=jgr_stage_pack_view(input,kf,output,kogane,borrowed,view_bytes==(size_t)-1?bytes:view_bytes);
#endif
 last_error=errno;
 if(input)fclose(input);if(output)fclose(output);if(kf)fclose(kf);free(borrowed);
 if(live)abort();return result;
}
void deny_read(size_t begin,size_t end){deny_begin=begin;deny_end=end;}
void cap_live(size_t bytes){live_limit=bytes;}
size_t peak_bytes(void){return peak;}
size_t largest_bytes(void){return largest;}
size_t read_bytes(void){return reads;}
int pack_error(void){return last_error;}
'''


class StagePack(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-stage-pack-')
        cls.work = Path(cls.temp.name)
        bridge = cls.work / 'bridge.c'; bridge.write_text(BRIDGE)
        cls.libraries = []
        baseline = os.environ.get('JGR_TEST_PACK_BASELINE')
        for old in (False, True) if baseline else (False,):
            out = cls.work / ('before.so' if old else 'after.so')
            include = [str(Path(baseline).resolve())] if old else []
            subprocess.run(['cc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-shared', '-fPIC', *(['-DJGR_TEST_OLD=1'] if old else []),
                            *['-I' + p for p in include + [str(ROOT / 'psp/src')]],
                            str(bridge), '-o', str(out)], check=True)
            lib = C.CDLL(str(out)); lib.run.argtypes = [C.c_char_p] * 3 + [C.c_int] * 2 + [C.c_size_t] * 4
            lib.deny_read.argtypes = [C.c_size_t, C.c_size_t]
            lib.cap_live.argtypes = [C.c_size_t]
            for name in ('peak_bytes', 'largest_bytes', 'read_bytes'):
                getattr(lib, name).restype = C.c_size_t
            cls.libraries.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_pack(self, data, keys=None, kogane=False, view=False, cap=0, stop=0, write_failure=0, view_bytes=-1, old=False, deny=None, live_cap=0):
        src, key, out = (self.work / n for n in ('input.jgs', 'keys.bin', 'output.jgs'))
        src.write_bytes(data)
        if keys is not None:
            key.write_bytes(keys)
        lib = self.libraries[int(old)]
        lib.deny_read(*(deny or (0, 0)))
        lib.cap_live(live_cap)
        result = lib.run(os.fsencode(src), os.fsencode(key) if keys is not None else None,
                         os.fsencode(out), kogane, view, cap, stop, write_failure, view_bytes)
        return result, out.read_bytes(), lib.peak_bytes(), lib.largest_bytes()

    def test_large_pixel_payload_is_streamed_under_small_allocation_limit(self):
        data, _ = fixture()
        result, output, peak, largest = self.run_pack(data, cap=8192)
        self.assertEqual(result, 0); self.assertLess(peak, 4096); self.assertLess(largest, 4096)
        self.assertEqual(struct.unpack_from('<II', output, 16), (3, 3))
        old_pixels, = struct.unpack_from('<I', data, 44)
        new_pixels, = struct.unpack_from('<I', output, 44)
        self.assertEqual(output[new_pixels:], data[old_pixels:])
        if len(self.libraries) == 2:
            self.assertEqual(self.run_pack(data, old=True)[1], output)

    def test_kogane_reordered_pixels_and_borrowed_keys_are_exact(self):
        data, keys = fixture(kogane=True)
        result, output, peak, _ = self.run_pack(data, keys, kogane=True, view=True, cap=8192)
        self.assertEqual(result, 0)
        normal = self.run_pack(data, keys, kogane=True)
        self.assertEqual(normal[:2], (0, output))
        self.assertGreaterEqual(normal[2], peak)
        self.assertLessEqual(normal[2] - peak, len(keys))
        for old in range(243):
            dest = old if old < 215 else old + 17 if old < 226 else old - 11
            clut, pixel = struct.unpack_from('<II', output, 80 + dest * 16 + 8)
            self.assertEqual(output[clut:clut + 1024], bytes([old]) * 1024)
            self.assertEqual(output[pixel:pixel + 16], bytes([255 - old]) * 16)
        if len(self.libraries) == 2:
            self.assertEqual(self.run_pack(data, keys, kogane=True, old=True)[1], output)

    def test_file_backed_keys_stream_corrected_vertices_under_allocation_limit(self):
        data, keys = fixture(vertices=170)
        # Each key/packed vertex buffer fits 8KiB, but a duplicate raw geometry
        # prefix would exceed it. This covers spilled keys on a small heap.
        self.assertEqual(len(keys), 8160)
        prefix, = struct.unpack_from('<I', data, 44)
        self.assertGreater(prefix, 8192)
        result, output, _, largest = self.run_pack(data, keys, cap=8192)
        self.assertEqual(result, 0); self.assertEqual(largest, len(keys))
        self.assertEqual(struct.unpack_from('<II', output, 16), (170, 170))
        if len(self.libraries) == 2:
            self.assertEqual(self.run_pack(data, keys, old=True)[1], output)
        # Equal ordering keys must still reject unequal corrected vertices.
        bad = bytearray(data); at, = struct.unpack_from('<I', bad, 36)
        bad[at + len(keys) // 2] ^= 1
        self.assertEqual(self.run_pack(bad, keys, cap=8192)[0], -1)

    def test_corrected_vertex_short_read_fails_before_output(self):
        data, keys = fixture(vertices=170)
        vertices, = struct.unpack_from('<I', data, 36)
        result, output, _, _ = self.run_pack(data, keys, deny=(vertices + 1, vertices + 2))
        self.assertEqual(result, -1); self.assertEqual(output, b'')

    def test_packed_keys_and_corrected_vertices_share_one_allocation(self):
        data, keys = fixture(vertices=256)
        result, output, peak, largest = self.run_pack(data, keys, live_cap=30 * 1024)
        self.assertEqual(result, 0)
        self.assertLess(peak, 30 * 1024)
        self.assertEqual(largest, len(keys))
        self.assertEqual(struct.unpack_from('<II', output, 16), (256, 256))
        if len(self.libraries) == 2:
            self.assertEqual(self.run_pack(data, keys, old=True)[1], output)
            self.assertEqual(self.run_pack(data, keys, old=True, live_cap=30 * 1024)[0], -1)

    def test_allocation_failure_sets_errno_and_cleanup_preserves_it(self):
        data, keys = fixture(vertices=256)
        self.assertEqual(self.run_pack(data, keys, live_cap=20 * 1024)[0], -1)
        self.assertEqual(self.libraries[0].pack_error(), errno.ENOMEM)

    def test_contained_key_span_uses_already_corrected_vertices(self):
        original, keys = fixture(vertices=6)
        data = bytearray(original)
        batches, vertices = struct.unpack_from('<II', data, 32)
        struct.pack_into('<I', data, batches + 16, 8)
        struct.pack_into('<HH', data, batches + 16 + 8, 3, 3)
        for i in range(12):
            struct.pack_into('<f', data, vertices + i * 24 + 12, 1000 + i % 6)
        result, output, _, _ = self.run_pack(data, keys)
        self.assertEqual(result, 0)
        self.assertEqual(struct.unpack_from('<I', output, 16)[0], 6)
        self.assertEqual(struct.unpack_from('<I', output, batches + 16)[0], 2)
        self.assertEqual(output[vertices:vertices + 6 * 24], data[vertices:vertices + 6 * 24])
        if len(self.libraries) == 2:
            self.assertEqual(self.run_pack(data, keys, old=True)[1], output)

    def test_short_or_invalid_key_view_is_rejected_without_freeing_view(self):
        data, keys = fixture(kogane=True)
        self.assertEqual(self.run_pack(data, keys, kogane=True, view=True, view_bytes=len(keys)-1)[0], -1)
        self.assertEqual(self.run_pack(data, keys[:-1], kogane=True)[0], -1)

    def test_malformed_prefix_and_pixel_ranges_are_rejected(self):
        data, _ = fixture()
        for at, value in ((44, len(data)+1), (36, 79), (32, 0xffffffff), (24, 0xffffffff)):
            bad = bytearray(data); struct.pack_into('<I', bad, at, value)
            self.assertEqual(self.run_pack(bad)[0], -1)
        data, keys = fixture(kogane=True)
        bad = bytearray(data); struct.pack_into('<I', bad, 80+12, len(data)-1)
        self.assertEqual(self.run_pack(bad, keys, kogane=True, view=True)[0], -1)

    def test_streamed_read_and_output_write_failures_propagate(self):
        for kogane in (False, True):
            data, keys = fixture(kogane=kogane)
            pixel_start, = struct.unpack_from('<I', data, 44)
            self.assertEqual(self.run_pack(data, keys if kogane else None, kogane, view=kogane, stop=pixel_start+1000)[0], -1)
            self.assertEqual(self.run_pack(data, keys if kogane else None, kogane, view=kogane, write_failure=pixel_start)[0], -1)


if __name__ == '__main__':
    unittest.main()
