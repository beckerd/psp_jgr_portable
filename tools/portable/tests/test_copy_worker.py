"""Synthetic audio-worker tests. No game payload or device access is required."""
import ctypes as C
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import unittest
import zlib

from test_native_disc import File, fixture
ROOT = Path(__file__).resolve().parents[3]

class Task(C.Structure):
    _fields_ = [('source', File), ('temporary', C.c_char_p), ('bytes', C.c_uint32),
                ('crc32', C.c_uint32), ('tag', C.c_uint32)]
class Completion(C.Structure):
    _fields_ = [('status', C.c_int), ('system_error', C.c_int), ('tag', C.c_uint32),
                ('crc32', C.c_uint32), ('chunks', C.c_uint32), ('bytes', C.c_uint64),
                ('read_us', C.c_uint64), ('write_us', C.c_uint64), ('hash_us', C.c_uint64),
                ('detail', C.c_char * 160)]
class Progress(C.Structure):
    _fields_ = [('completed', C.c_uint), ('total', C.c_uint), ('finished', C.c_int),
                ('paused', C.c_int), ('status', C.c_int)]

class CopyWorkerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='jgr-copy-worker-build-')
        folder = Path(cls.build.name); shim = folder/'shim.c'
        shim.write_text(r'''
#undef fread
#undef fwrite
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned delay_us,failed_writes;
void copy_test_options(unsigned delay,unsigned failures){
    pthread_mutex_lock(&mutex);delay_us=delay;failed_writes=failures;pthread_mutex_unlock(&mutex);
}
size_t copy_test_fread(void *p,size_t size,size_t count,FILE *f){
    pthread_mutex_lock(&mutex);unsigned delay=delay_us;pthread_mutex_unlock(&mutex);
    if(delay){struct timespec t={delay/1000000u,(long)(delay%1000000u)*1000};nanosleep(&t,NULL);}
    return fread(p,size,count,f);
}
size_t copy_test_fwrite(const void *p,size_t size,size_t count,FILE *f){
    pthread_mutex_lock(&mutex);int fail=failed_writes!=0;if(fail)failed_writes--;pthread_mutex_unlock(&mutex);
    if(fail){errno=EIO;return 0;}return fwrite(p,size,count,f);
}
''')
        libpath = folder/'worker.so'
        subprocess.run([os.environ.get('CC','cc'), '-std=c99', '-D_XOPEN_SOURCE=700', '-Wall', '-Wextra',
                        '-Werror', '-pthread', '-fPIC', '-shared', '-DJGR_DISC_TESTING',
                        '-Dfread=copy_test_fread', '-Dfwrite=copy_test_fwrite',
                        str(ROOT/'psp/src/portable_disc.c'), str(ROOT/'psp/src/portable_copy_worker.c'),
                        str(shim), '-o', str(libpath)],check=True)
        cls.lib=C.CDLL(str(libpath))
        cls.lib.jgr_disc_open_fixture.argtypes=[C.POINTER(C.c_void_p),C.c_char_p,C.c_char_p,C.c_size_t]
        cls.lib.jgr_disc_close.argtypes=[C.c_void_p]
        cls.lib.jgr_disc_read.argtypes=[C.c_void_p,C.POINTER(File),C.c_uint32,C.c_void_p,C.c_size_t]
        cls.lib.jgr_disc_clone.argtypes=[C.POINTER(C.c_void_p),C.c_void_p,C.c_char_p,C.c_size_t]
        cls.lib.jgr_copy_worker_create.argtypes=[C.c_void_p,C.POINTER(Task),C.c_uint,C.c_int,C.c_char_p,C.c_size_t]
        cls.lib.jgr_copy_worker_create.restype=C.c_void_p
        cls.lib.jgr_copy_worker_progress.argtypes=[C.c_void_p,C.POINTER(Progress)]
        cls.lib.jgr_copy_worker_result.argtypes=[C.c_void_p,C.c_uint,C.POINTER(Completion)]
        cls.lib.jgr_copy_worker_pause.argtypes=[C.c_void_p,C.c_int]
        for name in ('wait','wait_paused','cancel','destroy'):
            getattr(cls.lib,'jgr_copy_worker_'+name).argtypes=[C.c_void_p]
        cls.lib.copy_test_options.argtypes=[C.c_uint,C.c_uint]
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='jgr-copy-worker-case-');self.addCleanup(self.temp.cleanup)
        self.folder=Path(self.temp.name);self.workers=[];self.discs=[];self.keep=[]
        self.lib.copy_test_options(0,0)
        self.addCleanup(self.cleanup)
    def cleanup(self):
        self.lib.copy_test_options(0,0)
        for worker in self.workers:self.lib.jgr_copy_worker_destroy(worker)
        for disc in self.discs:self.lib.jgr_disc_close(disc)
    def disc(self, raw=True, bad_header=False):
        self.payload=bytes((i*91+i//5)&255 for i in range(1024*1024+17));start=128*2048+37
        def extend(data):
            data.extend(bytes(768*2048-len(data)));data[start:start+len(self.payload)]=self.payload
        gdi,_=fixture(self.folder,raw=raw,prefix=123,mutate=extend)
        if bad_header:
            path=self.folder/'track data.bin';data=bytearray(path.read_bytes());data[123+200*2352+15]=2;path.write_bytes(data)
        handle=C.c_void_p();error=C.create_string_buffer(192)
        self.assertEqual(self.lib.jgr_disc_open_fixture(C.byref(handle),os.fsencode(gdi),error,len(error)),0,error.value)
        self.discs.append(handle);self.source=File(start,len(self.payload));return handle
    def tasks(self,count=4):
        result=(Task*count)();paths=[]
        for i,task in enumerate(result):
            path=self.folder/f'copy{i}.setup-tmp';paths.append(path)
            task.source=self.source;task.temporary=os.fsencode(path);task.bytes=len(self.payload)
            task.crc32=zlib.crc32(self.payload);task.tag=100+i
        self.keep.append(result);return result,paths
    def worker(self,disc,tasks,paused=False):
        error=C.create_string_buffer(192)
        worker=self.lib.jgr_copy_worker_create(disc,tasks,len(tasks),paused,error,len(error))
        self.assertTrue(worker,error.value);self.workers.append(worker);return worker
    def progress(self,worker):
        value=Progress();self.assertEqual(self.lib.jgr_copy_worker_progress(worker,C.byref(value)),0);return value
    def finish(self,worker):
        deadline=time.monotonic()+5
        while not self.progress(worker).finished:
            self.assertLess(time.monotonic(),deadline,'worker did not finish');time.sleep(.002)
        return self.lib.jgr_copy_worker_wait(worker)
    def result(self,worker,index):
        value=Completion();self.assertEqual(self.lib.jgr_copy_worker_result(worker,index,C.byref(value)),1);return value
    def test_raw_and_cooked_batch_runs_without_per_task_handoff(self):
        for raw in (False,True):
            with self.subTest(raw=raw):
                disc=self.disc(raw);tasks,paths=self.tasks(5);worker=self.worker(disc,tasks)
                self.assertEqual(self.finish(worker),0);self.assertEqual(self.progress(worker).completed,5)
                for i,path in enumerate(paths):
                    self.assertEqual(path.read_bytes(),self.payload)
                    r=self.result(worker,i);self.assertEqual((r.status,r.tag,r.bytes,r.crc32,r.chunks),(0,100+i,len(self.payload),zlib.crc32(self.payload),9))
                self.lib.jgr_copy_worker_destroy(worker);self.workers.remove(worker)
                self.lib.jgr_disc_close(disc);self.discs.remove(disc)
    def test_parent_source_reads_can_run_while_copy_worker_advances(self):
        disc=self.disc();tasks,paths=self.tasks(8);self.lib.copy_test_options(1000,0);worker=self.worker(disc,tasks)
        output=C.create_string_buffer(9001)
        for i in range(48):
            offset=(i*20963)%(len(self.payload)-len(output))
            self.assertEqual(self.lib.jgr_disc_read(disc,C.byref(self.source),offset,output,len(output)),0)
            self.assertEqual(output.raw,self.payload[offset:offset+len(output)])
        self.assertEqual(self.finish(worker),0)
        for path in paths:self.assertEqual(hashlib.sha256(path.read_bytes()).digest(),hashlib.sha256(self.payload).digest())
    def test_pause_boundary_cancel_and_independent_clone_lifetime(self):
        disc=self.disc();tasks,paths=self.tasks(4);self.lib.copy_test_options(10000,0);worker=self.worker(disc,tasks,True)
        self.assertEqual(self.lib.jgr_copy_worker_wait_paused(worker),0)
        self.assertTrue(self.progress(worker).paused);self.assertFalse(paths[0].exists())
        # The worker owns its reopened source; closing the parent is safe.
        self.lib.jgr_disc_close(disc);self.discs.remove(disc)
        self.lib.jgr_copy_worker_pause(worker,0)
        deadline=time.monotonic()+3
        while not paths[0].exists() or not paths[0].stat().st_size:
            self.assertLess(time.monotonic(),deadline);time.sleep(.001)
        self.lib.jgr_copy_worker_pause(worker,1)
        self.assertEqual(self.lib.jgr_copy_worker_wait_paused(worker),0)
        self.assertTrue(self.progress(worker).paused)
        size=paths[0].stat().st_size;time.sleep(.03);self.assertEqual(paths[0].stat().st_size,size)
        self.lib.jgr_copy_worker_cancel(worker)
        self.assertEqual(self.finish(worker),-3)
        self.assertEqual(self.progress(worker).completed,1)
        self.assertEqual(self.result(worker,0).status,-3)
        self.assertFalse(paths[1].exists())
    def test_failed_hash_or_output_stops_batch_and_reports_error(self):
        for mode in ('crc','write','directory','missing_parent','raw_header'):
            with self.subTest(mode=mode):
                disc=self.disc(bad_header=mode=='raw_header');tasks,paths=self.tasks(2)
                expected=-2 if mode=='crc' else -4 if mode=='raw_header' else -1
                if mode=='crc':tasks[0].crc32^=1
                if mode=='write':self.lib.copy_test_options(0,1)
                if mode=='directory':paths[0].mkdir()
                if mode=='missing_parent':tasks[0].temporary=os.fsencode(self.folder/'missing'/'audio.tmp')
                worker=self.worker(disc,tasks);self.assertEqual(self.finish(worker),expected)
                result=self.result(worker,0);self.assertEqual(result.status,expected);self.assertTrue(result.detail)
                if mode=='write':self.assertNotEqual(result.system_error,0)
                self.assertEqual(self.progress(worker).completed,1);self.assertFalse(paths[1].exists())
                self.lib.jgr_copy_worker_destroy(worker);self.workers.remove(worker)
                self.lib.jgr_disc_close(disc);self.discs.remove(disc)
                for path in paths:
                    if path.is_dir():path.rmdir()
                    elif path.exists():path.unlink()
                self.lib.copy_test_options(0,0)
    def test_clone_rejects_replaced_or_truncated_validated_track(self):
        for replacement in (False,True):
            with self.subTest(replacement=replacement):
                disc=self.disc();path=self.folder/'track data.bin'
                if replacement:
                    path.rename(self.folder/'old.bin');path.write_bytes(bytes((self.folder/'old.bin').stat().st_size))
                else:
                    with path.open('r+b') as stream:stream.truncate(2048)
                clone=C.c_void_p();error=C.create_string_buffer(192)
                self.assertLess(self.lib.jgr_disc_clone(C.byref(clone),disc,error,len(error)),0)
                self.assertFalse(clone.value);self.assertTrue(error.value)
                self.lib.jgr_disc_close(disc);self.discs.remove(disc)
    def test_psp_compilation(self):
        compiler=os.environ.get('PSP_CC') or shutil.which('psp-gcc')
        if not compiler:self.skipTest('PSP compiler not configured')
        sdk=Path(compiler).resolve().parents[1]/'psp/sdk/include'
        for source in ('portable_copy_worker.c','portable_disc.c'):
            subprocess.run([compiler,'-std=gnu99','-Os','-Wall','-Wextra','-Werror','-D__PSP__',
                            '-I'+str(sdk),'-DJGR_PORTABLE_ASSETS=1','-include',str(ROOT/'psp/src/portable_assets.h'),
                            '-c',str(ROOT/'psp/src'/source),'-o',str(Path(self.build.name)/(source+'.o'))],check=True)

if __name__=='__main__':unittest.main()
