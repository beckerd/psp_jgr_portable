"""Equal-edge tessellation must ignore differences below emitted precision."""
import ctypes as C
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
BRIDGE=r'''
#include "portable_models.c"
int jgr_disc_read(JgrDisc *d,const JgrDiscFile *f,uint32_t at,void *out,size_t n)
{(void)d;(void)f;(void)at;(void)out;(void)n;return -1;}
int jgr_disc_find(JgrDisc *d,const char *name,JgrDiscFile *file)
{(void)d;(void)name;(void)file;return -1;}
int split(const double *points,double limit,unsigned *indices,unsigned capacity)
{
    Assembly a;memset(&a,0,sizeof a);JgrModelOptions options;
    jgr_model_options_default(&options);options.owner=-1;options.max_edge=limit;
    a.options=&options;a.nv=a.vcap=3;a.ni=a.icap=3;
    a.vertices=calloc(3,sizeof *a.vertices);a.indices=malloc(3*sizeof *a.indices);
    a.hash=calloc(HASH_SIZE,sizeof *a.hash);int result=-1;
    if(!a.vertices||!a.indices||!a.hash)goto done;
    for(unsigned i=0;i<3;i++){memcpy(a.vertices[i].p,points+i*3,3*sizeof(double));a.indices[i]=i;}
    if(split_group(&a)||a.ni>capacity)goto done;
    memcpy(indices,a.indices,a.ni*sizeof *indices);result=(int)a.ni;
 done:free(a.vertices);free(a.indices);free(a.hash);return result;
}
'''

class Subdivision(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='jgr-subdivide-');cls.work=Path(cls.temp.name)
        bridge=cls.work/'bridge.c';bridge.write_text(BRIDGE)
        subprocess.run(['cc','-std=c99','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror',
                        '-shared','-fPIC','-I'+str(ROOT/'psp/src'),str(bridge),
                        str(ROOT/'psp/src/portable_convert.c'),str(ROOT/'psp/src/portable_indexed.c'),
                        '-lm','-o',str(cls.work/'native.so')],check=True)
        cls.native=C.CDLL(str(cls.work/'native.so'))
        cls.native.split.argtypes=[C.POINTER(C.c_double),C.c_double,C.POINTER(C.c_uint),C.c_uint]
        spec=importlib.util.spec_from_file_location('subdivision_reference',ROOT/'tools/portable/legacy_ninjalib.py')
        cls.reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.reference)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def check(self,points,limit):
        source=(C.c_double*9)(*(v for p in points for v in p));out=(C.c_uint*4096)()
        count=self.native.split(source,limit,out,len(out));self.assertGreaterEqual(count,0)
        native=list(out[:count]);vertices=[(0,0,0xffffffff,p) for p in points]
        _,expected=self.reference.subdivide(vertices,[0,1,2],limit)
        self.assertEqual(native,expected)
        return native
    def test_subpixel_equal_edges_have_one_topology(self):
        # The apex crosses an equal-edge boundary in double precision, while
        # every input still encodes the exact same positions for the renderer.
        results=[]
        for delta in (-2**-48,0,2**-48):
            points=((0,0,0),(2,0,0),(1+delta,2,0))
            self.assertEqual(struct.pack('<f',points[2][0]),struct.pack('<f',1))
            results.append(self.check(points,2.1))
        self.assertEqual(results[0],results[1]);self.assertEqual(results[1],results[2])
    def test_visible_edge_difference_is_respected(self):
        left=self.check(((0,0,0),(2,0,0),(0.9,2,0)),2.1)
        right=self.check(((0,0,0),(2,0,0),(1.1,2,0)),2.1)
        self.assertNotEqual(left,right)
    def test_threshold_uses_emitted_positions(self):
        for delta in (-2**-48,0,2**-48):
            self.assertEqual(self.check(((0,0,0),(2+delta,0,0),(1,1,0)),2),[0,1,2])

if __name__=='__main__':unittest.main()
