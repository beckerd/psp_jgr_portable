"""Sequential startup cache transactions, identity/bounds and old-install fallback.

Fixtures are synthetic byte patterns, never game payloads. Existing section,
pointer and bubble suites separately exercise their unchanged validators.
"""
import ctypes as C
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/'psp/src'
ROWS=[(name,int(size)) for name,size in re.findall(r'\{"([^"]+)",(\d+)u\}',(SRC/'source_data_startup_registry.inc').read_text())]
CACHE='data/cache/startup-tables.bin'
BRIDGE=r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "source_data_io.h"
static unsigned reads,writes;static int write_failure,validation_failure;
static size_t observed_read(void *p,size_t s,size_t n,FILE *f){reads++;return fread(p,s,n,f);}
static size_t observed_write(const void *p,size_t s,size_t n,FILE *f){writes++;if(write_failure){errno=ENOSPC;return 0;}return fwrite(p,s,n,f);}
#define fread observed_read
#define fwrite observed_write
#include "portable_startup_cache.c"
#undef fread
#undef fwrite
void test_reset(void){reads=writes=0;write_failure=validation_failure=0;}
unsigned test_reads(void){return reads;}
unsigned test_writes(void){return writes;}
void test_fail_write(int value){write_failure=value;}
void test_fail_validation(int value){validation_failure=value;}
static int check(unsigned first,unsigned end){
 if(validation_failure){validation_failure--;return -1;}
 for(unsigned i=first;i<end;i++){
  const void *p=NULL;size_t n=0;void *owned=NULL;
  int view=source_data_io_view(entries[i].name,&p,&n);
  if(view<0)return -1;
  if(!view){char name[256];snprintf(name,sizeof name,"data/assets/%s",entries[i].name);
   FILE *f=source_data_io_open(name);if(!f)return -1;n=entries[i].bytes;owned=malloc(n);p=owned;
   if(!owned || fread(owned,1,n,f)!=n || fgetc(f)!=EOF){fclose(f);free(owned);return -1;}fclose(f);
  }
  int ok=n==entries[i].bytes;
  for(size_t j=0;j<n&&ok;j++)if(((const unsigned char*)p)[j]!=(unsigned char)(i+1))ok=0;
  free(owned);if(!ok)return -1;
 }
 return 0;
}
int source_data_bubble_load_file(const char *path){(void)path;return check(0,1);}
int source_data_numeric_load(void){return check(1,102);}
int source_data_pointers_load(void){return check(102,ENTRY_COUNT);}
static FILE *nothing(void *u,const char*n){(void)u;(void)n;return NULL;}
int test_begin_dependency(void){return source_data_io_begin(nothing,NULL);}
'''
class Cache(C.Structure):
    _fields_=[('image',C.c_void_p),('bytes',C.c_size_t)]

class StartupCache(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='jgr-startup-build-');base=Path(cls.build.name)
        (base/'bridge.c').write_text(BRIDGE)
        so=base/'cache.so'
        subprocess.run(['cc','-shared','-fPIC','-O2','-std=c99','-Wall','-Wextra','-Werror',
                        '-DJGR_PORTABLE_ASSETS=1','-I'+str(SRC),str(base/'bridge.c'),
                        str(SRC/'source_data_io.c'),str(SRC/'source_data_startup.c'),'-o',str(so)],check=True)
        cls.lib=C.CDLL(str(so))
        for name in ('read','collect','write'):
            getattr(cls.lib,'portable_startup_cache_'+name).argtypes=[C.POINTER(Cache),C.c_char_p]
        cls.lib.portable_startup_cache_free.argtypes=[C.POINTER(Cache)]
        cls.lib.portable_startup_cache_prepare.argtypes=[C.c_char_p]
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='jgr-startup-case-');self.root=Path(self.temp.name)
        (self.root/'data/assets').mkdir(parents=True);(self.root/'data/cache').mkdir()
        for i,(name,size) in enumerate(ROWS):(self.root/'data/assets'/name).write_bytes(bytes([i+1])*size)
        self.lib.test_reset();self.old=Path.cwd()
    def tearDown(self):
        self.lib.source_data_io_end();self.lib.source_data_io_view_end()
        __import__('os').chdir(self.old);self.temp.cleanup()
    def enter(self):__import__('os').chdir(self.root)
    def build_cache(self):self.assertEqual(self.lib.portable_startup_cache_prepare(bytes(self.root)),0)
    def read(self):
        cache=Cache();status=self.lib.portable_startup_cache_read(C.byref(cache),bytes(self.root))
        self.lib.portable_startup_cache_free(C.byref(cache));self.assertIsNone(cache.image);self.assertEqual(cache.bytes,0)
        return status
    def test_registry_matches_every_compiled_startup_loader(self):
        expected=[('spray_bubble.jsd',2760)]
        for registry in ('source_data_numeric.c','source_data_pointers_all.c'):
            for name in re.findall(r'if\((source_data_\w+)_load\(\)<0\)',(SRC/registry).read_text()):
                match=re.search(r'source_data_(?:sections_load|pointers_load_file)\("([^"]+)",\s*sections,\s*\d+u?,\s*(\d+)u?',(SRC/(name+'.c')).read_text())
                self.assertIsNotNone(match,name);expected.append((match[1],int(match[2])))
        self.assertEqual(ROWS,expected);self.assertEqual(len(ROWS),140)
        self.assertLess(sum(n for _,n in ROWS)+49,1024*1024)
    def test_old_install_builds_cache_then_one_read_without_originals(self):
        self.enter();self.assertEqual(self.lib.source_data_startup_load(),0)
        self.assertTrue((self.root/CACHE).exists());self.assertEqual(self.lib.test_reads(),140)
        shutil.rmtree(self.root/'data/assets');self.lib.test_reset()
        self.assertEqual(self.lib.source_data_startup_load(),0)
        self.assertEqual(self.lib.test_reads(),1);self.assertEqual(self.lib.test_writes(),0)
        self.assertFalse(self.lib.source_data_io_busy())
    def test_identity_header_payload_and_exact_length_fail_closed(self):
        self.build_cache();path=self.root/CACHE;original=path.read_bytes()
        candidates=[original[:-1],original+b'X',b'']
        for offset in (0,8,12,16,20,24,28,32,36,40,44,48,len(original)-1):
            image=bytearray(original);image[offset]^=1
            if offset<40:struct.pack_into('<I',image,40,zlib.crc32(image[:40]))
            candidates.append(image)
        for image in candidates:
            path.write_bytes(image);self.assertLess(self.read(),0)
        path.write_bytes(original);self.assertEqual(self.read(),0)
    def test_truncated_oversized_missing_source_never_publishes(self):
        source=self.root/'data/assets'/ROWS[17][0];data=source.read_bytes()
        for value in (data[:-1],data+b'X',None):
            if value is None:source.unlink()
            else:source.write_bytes(value)
            self.assertLess(self.lib.portable_startup_cache_prepare(bytes(self.root)),0)
            self.assertFalse((self.root/CACHE).exists())
    def test_corrupt_cache_falls_back_and_repairs(self):
        self.build_cache();path=self.root/CACHE;original=path.read_bytes();path.write_bytes(b'partial')
        self.enter();self.lib.test_reset();self.assertEqual(self.lib.source_data_startup_load(),0)
        self.assertEqual(path.read_bytes(),original);self.assertEqual(self.lib.test_reads(),141)
    def test_validator_failure_discards_cache_and_checks_originals(self):
        self.build_cache();self.enter();self.lib.test_fail_validation(1)
        self.assertEqual(self.lib.source_data_startup_load(),0)
        self.assertFalse(self.lib.source_data_io_busy())
        # An invalid original is rejected and never cached even with a valid envelope.
        (self.root/CACHE).unlink();source=self.root/'data/assets'/ROWS[12][0]
        source.write_bytes(b'X'*ROWS[12][1]);self.assertLess(self.lib.source_data_startup_load(),0)
        self.assertFalse((self.root/CACHE).exists());self.assertFalse(self.lib.source_data_io_busy())
    def test_write_failure_is_nonfatal_for_validated_old_install(self):
        self.enter();self.lib.test_fail_write(1)
        self.assertEqual(self.lib.source_data_startup_load(),0)
        self.assertFalse((self.root/CACHE).exists());self.assertFalse((self.root/(CACHE+'.tmp')).exists())
        self.assertFalse(self.lib.source_data_io_busy())
    def test_failed_publication_retains_previous_cache(self):
        self.build_cache();path=self.root/CACHE;before=path.read_bytes();self.lib.test_fail_write(1)
        self.assertLess(self.lib.portable_startup_cache_prepare(bytes(self.root)),0)
        self.assertEqual(path.read_bytes(),before);self.assertFalse((self.root/(CACHE+'.tmp')).exists())
    def test_converter_dependency_scope_is_preserved(self):
        self.enter();self.assertEqual(self.lib.test_begin_dependency(),0)
        self.assertLess(self.lib.source_data_startup_load(),0)
        self.assertTrue(self.lib.source_data_io_busy());self.assertEqual(self.lib.test_reads(),0)

if __name__=='__main__':unittest.main()
