"""Native installer transactions, cache-only startup and real-disc conversion parity."""
import ctypes as C
import errno
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock
import zlib
from install_plan import make_plan,CONVERSIONS,derived_recipes,conversion_recipes
from resource_recipes import recipes as resource_recipes

ROOT=Path(__file__).resolve().parents[2]
class CopyStats(C.Structure):
    _fields_=[(name,C.c_uint64) for name in ('bytes','read_us','write_us','hash_us','callback_us','publish_us')]+[(name,C.c_uint) for name in ('files','chunks','bulk_chunks','max_chunk')]
class PipelineStats(C.Structure):
    _fields_=[("drain_us",C.c_uint64)]+[(name,C.c_uint) for name in ("queued","published","overlapped_jobs")]
class WorkGroup(C.Structure):
    _fields_=[(name,C.c_uint64) for name in ('convert_us','flush_us','verify_us','publish_us','bytes')]+[(name,C.c_uint) for name in ('started','completed','failed')]
class WorkStats(C.Structure):
    _fields_=[('city',WorkGroup),('replay',WorkGroup)]
class Result(C.Structure):
    _fields_=[('status',C.c_int),('total',C.c_uint),('available',C.c_uint),
              ('written',C.c_uint),('reused',C.c_uint),('missing',C.c_uint),
              ('completed_bytes',C.c_uint64),('required_bytes',C.c_uint64),
              ('file',C.c_char*192),('detail',C.c_char*192),
              ('phase',C.c_uint),('phase_current',C.c_uint),('phase_total',C.c_uint),('copy',CopyStats),('pipeline',PipelineStats)]
CALLBACK=C.CFUNCTYPE(C.c_int,C.POINTER(Result),C.c_void_p)

def catalog(files):
    return {'files':[{'path':k,'bytes':len(v),'sha256':hashlib.sha256(v).hexdigest(),'crc32':zlib.crc32(v)} for k,v in files.items()]}

