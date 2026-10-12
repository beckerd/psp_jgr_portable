"""Prop topology compares exact UV/index bytes with bounded bulk reads."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = r'''
#include <stdio.h>
static unsigned calls;
static size_t counted_read(void *p,size_t size,size_t n,FILE *f){calls++;return fread(p,size,n,f);}
#define fread counted_read
#include "portable_stage_props.c"
int check(const char *a,const char *b,unsigned nv,unsigned ni,unsigned *reads){
    FILE *left=fopen(a,"rb"),*right=fopen(b,"rb");if(!left||!right)return -1;
    Bank x={0},y={0};unsigned char batch[16]={0},object[32]={0};
    w16(batch+8,nv);w16(batch+10,ni);w32(object+20,1);
    x.file=left;y.file=right;x.offset[4]=y.offset[4]=nv*24;x.meta[2]=y.meta[2]=batch;
    calls=0;int result=compare_topology(&x,object,&y,object);*reads=calls;
    fclose(left);fclose(right);return result;
}
'''


class PropTopologyIO(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-prop-topology-')
        cls.work = Path(cls.temp.name)
        (cls.work / 'bridge.c').write_text(BRIDGE)
        sources = ('portable_stage', 'portable_stage_pack', 'portable_scratch', 'portable_disc',
                   'portable_models', 'portable_convert', 'portable_indexed')
        subprocess.run(['cc', '-std=gnu99', '-O2', '-ffp-contract=off', '-Wall', '-Wextra',
                        '-Werror', '-shared', '-fPIC', '-I' + str(ROOT / 'psp/src'),
                        str(cls.work / 'bridge.c'),
                        *(str(ROOT / 'psp/src' / (s + '.c')) for s in sources),
                        '-lm', '-o', str(cls.work / 'test.so')], check=True)
        cls.lib = C.CDLL(str(cls.work / 'test.so'))
        cls.lib.check.argtypes = [C.c_char_p, C.c_char_p, C.c_uint, C.c_uint, C.POINTER(C.c_uint)]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @staticmethod
    def fixture(nv, ni):
        return b''.join(struct.pack('<6I', i, i * 17, 0x88776655, i, i + 1, i + 2)
                        for i in range(nv)) + struct.pack('<' + str(ni) + 'H', *(i % nv for i in range(ni)))

    def check(self, a, b, nv, ni):
        left, right = self.work / 'left', self.work / 'right'
        left.write_bytes(a);right.write_bytes(b)
        reads = C.c_uint()
        status = self.lib.check(str(left).encode(), str(right).encode(), nv, ni, C.byref(reads))
        return status, reads.value

    def test_exact_topology_and_read_bound_through_maximum_batch(self):
        for nv in (1, 63, 64, 65, 257, 65535):
            with self.subTest(vertices=nv):
                ni = min(nv * 3, 65535);data = self.fixture(nv, ni)
                self.assertEqual(self.check(data, data, nv, ni),
                                 (1, 2 * ((nv + 63) // 64) + 2 * ((ni * 2 + 2047) // 2048)))

    def test_colors_and_positions_do_not_change_topology(self):
        nv, ni = 257, 771;data = self.fixture(nv, ni);other = bytearray(data)
        for i in range(nv):other[i * 24 + 8:i * 24 + 24] = b'\xff' * 16
        self.assertEqual(self.check(data, other, nv, ni)[0], 1)

    def test_uv_or_index_difference_rejects_and_short_read_falls_back(self):
        nv, ni = 257, 771;data = self.fixture(nv, ni)
        for at in (0, 63 * 24 + 7, 64 * 24, 256 * 24 + 7, nv * 24):
            changed = bytearray(data);changed[at] ^= 1
            self.assertEqual(self.check(data, changed, nv, ni)[0], 0)
        self.assertEqual(self.check(data, data[:-1], nv, ni)[0], 0)
        self.assertEqual(self.check(data[:10], data, nv, 0)[0], 0)


if __name__ == '__main__':
    unittest.main()
