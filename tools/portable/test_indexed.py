"""Native texture parity against the pinned desktop encoder, without game data."""
import ctypes as C
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
class CacheStats(C.Structure):
    _fields_=[('bytes',C.c_size_t)]+[(name,C.c_uint) for name in ('entries','hits','builds','evictions')]
BRIDGE='''#include "portable_indexed.h"
#include <stdlib.h>
int texture(unsigned char *p,unsigned sw,unsigned sh,unsigned w,unsigned h,const char *out){
 FILE *f=fopen(out,"wb");if(!f)return -1;JgrJgcTexture t;
 int r=jgr_indexed_texture(p,sw,sh,w,h,f,&t);if(fclose(f))r=-1;return r;}
void release(void *p){free(p);}
'''

class Indexed(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='jgr-indexed-')
        cls.work=Path(cls.temp.name);bridge=cls.work/'bridge.c';bridge.write_text(BRIDGE)
        subprocess.run(['cc','-std=c99','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror',
            '-shared','-fPIC','-DJGR_INDEXED_TESTING','-I'+str(ROOT/'psp/src'),str(bridge),
            str(ROOT/'psp/src/portable_indexed.c'),'-lm','-o',str(cls.work/'native.so')],check=True)
        cls.native=C.CDLL(str(cls.work/'native.so'))
        cls.native.texture.argtypes=[C.c_void_p]+[C.c_uint]*4+[C.c_char_p]
        cls.native.jgr_indexed_memory.argtypes=[C.c_void_p]+[C.c_uint]*4+[C.c_void_p,C.c_size_t,C.POINTER(C.c_uint)]
        cls.native.jgr_rgba_lanczos.argtypes=[C.c_void_p]+[C.c_uint]*4
        cls.native.jgr_rgba_lanczos.restype=C.c_void_p;cls.native.release.argtypes=[C.c_void_p]
        cls.native.jgr_indexed_cache_stats.argtypes=[C.POINTER(CacheStats)]
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def setUp(self):self.native.jgr_indexed_cache_end()
    def tearDown(self):self.native.jgr_indexed_cache_end()
    def stats(self):
        value=CacheStats();self.native.jgr_indexed_cache_stats(C.byref(value));return value
    def resize(self,pixels,sw,sh,w,h):
        buffer=C.create_string_buffer(pixels)
        resized=self.native.jgr_rgba_lanczos(buffer,sw,sh,w,h)
        self.assertTrue(resized)
        try:return C.string_at(resized,w*h*4)
        finally:self.native.release(resized)
    def compare(self,pixels,sw,sh,w,h):
        target=Image.frombytes('RGBA',(sw,sh),pixels).resize((w,h),Image.Resampling.LANCZOS)
        buffer=C.create_string_buffer(pixels)
        resized=self.native.jgr_rgba_lanczos(buffer,sw,sh,w,h)
        self.assertTrue(resized)
        try:self.assertEqual(C.string_at(resized,w*h*4),target.tobytes())
        finally:self.native.release(resized)
        q=target.quantize(256,method=Image.Quantize.FASTOCTREE);p=q.tobytes()
        swizzled=b''.join(p[(y+dy)*w+x:(y+dy)*w+x+16]
            for y in range(0,h,8) for x in range(0,w,16) for dy in range(8))
        output=self.work/'texture.bin';buffer=C.create_string_buffer(pixels)
        self.assertEqual(self.native.texture(buffer,sw,sh,w,h,str(output).encode()),0)
        expected=bytes(q.getpalette('RGBA'))+swizzled
        self.assertEqual(output.read_bytes(),expected)
        memory=C.create_string_buffer(len(expected));flags=C.c_uint();buffer=C.create_string_buffer(pixels)
        self.assertEqual(self.native.jgr_indexed_memory(buffer,sw,sh,w,h,memory,len(memory),C.byref(flags)),0)
        self.assertEqual(memory.raw,expected)
        self.assertEqual(flags.value,int(target.getextrema()[3][0]<255))
        buffer=C.create_string_buffer(pixels)
        self.assertEqual(self.native.jgr_indexed_memory(buffer,sw,sh,w,h,memory,len(memory)-1,C.byref(flags)),-1)
    def test_resampling_and_palette_ties(self):
        rng=random.Random(13)
        for dims in ((16,8,16,8),(32,64,16,8),(128,128,64,64),(128,64,16,8),
                     (4,4,16,8),(16,32,16,8),(64,8,16,8),(255,179,128,128),(1024,512,64,32)):
            with self.subTest(dimensions=dims):
                self.compare(bytes(rng.randrange(256) for _ in range(dims[0]*dims[1]*4)),*dims)
    def test_transparent_colors_and_uniform_palettes(self):
        for p in (bytes((23,92,180,0))*512,bytes((245,100,0,255))*512,
                  b''.join(bytes((i%256,(i*7)%256,(i*3)%256,0 if i%3 else 255)) for i in range(512))):
            self.compare(p,32,16,32,16);self.compare(p,32,16,16,8)
    def test_dimensions_rejected_before_reading(self):
        for dims in ((0,8,16,8),(2048,8,16,8),(16,0,16,8),(16,8,0,8),(16,8,16,1025)):
            self.assertFalse(self.native.jgr_rgba_lanczos(None,*dims))
        pixel=C.create_string_buffer(4)
        for dims in ((1,1,15,8),(1,1,16,7),(1,1,17,8)):
            self.assertEqual(self.native.texture(pixel,*dims,str(self.work/'bad.bin').encode()),-1)
    def test_dimension_cache_reuses_both_axes_and_releases_at_scope_end(self):
        pixels=bytes((i*137+i//3)&255 for i in range(128*128*4))
        uncached=self.resize(pixels,128,128,64,64)
        self.assertEqual(self.stats().bytes,0)
        self.native.jgr_indexed_cache_begin()
        self.assertEqual(self.resize(pixels,128,128,64,64),uncached)
        first=self.stats()
        self.assertEqual((first.entries,first.builds,first.hits),(1,1,1))
        self.assertGreater(first.bytes,0)
        self.assertEqual(self.resize(pixels,128,128,64,64),uncached)
        second=self.stats()
        self.assertEqual((second.bytes,second.builds,second.hits),(first.bytes,1,3))
        self.native.jgr_indexed_cache_end()
        ended=self.stats()
        self.assertEqual((ended.bytes,ended.entries,ended.builds,ended.hits),(0,0,1,3))
        self.assertEqual(self.resize(pixels,128,128,64,64),uncached)
        self.assertEqual(self.stats().builds,3)
        self.native.jgr_indexed_cache_begin()
        self.assertEqual((self.stats().hits,self.stats().builds),(0,0))
    def test_cached_odd_dimensions_and_alpha_extremes_match_pillow(self):
        rng=random.Random(701)
        self.native.jgr_indexed_cache_begin()
        for dims in ((1,1,16,8),(7,13,32,24),(255,179,128,128),(193,71,16,8),
                     (32,199,32,16),(251,8,64,8),(1024,3,16,8)):
            for alpha in (0,1,127,254,255):
                with self.subTest(dimensions=dims,alpha=alpha):
                    pixels=b''.join(bytes((rng.randrange(256),rng.randrange(256),rng.randrange(256),alpha))
                                    for _ in range(dims[0]*dims[1]))
                    self.compare(pixels,*dims)
        self.assertGreater(self.stats().hits,0)
        self.assertLessEqual(self.stats().bytes,128*1024)
    def test_cache_eviction_preserves_taps_and_stays_bounded(self):
        self.native.jgr_indexed_cache_begin()
        for source,target in [(1024,n) for n in range(991,1016)]+[(1024,991),(19,991),(991,19)]:
            with self.subTest(source=source,target=target):
                pixels=bytes((i*51+i//7)&255 for i in range(source*3*4))
                expected=Image.frombytes('RGBA',(source,3),pixels).resize((target,3),Image.Resampling.LANCZOS).tobytes()
                self.assertEqual(self.resize(pixels,source,3,target,3),expected)
                stats=self.stats()
                self.assertLessEqual(stats.bytes,128*1024)
                self.assertLessEqual(stats.entries,16)
        self.assertGreater(self.stats().evictions,0)
    def test_sanitized_cache_lifetime_and_allocation_fallback(self):
        source=self.work/'sanitize.c'
        source.write_text(r'''
#undef malloc
#include "portable_indexed.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static size_t fail_bytes;
static unsigned fail_hits;
void *indexed_test_malloc(size_t bytes){
    if(bytes==fail_bytes){fail_bytes=0;fail_hits++;return NULL;}
    return malloc(bytes);
}
static int check(unsigned sw,unsigned sh,unsigned w,unsigned h,size_t failure){
    size_t n=(size_t)sw*sh*4,out_bytes=(size_t)w*h*4;
    unsigned char *pixels=malloc(n),*copy=malloc(n),*expected=NULL,*actual=NULL;
    if(!pixels || !copy)return 1;
    for(size_t i=0;i<n;i++)pixels[i]=(unsigned char)(i*91+i/17);
    jgr_indexed_cache_end();memcpy(copy,pixels,n);
    expected=jgr_rgba_lanczos(copy,sw,sh,w,h);if(!expected)return 2;
    jgr_indexed_cache_begin();memcpy(copy,pixels,n);
    actual=jgr_rgba_lanczos(copy,sw,sh,w,h);
    if(!actual || memcmp(expected,actual,out_bytes))return 3;
    free(actual);memcpy(copy,pixels,n);fail_bytes=failure;
    actual=jgr_rgba_lanczos(copy,sw,sh,w,h);
    if(!actual || memcmp(expected,actual,out_bytes) || fail_bytes)return 4;
    JgrIndexedCacheStats stats;jgr_indexed_cache_stats(&stats);
    if(stats.bytes>128*1024u || stats.entries>16)return 5;
    jgr_indexed_cache_end();free(actual);free(expected);free(copy);free(pixels);return 0;
}
int main(void){
    static const unsigned cases[][4]={{128,128,64,64},{1024,512,16,8},
        {19,13,991,9},{991,9,19,13},{7,13,32,24},{32,199,32,16},{251,8,64,8}};
    for(unsigned i=0;i<sizeof cases/sizeof *cases;i++){
        const unsigned *d=cases[i];int bad=check(d[0],d[1],d[2],d[3],0);if(bad)return bad;
    }
    int bad=check(128,128,64,64,64*64*4);if(bad)return bad;
    bad=check(128,80,64,32,64*80*4);if(bad)return bad;
    if(fail_hits!=2)return 6;
    puts("9 exact cached/uncached cases; 2 allocation fallback cases passed");return 0;
}
''')
        output=self.work/'sanitize'
        subprocess.run(['cc','-std=c99','-O1','-g','-ffp-contract=off','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-DJGR_INDEXED_TESTING',
            '-Dmalloc=indexed_test_malloc','-I'+str(ROOT/'psp/src'),str(source),
            str(ROOT/'psp/src/portable_indexed.c'),'-lm','-o',str(output)],check=True)
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks='+('0' if sys.platform=='darwin' else '1'),UBSAN_OPTIONS='halt_on_error=1')
        result=subprocess.run([str(output)],env=env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=='__main__':unittest.main()