class NativeInstall(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='jgr-native-install-build-')
        path=Path(cls.build.name)
        bridge=path/'bridge.c'
        bridge.write_text('''#include "portable_disc.h"
#include "portable_convert.h"
#include "portable_resources.h"
#include "portable_models_jobs.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dlfcn.h>
#include <pthread.h>
static pthread_mutex_t watch_lock=PTHREAD_MUTEX_INITIALIZER;
unsigned test_model_cache_hits(void){JgrModelsStats s;jgr_models_stats(&s);return s.cache_hits;}
#define LOCK() pthread_mutex_lock(&watch_lock)
#define UNLOCK() pthread_mutex_unlock(&watch_lock)
static long long capacity_budget=-1;
static unsigned capacity_calls;
static unsigned stat_calls,watched_target_stats,watched_target_removes,watched_closed,watched_closed_stats;
static char watched_path[1024];
static FILE *watched_file;
static unsigned long long watched_read_bytes,watched_write_bytes;
static unsigned watched_read_calls,watched_write_calls;
static unsigned watched_failure,watched_failure_hits;
void test_watch_io(const char *path) {
 LOCK();snprintf(watched_path,sizeof watched_path,"%s",path);watched_file=NULL;
 watched_target_stats=watched_target_removes=watched_closed=watched_closed_stats=0;
 watched_read_bytes=watched_write_bytes=0;watched_read_calls=watched_write_calls=0;UNLOCK();
}
void test_fail_output(unsigned kind){LOCK();watched_failure=kind;watched_failure_hits=0;UNLOCK();}
unsigned test_output_failure_hits(void){LOCK();unsigned v=watched_failure_hits;UNLOCK();return v;}
unsigned long long test_read_bytes(void){LOCK();unsigned long long v=watched_read_bytes;UNLOCK();return v;}
unsigned long long test_write_bytes(void){LOCK();unsigned long long v=watched_write_bytes;UNLOCK();return v;}
unsigned test_read_calls(void){LOCK();unsigned v=watched_read_calls;UNLOCK();return v;}
unsigned test_write_calls(void){LOCK();unsigned v=watched_write_calls;UNLOCK();return v;}
FILE *fopen(const char *path,const char *mode) {
 FILE *(*system_fopen)(const char *,const char *)=dlsym(RTLD_NEXT,"fopen");
 FILE *file=system_fopen?system_fopen(path,mode):NULL;
 LOCK();if(file&&!strcmp(path,watched_path))watched_file=file;UNLOCK();
 return file;
}
int fclose(FILE *file) {
 int (*system_fclose)(FILE *)=dlsym(RTLD_NEXT,"fclose");
 LOCK();int fail=file==watched_file && watched_failure==3 && !watched_failure_hits;
 if(fail)watched_failure_hits++;
 if(file==watched_file){watched_file=NULL;watched_closed=1;}UNLOCK();
 int result=system_fclose?system_fclose(file):EOF;
 if(fail){errno=EIO;return EOF;}
 return result;
}
int fflush(FILE *file) {
 int (*system_fflush)(FILE *)=dlsym(RTLD_NEXT,"fflush");
 int result=system_fflush?system_fflush(file):EOF;
 LOCK();int fail=file==watched_file && watched_failure==2 && !watched_failure_hits;
 if(fail)watched_failure_hits++;UNLOCK();
 if(fail){errno=EIO;return EOF;}
 return result;
}
size_t fread(void *data,size_t size,size_t count,FILE *file) {
 size_t (*system_fread)(void *,size_t,size_t,FILE *)=dlsym(RTLD_NEXT,"fread");
 size_t result=system_fread?system_fread(data,size,count,file):0;
 LOCK();if(file==watched_file){watched_read_calls++;watched_read_bytes+=size*result;}UNLOCK();
 return result;
}
size_t fwrite(const void *data,size_t size,size_t count,FILE *file) {
 size_t (*system_fwrite)(const void *,size_t,size_t,FILE *)=dlsym(RTLD_NEXT,"fwrite");
 LOCK();if(file==watched_file && watched_failure==1 && !watched_failure_hits && count){
  watched_failure_hits++;errno=EIO;count--;
 }UNLOCK();
 size_t result=system_fwrite?system_fwrite(data,size,count,file):0;
 LOCK();if(file==watched_file){watched_write_calls++;watched_write_bytes+=size*result;}UNLOCK();
 return result;
}
void test_reset_stat_calls(void){LOCK();stat_calls=0;UNLOCK();}
unsigned test_stat_calls(void){LOCK();unsigned v=stat_calls;UNLOCK();return v;}
static int watched_target(const char *path){
 size_t n=strlen(watched_path),suffix=sizeof ".setup-tmp"-1;
 return n>suffix && strlen(path)==n-suffix && !memcmp(path,watched_path,n-suffix) && !strcmp(watched_path+n-suffix,".setup-tmp");
}
unsigned test_target_stats(void){LOCK();unsigned n=watched_target_stats;UNLOCK();return n;}
unsigned test_target_removes(void){LOCK();unsigned n=watched_target_removes;UNLOCK();return n;}
unsigned test_closed_stats(void){LOCK();unsigned n=watched_closed_stats;UNLOCK();return n;}
int remove(const char *path){
 int (*system_remove)(const char *)=dlsym(RTLD_NEXT,"remove");
 LOCK();if(watched_target(path))watched_target_removes++;UNLOCK();
 return system_remove?system_remove(path):-1;
}
int lstat(const char *path,struct stat *out) {
 int (*system_lstat)(const char *,struct stat *)=dlsym(RTLD_NEXT,"lstat");
 LOCK();stat_calls++;
 if(watched_target(path))watched_target_stats++;
 if(watched_closed && !strcmp(path,watched_path))watched_closed_stats++;
 UNLOCK();return system_lstat?system_lstat(path,out):-1;
}
void test_capacity_budget(long long bytes){LOCK();capacity_budget=bytes;capacity_calls=0;UNLOCK();}
unsigned test_capacity_calls(void){LOCK();unsigned v=capacity_calls;UNLOCK();return v;}
int statvfs(const char *root,struct statvfs *out) {
 LOCK();long long budget=capacity_budget;UNLOCK();
 if(budget<0){
  int (*system_statvfs)(const char *,struct statvfs *)=dlsym(RTLD_NEXT,"statvfs");
  return system_statvfs?system_statvfs(root,out):-1;
 }
 /* Synthetic card: interrupted records/output occupy the reserve until they
    are reclaimed. No large real files or nearly-full host volume required. */
 const char *leftovers[]={"data/cache/replay-records-0.tmp","data/assets/title_route.bin.setup-tmp"};
 unsigned long long free_bytes=(unsigned long long)budget;
 for(unsigned i=0;i<2;i++){
  char name[1024];struct stat st;snprintf(name,sizeof name,"%s/%s",root,leftovers[i]);
  if(!lstat(name,&st)&&S_ISREG(st.st_mode))free_bytes=free_bytes>(unsigned long long)st.st_size?free_bytes-st.st_size:0;
 }
 memset(out,0,sizeof *out);out->f_frsize=1;out->f_bavail=free_bytes;LOCK();capacity_calls++;UNLOCK();return 0;
}
/* Match PSP's basename-only rename behavior, including directory arguments. */
int rename(const char *from,const char *to) {
 char path[1024];const char *slash=strrchr(from,'/'),*name=strrchr(to,'/');
 size_t directory=slash?(size_t)(slash-from+1):0;
 if(directory+strlen(name?name+1:to)>=sizeof path)return -1;
 memcpy(path,from,directory);strcpy(path+directory,name?name+1:to);
 return renameat(AT_FDCWD,from,AT_FDCWD,path);
}
int convert_file(const char *gdi,const char *source,unsigned op,unsigned a,unsigned b,unsigned c,unsigned fourth,const char *out) {
 JgrDisc *d=0;JgrDiscFile f;char error[256];int status=-1;
 if(jgr_disc_open(&d,gdi,error,sizeof error)<0){fprintf(stderr,"%s\\n",error);return -1;}
 if(!jgr_disc_source(d,source,&f)) {
  FILE *o=fopen(out,"wb");if(o){unsigned args[4]={a,b,c,fourth};status=op>=40?jgr_resource_convert(d,&f,op,args,o):op==2?jgr_convert_tex2d(d,&f,a,o):jgr_convert_list2d(d,&f,a,b,c,o);if(fclose(o))status=-1;}
 }
 jgr_disc_close(d);return status;
}
#include "portable_install.c"
int test_locate_disc(const char *root,char *out) {
 Installer s={0};if(strlen(root)>=sizeof s.root)return -1;
 strcpy(s.root,root);return locate_disc(&s,out);
}
/* Exercise the actual private lookup with the same validated synthetic plan,
   without requiring a source disc or entering any converter. */
int test_recipe_lookups(const char *plan,const char **names,unsigned count,int fallback,
                       unsigned *indices,unsigned *sizes) {
 Installer s={0};JgrInstallResult r={0};s.result=&r;
 s.plan=fopen(plan,"rb");if(!s.plan)return -1;
 int result=validate(&s);
 if(fallback){free(s.recipe_keys);s.recipe_keys=NULL;s.recipe_key_count=0;}
 LOCK();watched_read_bytes=watched_write_bytes=0;watched_read_calls=watched_write_calls=0;UNLOCK();
 for(unsigned i=0;i<count&&!result;i++){
  Recipe found;long before=ftell(s.plan);unsigned index=0;
  int status=find_recipe(&s,names[i],&found,&index);
  if(ftell(s.plan)!=before){result=-2;break;}
  indices[i]=status?~0u:index;sizes[i]=status?0:found.bytes;
  if(!status&&strcmp(found.name,names[i]))result=-3;
 }
 free(s.recipe_keys);if(fclose(s.plan))result=-4;return result;
}
''')
        sources=['portable_startup_cache','portable_startup_warning','portable_copy_worker','portable_disc','portable_convert','portable_resources','portable_misc','portable_hardware','portable_mission_convert','portable_tables','portable_tables_cached','portable_mission_cast','portable_scene_convert','portable_graffiti_convert','portable_grind_convert','portable_roster_convert','portable_bubble','portable_people_convert',
                 'portable_models','portable_models_jobs','portable_models_colors','portable_models_pack','portable_scratch','portable_indexed','portable_world_convert','portable_stage','portable_stage_pack','portable_stage_derived','portable_stage_props']
        sources.extend(p.stem for p in sorted((ROOT/'psp/src').glob('portable_replay*.c')))
        command=['cc','-shared','-pthread','-fPIC','-O2','-std=c99','-Wall','-Wextra','-Werror','-ffp-contract=off','-DJGR_INSTALL_TESTING','-DJGR_MODELS_STATS=1','-DJGR_INSTALL_WORK_STATS=1','-Ipsp/src',str(bridge)]
        command += ['psp/src/'+s+'.c' for s in sources]+['-lm','-o',str(path/'native.so')]
        subprocess.run(command,cwd=ROOT,check=True)
        cls.lib=C.CDLL(str(path/'native.so'))
        cls.lib.jgr_install_run.argtypes=[C.c_char_p,C.c_char_p,C.c_char_p,C.c_int,C.c_void_p,C.c_void_p,C.POINTER(Result)]
        cls.lib.jgr_install_work_stats.argtypes=[C.POINTER(WorkStats)]
        cls.lib.convert_file.argtypes=[C.c_char_p,C.c_char_p,C.c_uint,C.c_uint,C.c_uint,C.c_uint,C.c_uint,C.c_char_p]
        cls.lib.test_capacity_budget.argtypes=[C.c_longlong]
        cls.lib.test_watch_io.argtypes=[C.c_char_p]
        cls.lib.test_fail_output.argtypes=[C.c_uint]
        cls.lib.test_read_bytes.restype=C.c_ulonglong
        cls.lib.test_write_bytes.restype=C.c_ulonglong
        cls.lib.test_locate_disc.argtypes=[C.c_char_p,C.c_char_p]
        cls.lib.jgr_install_test_bulk_disabled.argtypes=[C.c_int]
        cls.lib.jgr_install_test_audio_disabled.argtypes=[C.c_int]
        cls.lib.jgr_install_test_small_disabled.argtypes=[C.c_int]
        cls.lib.test_recipe_lookups.argtypes=[C.c_char_p,C.POINTER(C.c_char_p),C.c_uint,C.c_int,
                                             C.POINTER(C.c_uint),C.POINTER(C.c_uint)]
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='jgr-native-install-case-');self.root=Path(self.temp.name)
        self.lib.test_capacity_budget(-1)
        self.lib.test_watch_io(b'')
        self.lib.test_fail_output(0)
        self.lib.jgr_install_test_bulk_disabled(0)
        self.lib.jgr_install_test_audio_disabled(0)
        self.lib.jgr_install_test_small_disabled(0)
    def tearDown(self):self.temp.cleanup()
    def plan(self,files,raw=None):
        p=self.root/'install.recipe'
        metadata=catalog(files)
        data,report=make_plan(metadata,raw if raw is not None else {k:'decompile/extracted/ASSET.BIN' for k in files},conversions={})
        p.write_bytes(data);return p
    def install(self,p,partial=0,callback=None,disc=None):
        result=Result()
        status=self.lib.jgr_install_run(os.fsencode(self.root),os.fsencode(p),os.fsencode(disc or self.root/'REMOVED.gdi'),partial,callback,None,C.byref(result))
        self.assertEqual(status,result.status)
        return result
    def cached(self,files):
        for name,data in files.items():
            p=self.root/'data/assets'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    def test_disc_discovery_ignores_hidden_files(self):
        cases=[
            (['Game (USA).gdi'],0,'Game (USA).gdi'),
            (['Game (USA).gdi','._Game (USA).gdi'],0,'Game (USA).gdi'),
            (['._Game (USA).gdi'],1,None),
            (['Game.GdI','.Game.gdi','._Game.gdi','.backup.GDI','.DS_Store'],0,'Game.GdI'),
            (['.Game.GdI','._.Game.GdI','.backup.gdi'],1,None),
            (['Game.gdi','Other.GDI','._Game.gdi','.backup.gdi'],2,None),
        ]
        for index,(names,status,selected) in enumerate(cases):
            with self.subTest(names=names):
                directory=self.root/str(index);directory.mkdir()
                for name in names:(directory/name).touch()
                output=C.create_string_buffer(768)
                self.assertEqual(self.lib.test_locate_disc(os.fsencode(directory),output),status)
                if selected:self.assertEqual(os.fsdecode(output.value),str(directory/selected))
    def test_recipe_index_and_memos_preserve_collisions_misses_and_plan_cursor(self):
        def name_hash(name):
            value=2166136261
            for b in name.encode():value=((value^b)*16777619)&0xffffffff
            return value
        # These distinct names collide even after the common suffix. Candidate
        # hashes alone must never select one another's recipe or checksum.
        self.assertEqual(name_hash('costarring.bin'),name_hash('liquid.bin'))
        files={f'asset{i:04}.bin':b'x'*(i+1) for i in range(1892)}
        files.update({'costarring.bin':b'first','liquid.bin':b'second record'})
        plan=self.plan(files);order=sorted(files)
        queries=order[::13]+['costarring.bin','liquid.bin','not-installed.bin']
        queries=[name for name in queries for _ in range(3)]
        names=(C.c_char_p*len(queries))(*(name.encode() for name in queries))
        expected=[order.index(name) if name in files else 0xffffffff for name in queries]
        expected_sizes=[len(files.get(name,b'')) for name in queries]
        reads=[]
        for fallback in (0,1):
            indices=(C.c_uint*len(queries))();sizes=(C.c_uint*len(queries))()
            self.lib.test_watch_io(os.fsencode(plan))
            self.assertEqual(self.lib.test_recipe_lookups(os.fsencode(plan),names,len(queries),fallback,indices,sizes),0)
            self.assertEqual(list(indices),expected)
            self.assertEqual(list(sizes),expected_sizes)
            reads.append(self.lib.test_read_calls())
        self.assertLess(reads[0]*5,reads[1],reads)
        self.assertLessEqual(reads[0],len(set(queries))+2,reads)

    def test_complete_cache_never_opens_missing_disc(self):
        files={'ui.bin':b'converted data'*300,'audio/a.bin':b'voice'*50000}
        self.cached(files);p=self.plan(files)
        result=self.install(p)
        self.assertEqual(result.status,0);self.assertEqual(result.reused,2);self.assertEqual(result.written,0)
        expected='JGR_ASSETS 1\nlayout 2\ncount 2\n'+''.join(f'{len(v)} {zlib.crc32(v):08x} {k}\n' for k,v in sorted(files.items()))
        self.assertEqual((self.root/'data/assets/assets-manifest.txt').read_text(),expected)
        # Repeat after the source has been removed: only generated files matter.
        self.assertEqual(self.install(p).status,0)
    def test_crc_recipe_checks_contents_and_rejects_noncanonical_digest_padding(self):
        files={'asset.bin':b'cached CRC asset'*3701}
        self.cached(files);p=self.plan(files)
        data=p.read_bytes();self.assertEqual(struct.unpack_from('<I',data,8)[0],3)
        self.assertEqual(self.install(p).status,0)
        manifest=self.root/'data/assets/assets-manifest.txt';manifest.unlink()
        bad=bytearray(data);bad[40+28]=1
        struct.pack_into('<I',bad,24,zlib.crc32(bad[40:]))
        p.write_bytes(bad)
        self.assertEqual(self.install(p).status,-2)
        p.write_bytes(data)
        asset=self.root/'data/assets/asset.bin'
        asset.write_bytes(b'x'+files['asset.bin'][1:])
        self.assertEqual(self.install(p).status,-3)
        self.assertFalse(manifest.exists())
    def test_crc_copy_rejects_mismatch_and_repairs_same_size_corruption(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        from disc import Disc
        with Disc(Path(supplied)) as disc:
            entry=disc.entry('JETRADIO/BOUT_F.ADX');original=disc.extent(entry.lba,entry.size)
        source={'audio.bin':'decompile/extracted/JETRADIO/BOUT_F.ADX'}
        p=self.plan({'audio.bin':bytes([original[0]^1])+original[1:]},raw=source)
        self.assertEqual(self.install(p,disc=supplied).status,-4)
        self.assertFalse((self.root/'data/assets/audio.bin').exists())
        p=self.plan({'audio.bin':original},raw=source)
        self.lib.test_watch_io(os.fsencode(self.root/'data/assets/audio.bin.setup-tmp'))
        self.assertEqual(self.install(p,disc=supplied).status,0)
        self.assertEqual(self.lib.test_read_bytes(),0)
        asset=self.root/'data/assets/audio.bin';asset.write_bytes(b'x'+original[1:])
        result=self.install(p,disc=supplied)
        self.assertEqual((result.status,result.written,result.reused),(0,1,0))
        self.assertEqual(asset.read_bytes(),original)
    def audio_fixture(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        from disc import Disc
        with Disc(Path(supplied)) as disc:
            entry=disc.entry('JETRADIO/BOUT_F.ADX')
            original=disc.extent(entry.lba,entry.size)
        return supplied,original
    def test_background_audio_and_serial_copy_are_identical(self):
        supplied,original=self.audio_fixture();base=self.root
        for disabled in (False,True):
            with self.subTest(serial=disabled):
                self.root=base/str(disabled);self.root.mkdir()
                files={f'adx/audio{i}.adx':original for i in range(3)}
                p=self.plan(files,{name:'decompile/extracted/JETRADIO/BOUT_F.ADX' for name in files})
                self.lib.jgr_install_test_audio_disabled(disabled)
                watched=self.root/'data/assets/adx/audio0.adx.setup-tmp'
                self.lib.test_watch_io(os.fsencode(watched))
                phases=[];released=[]
                def progress(pointer,user):
                    r=pointer.contents
                    if r.pipeline.published:phases.append(r.phase)
                    if r.pipeline.queued and r.pipeline.published==r.pipeline.queued:
                        released.append(self.lib.jgr_install_test_audio_live())
                    return 1
                result=self.install(p,disc=supplied,callback=CALLBACK(progress))
                self.assertEqual(result.status,0,result.detail.decode())
                self.assertEqual((result.written,result.reused),(3,0))
                expected_count=0 if disabled else 3
                self.assertEqual((result.pipeline.queued,result.pipeline.published),(expected_count,expected_count))
                self.assertEqual((result.copy.files,result.copy.bytes),(3,3*len(original)))
                self.assertEqual(self.lib.test_read_bytes(),0)
                self.assertEqual(self.lib.test_write_bytes(),len(original))
                for name in files:self.assertEqual((self.root/'data/assets'/name).read_bytes(),original)
                expected='JGR_ASSETS 1\nlayout 2\ncount 3\n'+''.join(
                    f'{len(data)} {zlib.crc32(data):08x} {name}\n' for name,data in sorted(files.items()))
                self.assertEqual((self.root/'data/assets/assets-manifest.txt').read_text(),expected)
                if not disabled:
                    self.assertIn(5,phases,'Queued audio reports the conversion phase')
                    self.assertTrue(released,'Completed workers were observed before final return')
                    self.assertEqual(set(released),{0},'Completed audio no longer pins buffers through later phases')
        self.root=base
    def test_fresh_copy_publication_avoids_redundant_metadata_and_repair_keeps_checks(self):
        supplied,original=self.audio_fixture();base=self.root
        for worker in (False,True):
            with self.subTest(worker=worker):
                self.root=base/str(worker);self.root.mkdir()
                name='adx/voice.adx' if worker else 'voice.bin'
                target=self.root/'data/assets'/name
                plan=self.plan({name:original},{name:'decompile/extracted/JETRADIO/BOUT_F.ADX'})
                watch=os.fsencode(str(target)+'.setup-tmp')
                self.lib.test_watch_io(watch)
                result=self.install(plan,disc=supplied)
                self.assertEqual((result.status,result.written,result.reused),(0,1,0),result.detail.decode())
                self.assertEqual(target.read_bytes(),original)
                self.assertEqual(self.lib.test_target_stats(),1,'Only final published-size verification remains')
                self.assertEqual(self.lib.test_target_removes(),0,'Fresh absent targets need no removal')
                self.assertEqual(self.lib.test_closed_stats(),0,'Successful streamed output needs no temporary stat')
                self.assertEqual(self.lib.test_read_bytes(),0,'Copies remain hash-while-write')
                target.write_bytes(b'x'+original[1:])
                self.lib.test_watch_io(watch)
                result=self.install(plan,disc=supplied)
                self.assertEqual((result.status,result.written,result.reused),(0,1,0),result.detail.decode())
                self.assertEqual(target.read_bytes(),original)
                self.assertGreaterEqual(self.lib.test_target_stats(),2,'Repair rehash and final verification remain')
                self.assertEqual(self.lib.test_target_removes(),1,'Existing corrupt target is explicitly replaced')
                self.assertEqual(self.lib.test_closed_stats(),0)
        self.root=base
    def test_missing_copy_in_large_unknown_directory_keeps_conservative_checks(self):
        supplied,original=self.audio_fixture()
        directory=self.root/'data/assets';directory.mkdir(parents=True)
        for i in range(4097):(directory/f'unknown{i:04}.tmp').touch()
        plan=self.plan({'voice.bin':original},{'voice.bin':'decompile/extracted/JETRADIO/BOUT_F.ADX'})
        self.lib.test_watch_io(os.fsencode(directory/'voice.bin.setup-tmp'))
        result=self.install(plan,disc=supplied)
        self.assertEqual(result.status,0,result.detail.decode())
        self.assertGreaterEqual(self.lib.test_target_stats(),3)
        self.assertEqual(self.lib.test_target_removes(),1)
        self.assertEqual(len(list(directory.glob('unknown*.tmp'))),4097)
        self.assertEqual((directory/'voice.bin').read_bytes(),original)
    def test_copy_final_size_and_type_verification_still_blocks_manifest(self):
        supplied,original=self.audio_fixture();base=self.root
        for worker in (False,True):
            for damage in ('truncate','directory'):
                with self.subTest(worker=worker,damage=damage):
                    self.root=base/(str(worker)+damage);self.root.mkdir()
                    name='adx/voice.adx' if worker else 'voice.bin'
                    target=self.root/'data/assets'/name
                    plan=self.plan({name:original},{name:'decompile/extracted/JETRADIO/BOUT_F.ADX'})
                    altered=[]
                    def progress(pointer,user):
                        if pointer.contents.phase==6 and not altered:
                            altered.append(True)
                            if damage=='truncate':target.write_bytes(original[:-1])
                            else:
                                target.unlink();target.mkdir();(target/'keep').write_text('keep')
                        return 1
                    result=self.install(plan,disc=supplied,callback=CALLBACK(progress))
                    self.assertEqual(result.status,-4,result.detail.decode())
                    self.assertEqual(result.written,1)
                    self.assertTrue(altered)
                    self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
                    if damage=='directory':self.assertEqual((target/'keep').read_text(),'keep')
        self.root=base
    def test_fresh_foreground_rename_failure_preserves_directory_and_no_manifest(self):
        supplied,original=self.audio_fixture()
        target=self.root/'data/assets/voice.bin'
        plan=self.plan({'voice.bin':original},{'voice.bin':'decompile/extracted/JETRADIO/BOUT_F.ADX'})
        def progress(pointer,user):
            if pointer.contents.phase==4 and not target.exists():
                target.mkdir();(target/'keep').write_text('keep')
            return 1
        result=self.install(plan,disc=supplied,callback=CALLBACK(progress))
        self.assertEqual(result.status,-1,result.detail.decode())
        self.assertEqual(result.written,0)
        self.assertEqual((target/'keep').read_text(),'keep')
        self.assertFalse((self.root/'data/assets/voice.bin.setup-tmp').exists())
        self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
    def test_background_audio_errors_do_not_publish_bad_or_later_files(self):
        supplied,original=self.audio_fixture();base=self.root
        for failure in ('crc','write','flush','close','publish'):
            with self.subTest(failure=failure):
                self.root=base/failure;self.root.mkdir()
                bad=bytes([original[0]^1])+original[1:]
                files={'adx/a.adx':bad if failure=='crc' else original,'adx/b.adx':original}
                p=self.plan(files,{name:'decompile/extracted/JETRADIO/BOUT_F.ADX' for name in files})
                target=self.root/'data/assets/adx/a.adx'
                self.lib.test_watch_io(os.fsencode(str(target)+'.setup-tmp'))
                kind={'write':1,'flush':2,'close':3}.get(failure,0)
                self.lib.test_fail_output(kind)
                def progress(pointer,user):
                    if failure=='publish' and pointer.contents.phase==4 and not target.exists():
                        target.mkdir(parents=True);(target/'preserve').write_text('existing contents')
                    return 1
                result=self.install(p,disc=supplied,callback=CALLBACK(progress))
                self.assertEqual(result.status,-4 if failure=='crc' else -1,result.detail.decode())
                self.assertEqual(result.pipeline.queued,2)
                self.assertEqual(result.pipeline.published,0)
                self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
                self.assertFalse((self.root/'data/assets/adx/b.adx').exists())
                self.assertFalse(list((self.root/'data/assets').rglob('*.setup-tmp')))
                if failure=='publish':self.assertEqual((target/'preserve').read_text(),'existing contents')
                else:self.assertFalse(target.exists())
                if kind:self.assertEqual(self.lib.test_output_failure_hits(),1)
        self.root=base
    def test_background_audio_cancellation_retains_only_completed_verified_files(self):
        supplied,original=self.audio_fixture()
        files={f'adx/audio{i:03}.adx':original for i in range(32)}
        p=self.plan(files,{name:'decompile/extracted/JETRADIO/BOUT_F.ADX' for name in files})
        def progress(pointer,user):return int(pointer.contents.pipeline.published==0)
        result=self.install(p,disc=supplied,callback=CALLBACK(progress))
        self.assertEqual(result.status,-7,result.detail.decode())
        self.assertGreater(result.pipeline.published,0)
        self.assertEqual(result.written,result.pipeline.published)
        actual=list((self.root/'data/assets/adx').glob('*.adx'))
        self.assertEqual(len(actual),result.written)
        for item in actual:self.assertEqual(item.read_bytes(),original)
        self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
        self.assertFalse(list((self.root/'data/assets').rglob('*.setup-tmp')))
        resumed=self.install(p,disc=supplied)
        self.assertEqual(resumed.status,0,resumed.detail.decode())
        self.assertEqual(resumed.reused,result.written)
        self.assertEqual(resumed.written+resumed.reused,32)
    def test_background_audio_repair_does_not_trust_previous_run_hashes(self):
        supplied,original=self.audio_fixture()
        files={'adx/a.adx':original,'adx/b.adx':original}
        p=self.plan(files,{name:'decompile/extracted/JETRADIO/BOUT_F.ADX' for name in files})
        self.cached(files)
        target=self.root/'data/assets/adx/a.adx';target.write_bytes(b'x'+original[1:])
        result=self.install(p,disc=supplied)
        self.assertEqual((result.status,result.written,result.reused),(0,1,1),result.detail.decode())
        self.assertEqual((result.pipeline.queued,result.pipeline.published),(1,1))
        self.assertEqual(target.read_bytes(),original)
    def test_background_audio_finishes_during_real_character_conversion(self):
        supplied,original=self.audio_fixture()
        metadata=json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())
        metadata['files']=[r for r in metadata['files'] if r['path']=='chars.jgc']
        files={f'adx/audio{i:03}.adx':original for i in range(24)}
        for name,data in files.items():
            metadata['files'].append({'path':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'crc32':zlib.crc32(data)})
        p=self.root/'install.recipe'
        p.write_bytes(make_plan(metadata,{name:'decompile/extracted/JETRADIO/BOUT_F.ADX' for name in files},conversion_recipes())[0])
        result=self.install(p,disc=supplied)
        self.assertEqual(result.status,0,result.detail.decode())
        self.assertEqual((result.pipeline.queued,result.pipeline.published),(24,24))
        self.assertGreaterEqual(result.pipeline.overlapped_jobs,1,'Audio completion must occur inside real model generation')
        for row in metadata['files']:
            self.assertEqual(hashlib.sha256((self.root/'data/assets'/row['path']).read_bytes()).hexdigest(),row['sha256'],row['path'])

    def test_model_texture_reuse_survives_interleaved_raw_copy(self):
        supplied,original=self.audio_fixture()
        row=next(r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']
                 if r['path']=='bantam_openings.jgc')
        rows=[dict(row,path=name) for name in ('a_model.jgc','c_model.jgc')]
        rows.append({'path':'b_copy.bin','bytes':len(original),'sha256':hashlib.sha256(original).hexdigest(),
                     'crc32':zlib.crc32(original)})
        recipe=conversion_recipes()['bantam_openings.jgc']
        conversions={name:recipe for name in ('a_model.jgc','c_model.jgc')}
        # Exercise both converters, rather than the whole-file alias shortcut.
        with mock.patch('install_plan.model_derivatives',side_effect=lambda rows,jobs:jobs):
            plan=make_plan({'files':rows},{'b_copy.bin':'decompile/extracted/JETRADIO/BOUT_F.ADX'},conversions)[0]
        p=self.root/'install.recipe';p.write_bytes(plan)
        before=self.lib.test_model_cache_hits()
        result=self.install(p,disc=supplied)
        self.assertEqual((result.status,result.written,result.reused),(0,3,0),result.detail.decode())
        self.assertGreaterEqual(self.lib.test_model_cache_hits()-before,4)
        for item in rows:
            output=self.root/'data/assets'/item['path']
            self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(),item['sha256'],item['path'])
        self.assertFalse((self.root/'data/cache/model-textures.tmp').exists())

    def test_model_content_cache_space_is_reserved_once(self):
        row=next(r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']
                 if r['path']=='bantam_openings.jgc')
        rows=[dict(row,path=name) for name in ('a_model.jgc','b_model.jgc')]
        recipe=conversion_recipes()['bantam_openings.jgc']
        with mock.patch('install_plan.model_derivatives',side_effect=lambda rows,jobs:jobs):
            data=make_plan({'files':rows},{},{r['path']:recipe for r in rows})[0]
        p=self.root/'install.recipe';p.write_bytes(data)
        required=4*1024*1024+32*1024*1024+row['bytes']*3+2*(row['bytes']+32768)
        self.lib.test_capacity_budget(required)
        result=self.install(p)
        self.assertEqual(result.status,-3,result.detail.decode())
        self.assertEqual(result.required_bytes,required)
        self.lib.test_capacity_budget(required-1)
        self.assertEqual(self.install(p).status,-5)

    def test_crc_cutscene_stream_is_verified_without_reread_and_publishes_atomically(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        row=next(r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files'] if r['path']=='cutscene_sources.bin')
        base=self.root
        for failure in (0,1,2,3,4):
            with self.subTest(failure=failure):
                self.root=base/str(failure);self.root.mkdir()
                expected=dict(row)
                if failure==4:expected['crc32']^=1
                p=self.root/'install.recipe'
                p.write_bytes(make_plan({'files':[expected]}, {}, conversion_recipes())[0])
                target=self.root/'data/assets/cutscene_sources.bin'
                self.lib.test_watch_io(os.fsencode(str(target)+'.setup-tmp'))
                self.lib.test_fail_output(failure if failure<4 else 0)
                result=self.install(p,disc=supplied)
                self.assertEqual(self.lib.test_read_bytes(),0)
                if not failure:
                    self.assertEqual(result.status,0,result.detail.decode())
                    self.assertEqual(hashlib.sha256(target.read_bytes()).hexdigest(),row['sha256'])
                    self.assertEqual(self.lib.test_write_bytes(),row['bytes'])
                else:
                    self.assertEqual(result.status,-4 if failure==4 else -1,result.detail.decode())
                    self.assertFalse(target.exists())
                    self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
                    self.assertFalse(Path(str(target)+'.setup-tmp').exists())
                    if failure<4:self.assertEqual(self.lib.test_output_failure_hits(),1)
        self.root=base

    def test_real_model_siblings_derive_exact_outputs_from_verified_bases(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        from install_plan import model_derivatives
        all_catalog=json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())
        recipes=conversion_recipes();derived=model_derivatives(all_catalog['files'],recipes)
        targets={name for name,row in derived.items() if row[0]==93}
        bases={derived[name][1] for name in targets}
        self.assertEqual(len(targets),41)
        self.assertFalse(targets & bases)
        metadata={'files':[r for r in all_catalog['files'] if r['path'] in targets|bases]}
        p=self.root/'install.recipe';p.write_bytes(make_plan(metadata,{},recipes)[0])
        fixtures=Path(os.environ.get('JGR_TEST_ASSETS',ROOT/'psp'))
        for name in bases:self.cached({name:(fixtures/name).read_bytes()})
        result=self.install(p,disc=supplied)
        self.assertEqual((result.status,result.written,result.reused),(0,len(targets),len(bases)),result.detail.decode())
        for row in metadata['files']:
            self.assertEqual(hashlib.sha256((self.root/'data/assets'/row['path']).read_bytes()).hexdigest(),row['sha256'],row['path'])

    def small_fixture(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        rows={r['path']:r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']}
        return supplied,rows,conversion_recipes()

    def test_small_output_ram_and_disk_fallback_preserve_crc_and_patched_headers(self):
        supplied,rows,recipes=self.small_fixture();base=self.root
        names=('set18_cans.bin','set1_obj.bin','source_data_world_audio_tables.jsd')
        for name in names:
            for disabled in (False,True):
                with self.subTest(name=name,disk=disabled):
                    self.root=base/f'{name}-{disabled}';self.root.mkdir()
                    row=dict(rows[name])
                    plan=self.root/'install.recipe';plan.write_bytes(make_plan({'files':[row]}, {},recipes)[0])
                    target=self.root/'data/assets'/name
                    self.lib.jgr_install_test_small_disabled(disabled)
                    self.lib.test_watch_io(os.fsencode(str(target)+'.setup-tmp'))
                    result=self.install(plan,disc=supplied)
                    self.assertEqual((result.status,result.written),(0,1),result.detail.decode())
                    self.assertEqual(hashlib.sha256(target.read_bytes()).hexdigest(),row['sha256'])
                    self.assertEqual(self.lib.test_read_bytes(),row['bytes'] if disabled else 0)
                    if not disabled:
                        self.assertEqual(self.lib.test_write_bytes(),row['bytes'])
                        self.assertEqual(self.lib.test_write_calls(),1)
                    # A second invocation still verifies and reuses the disk file.
                    target.write_bytes(b'x'+target.read_bytes()[1:])
                    repaired=self.install(plan,disc=supplied)
                    self.assertEqual((repaired.status,repaired.written),(0,1),repaired.detail.decode())
                    self.assertEqual(hashlib.sha256(target.read_bytes()).hexdigest(),row['sha256'])
        self.root=base

    def test_small_output_faults_and_wrong_size_never_publish(self):
        supplied,rows,recipes=self.small_fixture();base=self.root;name='set18_cans.bin'
        for failure in ('write','flush','close','checksum','short','long'):
            with self.subTest(failure=failure):
                self.root=base/failure;self.root.mkdir();row=dict(rows[name])
                if failure=='checksum':row['crc32']^=1
                if failure=='short':row['bytes']-=1
                if failure=='long':row['bytes']+=1
                plan=self.root/'install.recipe';plan.write_bytes(make_plan({'files':[row]}, {},recipes)[0])
                target=self.root/'data/assets'/name
                self.lib.test_watch_io(os.fsencode(str(target)+'.setup-tmp'))
                kind={'write':1,'flush':2,'close':3}.get(failure,0)
                self.lib.test_fail_output(kind)
                result=self.install(plan,disc=supplied)
                self.assertEqual(result.status,-1 if kind else -4,result.detail.decode())
                self.assertEqual(result.written,0)
                self.assertFalse(target.exists())
                self.assertFalse(Path(str(target)+'.setup-tmp').exists())
                self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
                self.assertEqual(self.lib.test_read_bytes(),0)
                if kind:self.assertEqual(self.lib.test_output_failure_hits(),1)
                else:self.assertEqual(self.lib.test_write_bytes(),0)
                self.lib.test_fail_output(0)
        self.root=base

    def test_all_small_standalone_outputs_match_catalog_in_ram(self):
        supplied,rows,recipes=self.small_fixture()
        from tables_recipes import recipes as table_recipes
        deferred={r['output'] for r in table_recipes() if r['special']>=63}
        selected=[row for name,row in rows.items() if name in recipes and
                  (10<=recipes[name][0]<=18 or recipes[name][0]==20) and name not in deferred]
        self.assertGreater(len(selected),650)
        plan=self.root/'install.recipe';plan.write_bytes(make_plan({'files':selected}, {},recipes)[0])
        result=self.install(plan,disc=supplied)
        self.assertEqual((result.status,result.written),(0,len(selected)),result.detail.decode())
        for row in selected:
            data=(self.root/'data/assets'/row['path']).read_bytes()
            self.assertEqual(len(data),row['bytes'],row['path'])
            self.assertEqual(hashlib.sha256(data).hexdigest(),row['sha256'],row['path'])

    def test_small_output_cancellation_before_disk_write_does_not_publish(self):
        supplied,rows,recipes=self.small_fixture();name='set18_cans.bin'
        plan=self.root/'install.recipe';plan.write_bytes(make_plan({'files':[rows[name]]}, {},recipes)[0])
        target=self.root/'data/assets'/name;self.lib.test_watch_io(os.fsencode(str(target)+'.setup-tmp'))
        calls=0
        def progress(pointer,user):
            nonlocal calls
            r=pointer.contents
            if r.phase==5 and r.file.decode()==name:
                calls+=1
                if calls==2:return 0
            return 1
        result=self.install(plan,disc=supplied,callback=CALLBACK(progress))
        self.assertEqual(result.status,-7,result.detail.decode())
        self.assertEqual(self.lib.test_write_bytes(),0)
        self.assertFalse(target.exists());self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())

    def dependency_copy_plan(self,data):
        files={name:data for name in ('base.jgc','copy-a.jgc','copy-b.jgc')}
        metadata=catalog(files)
        for row in metadata['files']:row['crc32']=zlib.crc32(data)
        copies={name:(93,'base.jgc',(0,0,0,0)) for name in files if name!='base.jgc'}
        p=self.root/'install.recipe';p.write_bytes(make_plan(metadata,{},copies)[0])
        self.cached({'base.jgc':data});return p
    def test_dependencies_hash_once_per_run_then_copy_and_repair_rehashes(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        data=b'synthetic dependency payload'*18000;p=self.dependency_copy_plan(data)
        source=self.root/'data/assets/base.jgc';self.lib.test_watch_io(os.fsencode(source))
        result=self.install(p,disc=supplied)
        self.assertEqual((result.status,result.written,result.reused),(0,2,1),result.detail.decode())
        # One complete preflight hash and two consumers; no rehash per open.
        self.assertEqual(self.lib.test_read_bytes(),len(data)*3)
        for name in ('copy-a.jgc','copy-b.jgc'):self.assertEqual((source.parent/name).read_bytes(),data)
        self.lib.test_watch_io(os.fsencode(source))
        self.assertEqual(self.install(p).status,0)
        self.assertEqual(self.lib.test_read_bytes(),len(data))
        source.write_bytes(b'X'+data[1:]);(source.parent/'assets-manifest.txt').unlink()
        self.assertEqual(self.install(p).status,-6)
        self.assertFalse((source.parent/'assets-manifest.txt').exists())
    def test_dependency_cache_rejects_type_or_size_changes_before_each_consumer(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        for replacement in ('truncated','directory'):
            with self.subTest(replacement=replacement):
                data=b'synthetic checked dependency'*500;p=self.dependency_copy_plan(data)
                source=self.root/'data/assets/base.jgc';changed=[]
                @CALLBACK
                def change_between_consumers(result,_):
                    r=result.contents
                    if r.phase==5 and r.file==b'copy-b.jgc' and not changed:
                        source.unlink()
                        if replacement=='truncated':source.write_bytes(data[:-1])
                        else:source.mkdir()
                        changed.append(True)
                    return 1
                result=self.install(p,disc=supplied,callback=change_between_consumers)
                self.assertTrue(changed)
                self.assertNotEqual(result.status,0)
                self.assertEqual((source.parent/'copy-a.jgc').read_bytes(),data)
                self.assertFalse((source.parent/'copy-b.jgc').exists())
                self.assertFalse((source.parent/'assets-manifest.txt').exists())
                if source.is_dir():source.rmdir()
                (source.parent/'copy-a.jgc').unlink()
    def test_cast_lighting_mismatch_does_not_publish_either_new_output(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        metadata=json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())
        metadata['files']=[r for r in metadata['files'] if r['path'] in {'chars.jgc','playerlight.bin'}]
        for row in metadata['files']:
            if row['path']=='playerlight.bin':row['crc32']^=1
        p=self.root/'install.recipe';p.write_bytes(make_plan(metadata,{},conversion_recipes())[0])
        result=self.install(p,disc=supplied)
        self.assertEqual(result.status,-4,result.detail.decode())
        self.assertIn(b'cast lighting',result.detail)
        for name in ('chars.jgc','playerlight.bin','assets-manifest.txt','chars.jgc.setup-tmp','playerlight.bin.setup-tmp'):
            self.assertFalse((self.root/'data/assets'/name).exists(),name)
    def test_cast_and_lighting_are_published_from_one_geometry_pass(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        names={'chars.jgc','chars_compact.jgc','playerlight.bin'}
        metadata=json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())
        metadata['files']=[r for r in metadata['files'] if r['path'] in names]
        p=self.root/'install.recipe';p.write_bytes(make_plan(metadata,{},conversion_recipes())[0])
        completed=[]
        def progress(pointer,user):
            r=pointer.contents
            if r.file==b'chars.jgc' and r.written==2:completed.append(True)
            return 1
        result=self.install(p,disc=supplied,callback=CALLBACK(progress))
        self.assertEqual((result.status,result.written),(0,3),result.detail.decode())
        self.assertTrue(completed,'Lighting must finish inside the first cast conversion')
        for row in metadata['files']:
            self.assertEqual(hashlib.sha256((self.root/'data/assets'/row['path']).read_bytes()).hexdigest(),row['sha256'],row['path'])
    def test_missing_converter_never_publishes(self):
        p=self.plan({'unimplemented.bin':b'x'},raw={})
        for partial in (0,1):
            r=self.install(p,partial);self.assertEqual(r.status,-6);self.assertEqual(r.missing,1)
            self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
    def test_unimplemented_operation_can_repair_manifest_from_complete_cache(self):
        files={'replay.bin':b'complete generated replay'};self.cached(files)
        self.assertEqual(self.install(self.plan(files,raw={})).status,0)
    def test_cached_replay_and_native_people_do_not_open_disc(self):
        files={'title_route.bin':b'verified generated replay',
               'people1_native.jpn':b'verified generated pedestrian bank'}
        conversions={'title_route.bin':(91,'decompile/extracted/1ST_READ.BIN',(0,0,0,0)),
                     'people1_native.jpn':(92,'people1_compact.jgc',(1,0,0,0))}
        self.cached(files)
        data,_=make_plan(catalog(files),{},conversions=conversions)
        p=self.root/'install.recipe';p.write_bytes(data)
        result=self.install(p)
        self.assertEqual(result.status,0,result.detail.decode())
        self.assertEqual((result.written,result.reused),(0,2))
        self.assertEqual(list((self.root/'data/cache').iterdir()),[])
    def test_replay_and_native_people_recipe_binding_validated_before_output(self):
        for op,name,source,args in (
                (91,'unknown_route.bin','decompile/extracted/1ST_READ.BIN',(0,0,0,0)),
                (91,'title_route.bin','decompile/extracted/OTHER.BIN',(0,0,0,0)),
                (91,'title_route.bin','decompile/extracted/1ST_READ.BIN',(1,0,0,0)),
                (92,'people1_native.jpn','people2_compact.jgc',(1,0,0,0)),
                (92,'people4_native.jpn','people4_compact.jgc',(4,0,0,0))):
            with self.subTest(name=name,source=source,args=args):
                data,_=make_plan(catalog({name:b'x'}),{},conversions={name:(op,source,args)})
                p=self.root/'install.recipe';p.write_bytes(data)
                self.assertEqual(self.install(p).status,-2)
                self.assertFalse((self.root/'data').exists())
    def native_profile_plan(self,files):
        metadata=catalog(files)
        for row in metadata['files']:row['crc32']=zlib.crc32(files[row['path']])
        conversions={name:(91,'decompile/extracted/1ST_READ.BIN',(0,0,0,0)) for name in files}
        data,_=make_plan(metadata,{},conversions=conversions)
        data=bytearray(data);struct.pack_into('<I',data,8,4)
        for i in range(len(files)):struct.pack_into('<I',data,40+i*440+8,1)
        struct.pack_into('<I',data,24,zlib.crc32(data[40:]))
        p=self.root/'install.recipe';p.write_bytes(data);return p
    def test_obsolete_sha_recipe_versions_are_rejected_before_output(self):
        plan=self.plan({'audio.bin':b'current asset'})
        good=plan.read_bytes()
        for version in (1,2):
            with self.subTest(version=version):
                old=bytearray(good);struct.pack_into('<I',old,8,version)
                plan.write_bytes(old)
                self.assertEqual(self.install(plan).status,-2)
                self.assertFalse((self.root/'data').exists())
        metadata=catalog({'audio.bin':b'current asset'})
        del metadata['files'][0]['crc32']
        with self.assertRaisesRegex(ValueError,'CRC32'):
            make_plan(metadata,{},conversions={})

    def test_native_profile_cached_outputs_keep_crc_and_source_free_startup(self):
        files={'challenge_s1m11_0.bin':b'native route contents','title_route.bin':b'interpreted title contents'}
        self.cached(files);p=self.native_profile_plan(files)
        result=self.install(p)
        self.assertEqual((result.status,result.reused,result.written),(0,2,0),result.detail.decode())
        self.assertEqual(self.install(p).status,0)
        (self.root/'data/assets/assets-manifest.txt').unlink()
        path=self.root/'data/assets/challenge_s1m11_0.bin'
        path.write_bytes(b'X'*len(files['challenge_s1m11_0.bin']))
        result=self.install(p)
        self.assertEqual(result.status,-3,'Damaged native output must require the original disc')
        self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
    def test_native_profile_rejects_mixed_recipe_modes_before_any_output(self):
        files={'challenge_s1m11_0.bin':b'a','title_route.bin':b'b'}
        p=self.native_profile_plan(files);good=p.read_bytes()
        for version,first,second in [(4,0,1),(4,1,0),(3,1,1),(5,1,1),(4,2,1)]:
            with self.subTest(version=version,first=first,second=second):
                bad=bytearray(good);struct.pack_into('<I',bad,8,version)
                struct.pack_into('<I',bad,48,first);struct.pack_into('<I',bad,488,second)
                struct.pack_into('<I',bad,24,zlib.crc32(bad[40:]));p.write_bytes(bad)
                self.assertEqual(self.install(p).status,-2)
                self.assertFalse((self.root/'data').exists())
    def test_damaged_cache_requires_source_and_preserves_save(self):
        saved=self.root/'PSP/SAVEDATA/JGR_PORTABLE/jgr_story.sav'
        saved.parent.mkdir(parents=True);saved.write_bytes(b'my progress')
        self.root=self.root/'PSP/GAME/JGR_PORTABLE'
        files={'ui.bin':b'expected'};self.cached({'ui.bin':b'damaged!'})
        result=self.install(self.plan(files))
        self.assertEqual(result.status,-3);self.assertEqual(saved.read_bytes(),b'my progress')
        self.assertEqual((self.root/'data/assets/ui.bin').read_bytes(),b'damaged!')
        self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
    def test_corrupt_plan_refused_before_creating_data(self):
        p=self.plan({'ui.bin':b'hello'});data=bytearray(p.read_bytes());data[-1]^=1;p.write_bytes(data)
        self.assertEqual(self.install(p).status,-2);self.assertFalse((self.root/'data').exists())
    def test_model_appendix_validated_before_output(self):
        from model_recipes import recipe
        job=recipe(['Test:afs/PLAYER/BEAT.BIN:afs/PLAYER/BEAT.PVR:0,0,0,0'],{'motions':0})
        conversion=(30,job['source'],(0,0,0,0),job['block'])
        files={'model.jgc':b'existing verified model'};self.cached(files)
        data,_=make_plan(catalog(files),{},conversions={'model.jgc':conversion})
        p=self.root/'install.recipe';p.write_bytes(data)
        self.assertEqual(self.install(p).status,0)
        # Recompute outer integrity after damaging inner metadata: the parser,
        # not merely the outer checksum, must reject the invalid instruction.
        bad=bytearray(data);bad[-1]=1;struct.pack_into('<I',bad,24,zlib.crc32(bad[40:]));p.write_bytes(bad)
        self.assertEqual(self.install(p).status,-2)
        self.assertEqual((self.root/'data/assets/model.jgc').read_bytes(),files['model.jgc'])
    def test_stage_sidecar_requires_final_dependency_providers(self):
        self.assertEqual(derived_recipes({'stage1_obj.jgs':None,'stage1.jrs':None}),{})
        self.assertEqual(set(derived_recipes({'stage1.jgs':None})),{'stage1.jgm'})
        self.assertEqual(set(derived_recipes({'stage1.jgs':None,'stage1.jrs':None})),{'stage1.jgm','stage1.jvo'})
    def test_stage_sidecar_recipe_binding_validated_before_output(self):
        for op,name,source,args in (
                (61,'stage1.jgm','stage2.jgs',(1,0,0,0)),
                (61,'stage2.jgm','stage1.jgs',(1,0,0,0)),
                (62,'stage4.jvo','stage4.jgs',(4,0,0,0)),
                (62,'stage1.jvo','stage1.jgs',(1,1,0,0)),
                (63,'stage1.jgs','decompile/afs/STAGE2/000_st2dat.bin',(1,0,0,0)),
                (64,'stage2.jrs','decompile/afs/STAGE1/000_st1dat.bin',(1,0,0,0))):
            data,_=make_plan(catalog({name:b'x'}),{},conversions={name:(op,source,args)})
            p=self.root/'install.recipe';p.write_bytes(data)
            self.assertEqual(self.install(p).status,-2)
            self.assertFalse((self.root/'data').exists())
    def test_stage_dependency_generated_before_alphabetically_earlier_sidecar(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        # This deliberately invalid JGS is still a valid direct-copy recipe.
        # Its successful publication before the JGM parser rejects it proves
        # dependency scheduling, without requiring generated city fixtures.
        from disc import Disc
        with Disc(Path(supplied)) as disc:
            entry=disc.entry('1ST_READ.BIN');original=disc.extent(entry.lba,entry.size)
        files={'stage1.jgm':b'expected sidecar','stage1.jgs':original}
        data,_=make_plan(catalog(files),{'stage1.jgs':'decompile/extracted/1ST_READ.BIN'},
                         conversions={'stage1.jgm':(61,'stage1.jgs',(1,0,0,0))})
        p=self.root/'install.recipe';p.write_bytes(data)
        result=self.install(p,disc=supplied)
        self.assertNotEqual(result.status,0)
        self.assertEqual((self.root/'data/assets/stage1.jgs').read_bytes(),original)
        self.assertFalse((self.root/'data/assets/stage1.jgm').exists())
        self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
    def test_real_copy_hashes_while_writing_and_reads_reused_assets_once(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        from disc import Disc
        with Disc(Path(supplied)) as disc:
            entry=disc.entry('JETRADIO/BOUT_F.ADX');original=disc.extent(entry.lba,entry.size)
        cached=b'already verified asset'*25000
        files={'a_cached.bin':cached,'z_copied.bin':original}
        self.cached({'a_cached.bin':cached})
        plan=self.plan(files,raw={name:'decompile/extracted/JETRADIO/BOUT_F.ADX' for name in files})
        self.lib.test_watch_io(os.fsencode(self.root/'data/assets/a_cached.bin'))
        result=self.install(plan,disc=supplied)
        self.assertEqual(result.status,0,result.detail.decode())
        self.assertEqual((result.written,result.reused),(1,1))
        # Trust lasts only for this install invocation: preflight hashes the
        # complete reused file, and publication checks its current size.
        self.assertEqual(self.lib.test_read_bytes(),len(cached))
        self.assertEqual(self.lib.test_read_calls(),(len(cached)+131071)//131072)
        self.assertEqual((self.root/'data/assets/z_copied.bin').read_bytes(),original)
        (self.root/'data/assets/z_copied.bin').unlink()
        self.lib.test_watch_io(os.fsencode(self.root/'data/assets/z_copied.bin.setup-tmp'))
        result=self.install(plan,disc=supplied)
        self.assertEqual(result.status,0,result.detail.decode())
        chunks=(len(original)+1048575)//1048576
        self.assertEqual((self.lib.test_write_bytes(),self.lib.test_read_bytes()),(len(original),0))
        self.assertEqual((self.lib.test_write_calls(),self.lib.test_read_calls()),(chunks,0))
        self.assertEqual((self.root/'data/assets/z_copied.bin').read_bytes(),original)
        expected='JGR_ASSETS 1\nlayout 2\ncount 2\n'+''.join(
            f'{len(content)} {zlib.crc32(content):08x} {name}\n' for name,content in sorted(files.items()))
        self.assertEqual((self.root/'data/assets/assets-manifest.txt').read_text(),expected)
    def test_same_size_change_after_hash_is_accepted_only_for_current_invocation(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        from disc import Disc
        with Disc(Path(supplied)) as disc:
            entry=disc.entry('JETRADIO/BOUT_F.ADX');original=disc.extent(entry.lba,entry.size)
        files={'a_cached.bin':b'cached original','z_copied.bin':original}
        self.cached({'a_cached.bin':files['a_cached.bin']})
        plan=self.plan(files,raw={name:'decompile/extracted/JETRADIO/BOUT_F.ADX' for name in files})
        changed=[]
        def progress(pointer,user):
            if pointer.contents.phase==6 and not changed:
                (self.root/'data/assets/a_cached.bin').write_bytes(b'x'*len(files['a_cached.bin']))
                changed.append(True)
            return 1
        result=self.install(plan,disc=supplied,callback=CALLBACK(progress))
        # The accepted speed tradeoff intentionally omits a second content
        # read in this invocation. This is not persistent trust in that file.
        self.assertEqual(result.status,0,result.detail.decode())
        self.assertEqual(changed,[True])
        self.assertTrue((self.root/'data/assets/assets-manifest.txt').is_file())
        (self.root/'data/assets/assets-manifest.txt').unlink()
        result=self.install(plan)
        self.assertEqual(result.status,-3)
        self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
    def test_publication_rejects_deleted_or_resized_previously_hashed_assets(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        from disc import Disc
        with Disc(Path(supplied)) as disc:
            entry=disc.entry('JETRADIO/BOUT_F.ADX');original=disc.extent(entry.lba,entry.size)
        files={'a_cached.bin':b'cached original','z_copied.bin':original}
        plan=self.plan(files,raw={name:'decompile/extracted/JETRADIO/BOUT_F.ADX' for name in files})
        for name in files:
            for action in ('delete','truncate'):
                with self.subTest(name=name,action=action):
                    self.cached({'a_cached.bin':files['a_cached.bin']})
                    output=self.root/'data/assets/z_copied.bin'
                    if output.exists():output.unlink()
                    changed=[]
                    def progress(pointer,user):
                        if pointer.contents.phase==6 and not changed:
                            target=self.root/'data/assets'/name
                            if action=='delete':target.unlink()
                            else:target.write_bytes(b'wrong size')
                            changed.append(True)
                        return 1
                    result=self.install(plan,disc=supplied,callback=CALLBACK(progress))
                    self.assertLess(result.status,0)
                    self.assertEqual(result.file,name.encode())
                    self.assertEqual(changed,[True])
                    self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
    def test_raw_copy_rejects_wrong_source_digest_and_extent_before_publication(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        from disc import Disc
        with Disc(Path(supplied)) as disc:
            entry=disc.entry('JETRADIO/BOUT_F.ADX');original=disc.extent(entry.lba,entry.size)
        wrong_digest=bytes([original[0]^1])+original[1:]
        for content,expected_status in ((wrong_digest,-4),(original+b'extra',-1)):
            with self.subTest(expected_status=expected_status):
                plan=self.plan({'audio.bin':content},raw={'audio.bin':'decompile/extracted/JETRADIO/BOUT_F.ADX'})
                self.lib.test_watch_io(os.fsencode(self.root/'data/assets/audio.bin.setup-tmp'))
                result=self.install(plan,disc=supplied)
                self.assertEqual(result.status,expected_status,result.detail.decode())
                self.assertEqual(result.written,0)
                self.assertEqual(self.lib.test_read_bytes(),0)
                if expected_status==-1:self.assertEqual(self.lib.test_write_bytes(),0)
                self.assertFalse((self.root/'data/assets/audio.bin').exists())
                self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
    def test_bulk_copy_and_low_memory_fallback_keep_output_identity(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        from disc import Disc
        with Disc(Path(supplied)) as disc:
            entry=disc.entry('JETRADIO/YEL_SAW1.ADX');original=disc.extent(entry.lba,entry.size)
        self.assertGreater(len(original),2*1024*1024)
        for fallback in (False,True):
            with self.subTest(fallback=fallback):
                output=self.root/'data/assets/audio.bin'
                output.unlink(missing_ok=True)
                self.lib.jgr_install_test_bulk_disabled(int(fallback))
                plan=self.plan({'audio.bin':original},raw={'audio.bin':'decompile/extracted/JETRADIO/YEL_SAW1.ADX'})
                self.lib.test_watch_io(os.fsencode(self.root/'data/assets/audio.bin.setup-tmp'))
                result=self.install(plan,disc=supplied)
                self.assertEqual(result.status,0,result.detail.decode())
                self.assertEqual(output.read_bytes(),original)
                self.assertEqual(self.lib.test_read_bytes(),0)
                self.assertEqual(self.lib.test_write_bytes(),len(original))
                chunk=128*1024 if fallback else 1024*1024
                chunks=(len(original)+chunk-1)//chunk
                self.assertEqual(result.copy.bytes,len(original))
                self.assertEqual(result.copy.files,1)
                self.assertEqual(result.copy.chunks,chunks)
                self.assertEqual(result.copy.bulk_chunks,0 if fallback else chunks)
                self.assertEqual(result.copy.max_chunk,chunk)
                self.assertEqual(self.lib.test_write_calls(),chunks)
                self.assertGreater(result.copy.read_us,0)
                self.assertGreater(result.copy.hash_us,0)

    def test_bulk_copy_cancellation_discards_only_unpublished_output(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        from disc import Disc
        with Disc(Path(supplied)) as disc:
            entry=disc.entry('JETRADIO/YEL_SAW1.ADX');original=disc.extent(entry.lba,entry.size)
        plan=self.plan({'audio.bin':original},raw={'audio.bin':'decompile/extracted/JETRADIO/YEL_SAW1.ADX'})
        seen=[]
        def cancel(pointer,user):
            if pointer.contents.copy.chunks:
                seen.append(pointer.contents.copy.bytes)
                return 0
            return 1
        result=self.install(plan,disc=supplied,callback=CALLBACK(cancel))
        self.assertEqual(result.status,-7)
        self.assertEqual(seen,[1024*1024])
        self.assertEqual(result.copy.files,0)
        for name in ('audio.bin','audio.bin.setup-tmp','assets-manifest.txt'):
            self.assertFalse((self.root/'data/assets'/name).exists())
        result=self.install(plan,disc=supplied)
        self.assertEqual(result.status,0,result.detail.decode())
        self.assertEqual((self.root/'data/assets/audio.bin').read_bytes(),original)
    def test_raw_copy_write_flush_and_close_errors_never_publish(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        from disc import Disc
        with Disc(Path(supplied)) as disc:
            entry=disc.entry('JETRADIO/BOUT_F.ADX');original=disc.extent(entry.lba,entry.size)
        plan=self.plan({'audio.bin':original},raw={'audio.bin':'decompile/extracted/JETRADIO/BOUT_F.ADX'})
        for kind in (1,2,3):
            with self.subTest(operation=('write','flush','close')[kind-1]):
                self.lib.test_watch_io(os.fsencode(self.root/'data/assets/audio.bin.setup-tmp'))
                self.lib.test_fail_output(kind)
                result=self.install(plan,disc=supplied)
                self.assertEqual(self.lib.test_output_failure_hits(),1)
                self.assertEqual(result.status,-1,result.detail.decode())
                self.assertEqual(result.detail.decode(),
                    f"Asset {('conversion','flush','close')[kind-1]} failed (operation 1, errno {errno.EIO})")
                self.assertEqual(result.written,0)
                self.assertFalse((self.root/'data/assets/audio.bin').exists())
                self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
                self.lib.test_fail_output(0)
    def test_real_city_chain_sidecars_and_interrupted_pair_repair(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        conversions={}
        for stage in (1,2,3,5,6):
            source=f'decompile/afs/STAGE{stage}/000_st{stage}dat.bin'
            for suffix,op in (('jgs',63),('jrs',64)):
                conversions[f'stage{stage}.{suffix}']=(op,source,(stage,0,0,0))
        conversions.update(derived_recipes(conversions))
        all_recipes=conversion_recipes()
        extra=('kogane_surface.bin','stage1_obj.jgs','set1_obj.bin','grind_lights.jgs','spray_bubble.jsd',
               'source_data_ptr_grind_props_data.jsd','source_data_shibuya_gate_data.jsd',
               'source_data_ptr_shibuya_door_data.jsd','source_data_stage_ped_proxy_data.jsd',
               'source_data_ptr_kogane_tunnel_materials.jsd')
        conversions.update({name:all_recipes[name] for name in extra})
        expected=json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())
        expected['files']=[r for r in expected['files'] if r['path'] in conversions]
        self.assertEqual(len(expected['files']),30)
        data,_=make_plan(expected,{},conversions=conversions)
        p=self.root/'install.recipe';p.write_bytes(data)
        result=self.install(p,disc=supplied)
        self.assertEqual(result.status,0,(result.file.decode(),result.detail.decode()))
        self.assertEqual(result.written,30)
        work=WorkStats();self.lib.jgr_install_work_stats(C.byref(work))
        city_names={name for name,recipe in conversions.items() if recipe[0] in (50,60,61,62,63,64,71)}
        self.assertEqual((work.city.started,work.city.completed,work.city.failed),(len(city_names),len(city_names),0))
        self.assertEqual(work.city.bytes,sum(row['bytes'] for row in expected['files'] if row['path'] in city_names))
        self.assertGreater(work.city.convert_us,0)
        self.assertGreater(work.city.verify_us,0)
        self.assertEqual(work.replay.started,0)
        for row in expected['files']:
            with self.subTest(asset=row['path']):
                content=(self.root/'data/assets'/row['path']).read_bytes()
                self.assertEqual(len(content),row['bytes'])
                self.assertEqual(hashlib.sha256(content).hexdigest(),row['sha256'])
        # A complete pack does not consult the original after conversion.
        self.assertEqual(self.install(p).status,0)
        self.lib.jgr_install_work_stats(C.byref(work))
        self.assertEqual(bytes(work),bytes(C.sizeof(work)),'A cached restart must reset all work counters')
        # JGS survived, but neither the JRS output nor its setup cache did.
        # Repair must regenerate the pair, verify the disposable JGS, and
        # replace only the missing JRS without rewriting an already valid JGS.
        geometry=self.root/'data/assets/stage5.jgs'
        stamp=geometry.stat().st_mtime_ns
        (self.root/'data/assets/stage5.jrs').unlink()
        (self.root/'data/cache/stage5.jrs').write_bytes(b'damaged interrupted sidecar')
        result=self.install(p,disc=supplied)
        self.assertEqual(result.status,0,(result.file.decode(),result.detail.decode()))
        self.assertEqual((result.written,result.reused),(1,29))
        self.assertEqual(geometry.stat().st_mtime_ns,stamp)
        self.assertEqual(self.install(p).status,0)
    def test_real_city_sidecar_mismatch_reports_checksum(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Supported original disc not configured')
        source='decompile/afs/STAGE6/000_st6dat.bin'
        data,_=make_plan(catalog({'stage6.jgs':b'wrong geometry','stage6.jrs':b'wrong sidecar'}),{},
                        conversions={'stage6.jgs':(63,source,(6,0,0,0)),
                                     'stage6.jrs':(64,source,(6,0,0,0))})
        p=self.root/'install.recipe';p.write_bytes(data)
        result=self.install(p,disc=supplied)
        self.assertEqual(result.status,-4)
        self.assertEqual(result.file,b'stage6.jrs')
        work=WorkStats();self.lib.jgr_install_work_stats(C.byref(work))
        self.assertEqual((work.city.started,work.city.completed,work.city.failed),(1,0,1))
        self.assertGreater(work.city.convert_us,0)
        self.assertEqual(work.city.bytes,0)
        self.assertIn(b'city sidecar',result.detail)
        self.assertFalse((self.root/'data/assets/stage6.jgs').exists())
        self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())

    def test_roster_runs_after_verified_generated_dependencies(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test source-assisted setup')
        fixtures=Path(os.environ.get('JGR_TEST_ASSETS',ROOT/'psp'))
        names=('chars_compact.jgc','playerlight.bin','chars_roster.bin')
        expected=json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())
        expected['files']=[r for r in expected['files'] if r['path'] in names]
        recipes=conversion_recipes();conversions={name:recipes[name] for name in names}
        data,_=make_plan(expected,{},conversions=conversions)
        p=self.root/'install.recipe';p.write_bytes(data)
        for name in names[:2]:
            content=(fixtures/name).read_bytes()
            row=next(r for r in expected['files'] if r['path']==name)
            self.assertEqual(hashlib.sha256(content).hexdigest(),row['sha256'])
            self.cached({name:content})
        result=self.install(p,disc=supplied)
        self.assertEqual(result.status,0,(result.file.decode(),result.detail.decode()))
        self.assertEqual((result.written,result.reused),(1,2))
        actual=(self.root/'data/assets/chars_roster.bin').read_bytes()
        row=next(r for r in expected['files'] if r['path']=='chars_roster.bin')
        self.assertEqual(hashlib.sha256(actual).hexdigest(),row['sha256'])
        self.assertEqual(self.install(p).status,0)
    def test_cancel_keeps_verified_files_and_no_manifest(self):
        files={'ui.bin':b'x'*100000};self.cached(files);p=self.plan(files)
        callback=CALLBACK(lambda r,user:0)
        self.assertEqual(self.install(p,callback=callback).status,-7)
        self.assertEqual((self.root/'data/assets/ui.bin').read_bytes(),files['ui.bin'])
        self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
        self.assertEqual(self.install(p).status,0)
    def test_fresh_preflight_reports_phases_and_avoids_per_file_stats(self):
        files={f'group/asset{i:04}.bin':b'asset' for i in range(1892)}
        p=self.plan(files);events=[]
        def progress(pointer,user):
            r=pointer.contents;events.append((r.phase,r.phase_current,r.phase_total));return 1
        self.lib.test_reset_stat_calls()
        result=self.install(p,callback=CALLBACK(progress))
        self.assertEqual(result.status,-3)
        self.assertEqual(set(phase for phase,_,_ in events),set(range(5)))
        for phase in (0,1,2):
            counts=[current for value,current,total in events if value==phase]
            self.assertEqual(counts[0],0);self.assertEqual(counts[-1],1892)
        self.assertLess(self.lib.test_stat_calls(),80)
        self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
    def test_cached_progress_reaches_final_verification_and_completion(self):
        files={'nested/voice.bin':b'voice','ui.bin':b'ui'};self.cached(files);events=[]
        def progress(pointer,user):
            r=pointer.contents;events.append((r.phase,r.phase_current,r.phase_total));return 1
        result=self.install(self.plan(files),callback=CALLBACK(progress))
        self.assertEqual(result.status,0)
        self.assertEqual(set(phase for phase,_,_ in events),{0,1,2,6,7,8})
        self.assertEqual(events[-1],(8,1,1))
        self.assertIn((6,2,2),events)
    def test_manifest_publication_failure_names_manifest_and_keeps_assets(self):
        files={'zones6.bin':b'verified cached asset'};self.cached(files)
        target=self.root/'data/assets/assets-manifest.txt'
        target.mkdir();(target/'keep.txt').write_text('preserve')
        result=self.install(self.plan(files))
        self.assertEqual(result.status,-1)
        self.assertEqual(result.file,b'assets-manifest.txt')
        self.assertIn(b'Cannot replace manifest:',result.detail)
        self.assertEqual((target/'keep.txt').read_text(),'preserve')
        self.assertEqual((self.root/'data/assets/zones6.bin').read_bytes(),files['zones6.bin'])
        self.assertFalse((self.root/'data/assets/.assets-manifest.tmp').exists())
    def test_manifest_identity_failure_is_distinct_from_io_failure(self):
        files={'zones6.bin':b'verified cached asset'};self.cached(files);plan=self.plan(files)
        data=bytearray(plan.read_bytes());struct.pack_into('<I',data,28,0x12345678);plan.write_bytes(data)
        result=self.install(plan)
        self.assertEqual(result.status,-4)
        self.assertEqual(result.file,b'.assets-manifest.tmp')
        self.assertIn(b'Manifest checksum',result.detail)
        self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
    def test_previous_asset_layout_is_rejected_before_publication(self):
        files={'zones6.bin':b'verified cached asset'};self.cached(files);plan=self.plan(files)
        data=bytearray(plan.read_bytes());struct.pack_into('<I',data,32,1);plan.write_bytes(data)
        self.assertEqual(self.install(plan).status,-2)
        self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
    def test_preflight_cancellation_preserves_status_at_each_stage(self):
        for stage in (0,1,2,3,4):
            with self.subTest(stage=stage):
                callback=CALLBACK(lambda pointer,user:pointer.contents.phase!=stage)
                result=self.install(self.plan({'missing.bin':b'asset'}),callback=callback)
                self.assertEqual(result.status,-7)
                self.assertEqual(result.phase,stage)
                self.assertFalse((self.root/'data/assets/assets-manifest.txt').exists())
    def test_large_unrelated_directory_falls_back_without_deleting_unknowns(self):
        files={'ui.bin':b'ui'};self.cached(files)
        asset_dir=self.root/'data/assets'
        for i in range(4097):(asset_dir/f'unknown{i:04}.tmp').touch()
        (asset_dir/'ui.bin.setup-tmp').write_bytes(b'interrupted')
        result=self.install(self.plan(files))
        self.assertEqual(result.status,0)
        self.assertFalse((asset_dir/'ui.bin.setup-tmp').exists())
        self.assertEqual(len(list(asset_dir.glob('unknown*.tmp'))),4097)
    def test_output_directory_link_refused(self):
        external=self.root/'outside';external.mkdir();(self.root/'data').symlink_to(external,target_is_directory=True)
        self.assertEqual(self.install(self.plan({'ui.bin':b'x'})).status,-1)
        self.assertEqual(list(external.iterdir()),[])
    def test_temporary_link_refused(self):
        files={'ui.bin':b'x'};self.cached(files);target=self.root/'precious';target.write_bytes(b'preserve')
        (self.root/'data/assets/.assets-manifest.tmp').symlink_to(target)
        self.assertEqual(self.install(self.plan(files)).status,-1);self.assertEqual(target.read_bytes(),b'preserve')
    def test_interrupted_scratch_reclaimed_before_low_space_check(self):
        files={'keep.bin':b'committed','title_route.bin':b'planned replay'}
        self.cached({'keep.bin':files['keep.bin']})
        data,_=make_plan(catalog(files),{'keep.bin':'decompile/extracted/ASSET.BIN'},
                        conversions={'title_route.bin':(91,'decompile/extracted/1ST_READ.BIN',(0,0,0,0))})
        plan=self.root/'install.recipe';plan.write_bytes(data)
        scratch=('data/cache/replay-records-0.tmp','data/assets/title_route.bin.setup-tmp')
        for name in scratch:
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'x'*4096)
        required=4*1024*1024+192*1024*1024+len(files['title_route.bin'])+32768
        self.lib.test_capacity_budget(required+1024)
        result=self.install(plan)
        # The reclaimed space allows setup to reach source discovery. With
        # either stale file retained this synthetic card fails with SPACE.
        self.assertEqual(result.status,-3,result.detail.decode())
        self.assertEqual(result.required_bytes,required)
        self.assertEqual(self.lib.test_capacity_calls(),1)
        self.assertTrue(all(not (self.root/name).exists() for name in scratch))
        self.assertEqual((self.root/'data/assets/keep.bin').read_bytes(),files['keep.bin'])
        self.lib.test_capacity_budget(required-1)
        self.assertEqual(self.install(plan).status,-5)
    def test_reclaim_only_recognized_scratch_preserves_user_and_completed_files(self):
        saved=self.root/'PSP/SAVEDATA/JGR_PORTABLE/jgr_story.sav.tmp'
        saved.parent.mkdir(parents=True);saved.write_bytes(b'pending progress')
        self.root=self.root/'PSP/GAME/JGR_PORTABLE'
        files={'ui.bin':b'committed','nested/voice.bin':b'voice'};self.cached(files)
        stale=['data/assets/.assets-manifest.tmp','data/assets/ui.bin.setup-tmp','data/assets/nested/voice.bin.setup-tmp']
        for pattern,count in (('replay-records-%d.tmp',3),('replay-output-%d.tmp',5),
                              ('jgc-setup-%02d.tmp',13),('jgs-setup-%d.tmp',7),
                              ('props-setup-%d.tmp',8),('city-props-input-%d.tmp',2),
                              ('roster-setup-%d.tmp',12),('ped-table-%d.tmp',2),('tunnel-setup-%d.tmp',2)):
            stale.extend('data/cache/'+pattern%i for i in range(count))
        stale.extend('data/cache/'+name for name in ('jgs-city-raw.tmp','jgs-city-keys.tmp','tunnel-table.tmp','mission-cast-table.tmp','city-table-capture.tmp','model-textures.tmp'))
        keep=('data/assets/unknown.bin.setup-tmp','data/cache/replay-records-3.tmp',
              'data/cache/replay-output-5.tmp','data/cache/jgc-setup-13.tmp',
              'data/cache/custom.tmp','data/cache/stage1.jrs','data/cache/level.txt',
              'data/log/setup.log')
        for name in stale+list(keep):
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(name.encode())
        link=self.root/'data/cache/unknown-link.tmp';link.symlink_to(self.root/'data/cache/custom.tmp')
        self.assertEqual(self.install(self.plan(files)).status,0)
        self.assertTrue(all(not (self.root/name).exists() for name in stale))
        for name in keep:self.assertEqual((self.root/name).read_bytes(),name.encode())
        self.assertEqual(saved.read_bytes(),b'pending progress')
        for name,content in files.items():self.assertEqual((self.root/'data/assets'/name).read_bytes(),content)
        self.assertTrue(link.is_symlink())
    def test_reclaim_refuses_known_scratch_links_and_linked_parents(self):
        files={'ui.bin':b'committed'};self.cached(files);plan=self.plan(files)
        precious=self.root/'precious';precious.write_bytes(b'preserve')
        cache=self.root/'data/cache';cache.mkdir()
        for name in ('data/cache/replay-output-4.tmp','data/assets/ui.bin.setup-tmp','data/cache/model-textures.tmp','data/cache/city-table-capture.tmp'):
            with self.subTest(name=name):
                link=self.root/name;link.symlink_to(precious)
                self.assertEqual(self.install(plan).status,-1)
                self.assertTrue(link.is_symlink());self.assertEqual(precious.read_bytes(),b'preserve');link.unlink()
        directory=cache/'replay-records-0.tmp';directory.mkdir();(directory/'keep').write_bytes(b'keep')
        self.assertEqual(self.install(plan).status,-1)
        self.assertEqual((directory/'keep').read_bytes(),b'keep');(directory/'keep').unlink();directory.rmdir()
        external=self.root/'external';external.mkdir();victim=external/'voice.bin.setup-tmp';victim.write_bytes(b'keep')
        linked=self.root/'data/assets/nested';linked.symlink_to(external,target_is_directory=True)
        self.assertEqual(self.install(self.plan({'nested/voice.bin':b'voice'})).status,-1)
        self.assertTrue(linked.is_symlink());self.assertEqual(victim.read_bytes(),b'keep')
    def test_city_staging_cleanup_is_bound_to_plan_and_keeps_reusable_sidecar(self):
        files={'stage1.jgs':b'geometry','stage1.jrs':b'owners'};self.cached(files)
        source='decompile/afs/STAGE1/000_st1dat.bin'
        conversions={'stage1.jgs':(63,source,(1,0,0,0)),'stage1.jrs':(64,source,(1,0,0,0))}
        data,_=make_plan(catalog(files),{},conversions=conversions)
        plan=self.root/'install.recipe';plan.write_bytes(data)
        cache=self.root/'data/cache';cache.mkdir()
        for name in ('stage1.jgs.setup-tmp','stage1.jrs.setup-tmp','stage2.jrs.setup-tmp','stage1.jrs'):
            (cache/name).write_bytes(name.encode())
        self.assertEqual(self.install(plan).status,0)
        for name in ('stage1.jgs.setup-tmp','stage1.jrs.setup-tmp'):self.assertFalse((cache/name).exists())
        for name in ('stage2.jrs.setup-tmp','stage1.jrs'):self.assertEqual((cache/name).read_bytes(),name.encode())
    def test_invalid_plan_never_reclaims_scratch(self):
        plan=self.plan({'ui.bin':b'x'});bad=bytearray(plan.read_bytes());bad[-1]^=1;plan.write_bytes(bad)
        scratch=self.root/'data/cache/replay-output-0.tmp';scratch.parent.mkdir(parents=True);scratch.write_bytes(b'keep')
        self.assertEqual(self.install(plan).status,-2);self.assertEqual(scratch.read_bytes(),b'keep')
    def test_real_ui_converters_match_catalog(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to test your supported original disc')
        expected={r['path']:r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']}
        for name,(op,source,args) in dict(CONVERSIONS,**resource_recipes()).items():
            output=self.root/name
            with self.subTest(asset=name):
                self.assertEqual(self.lib.convert_file(os.fsencode(supplied),source.encode(),op,*args,os.fsencode(output)),0)
                data=output.read_bytes();self.assertEqual(len(data),expected[name]['bytes'])
                self.assertEqual(hashlib.sha256(data).hexdigest(),expected[name]['sha256'])

if __name__=='__main__':unittest.main()
