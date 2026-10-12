"""Aligned city-span search parity, boundaries, collisions and bounded fallback."""
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static int deny;
static void *optional_calloc(size_t n,size_t size){return deny?NULL:calloc(n,size);}
#define calloc optional_calloc
#include "portable_stage_pack.c"
#undef calloc
int check(const unsigned char *data,unsigned length,unsigned stride,
          const unsigned char *key,unsigned bytes,const unsigned *cuts,unsigned ncuts,
          unsigned capacity,int fail,unsigned *found,unsigned *allocated)
{
    SpanIndex index;deny=fail;span_index_begin(&index,capacity,stride);
    *allocated=index.head!=NULL;
    unsigned previous=0;
    for(unsigned i=0;i<=ncuts;i++){
        unsigned end=i<ncuts?cuts[i]:length;
        if(end<previous||end>length){free(index.head);return -1;}
        span_index_append(&index,data,end);previous=end;
        unsigned a=span_index_find(&index,data,end,key,bytes,stride);
        unsigned b=find_span(data,end,key,bytes,stride);
        if(a!=b){free(index.head);return 1;}
        *found=a;
    }
    free(index.head);return 0;
}
'''


class SpanIndexTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-pack-index-')
        root = Path(cls.temp.name)
        (root / 'bridge.c').write_text(BRIDGE)
        subprocess.run(['cc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-I' + str(ROOT / 'psp/src'),
                        str(root / 'bridge.c'), '-o', str(root / 'test.so')], check=True)
        cls.lib = C.CDLL(str(root / 'test.so'))
        cls.lib.check.argtypes = [C.c_void_p, C.c_uint, C.c_uint, C.c_void_p, C.c_uint,
                                 C.POINTER(C.c_uint), C.c_uint, C.c_uint, C.c_int,
                                 C.POINTER(C.c_uint), C.POINTER(C.c_uint)]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check(self, data, key, stride, cuts=(), capacity=None, fail=False):
        source, query = C.create_string_buffer(data), C.create_string_buffer(key)
        offsets = (C.c_uint * len(cuts))(*cuts)
        found, allocated = C.c_uint(), C.c_uint()
        result = self.lib.check(source, len(data), stride, query, len(key), offsets, len(cuts),
                                capacity if capacity is not None else len(data), fail,
                                C.byref(found), C.byref(allocated))
        self.assertEqual(result, 0)
        return found.value, allocated.value

    def test_cross_append_matches_and_earliest_aligned_occurrence(self):
        for stride in (2, 24):
            data = bytearray(1024 * stride)
            for i in range(len(data)):
                data[i] = (i * 17 + i // stride) % 251
            query = bytes(data[10 * stride:17 * stride])
            data[700 * stride:707 * stride] = query
            found, allocated = self.check(bytes(data), query, stride,
                                           cuts=(12 * stride, 15 * stride, 701 * stride))
            self.assertEqual(found, 10 * stride)
            self.assertEqual(allocated, 1)
            # A short append exposes a prefix that could not be indexed before.
            self.check(bytes(data), bytes(data[10 * stride:20 * stride]), stride,
                       cuts=tuple(range(0, 25 * stride, stride)))

    def test_unaligned_occurrence_does_not_hide_later_aligned_match(self):
        for stride in (2, 24):
            data = bytearray(1000 * stride)
            query = bytes(range(stride * 5))
            data[1:1 + len(query)] = query
            data[100 * stride:105 * stride] = query
            self.assertEqual(self.check(bytes(data), query, stride)[0], 100 * stride)

    def test_equal_prefix_collisions_use_complete_bytes(self):
        for stride, anchor in ((2, 8), (24, 24)):
            records = [bytes(anchor) + struct.pack('<I', i) + bytes(stride * 20 - anchor - 4)
                       for i in range(256)]
            data = b''.join(records)
            for i in (0, 1, 17, 127, 255):
                self.assertEqual(self.check(data, records[i], stride)[0], i * stride * 20)
            self.assertEqual(self.check(data, bytes(anchor) + b'FAIL', stride)[0], 0xffffffff)

    def test_randomized_queries_match_original_search_after_each_append(self):
        rng = random.Random(1968)
        for stride in (2, 24):
            for trial in range(100):
                data = bytes(rng.randrange(16) for _ in range(512 * stride))
                count = rng.randrange(0, 40) * stride
                at = rng.randrange(0, len(data) - count + 1)
                key = data[at:at + count]
                cuts = sorted(rng.sample(range(512), 8))
                self.check(data, key, stride, cuts=[n * stride for n in cuts])
                self.check(data, key + b'\xff' * stride, stride)

    def test_small_short_empty_and_allocation_fallback_queries(self):
        data = bytes(range(256)) * 100
        for stride in (2, 24):
            for key in (b'', data[:2], data[:8], data[48:120], b'nonexistent'):
                expected, _ = self.check(data, key, stride)
                found, allocated = self.check(data, key, stride, fail=True)
                self.assertEqual((found, allocated), (expected, 0))
                found, allocated = self.check(data, key, stride, capacity=0xffffffff)
                self.assertEqual((found, allocated), (expected, 0))
        self.assertEqual(self.check(b'', b'', 24), (0, 0))
        self.assertEqual(self.check(b'', b'x', 24), (0xffffffff, 0))


if __name__ == '__main__':
    unittest.main()
