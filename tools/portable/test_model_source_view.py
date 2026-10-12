"""Immutable source views and bounded bulk preloading; no game data required."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/portable/tests'))
import test_native_disc as native_disc

DRIVER = r'''
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
static size_t free_heap=(size_t)-1;
static int deny_allocation;
int scratch_pressure;
static void *optional_source;
static void view_free(void *p){if(p==optional_source){scratch_pressure=0;optional_source=NULL;}free(p);}
static void *view_malloc(size_t n){if(deny_allocation){errno=ENOMEM;return NULL;}return malloc(n);}
#define malloc view_malloc
#define free view_free
#define jgr_scratch_heap_available view_heap_available
#include "portable_models_jobs.c"
#undef malloc
#undef free
#undef jgr_scratch_heap_available
size_t view_heap_available(void){return free_heap;}
static int conversion_lifetime(const char *gdi,const char *recipe,const char *directory){
 JgrDisc *disc=NULL;char error[192];assert(!jgr_disc_open(&disc,gdi,error,sizeof error));
 FILE *plan=fopen(recipe,"r+b"),*output=tmpfile();assert(plan&&output);
 assert(!jgr_scratch_setup_phase(0));assert(!fseek(plan,0,SEEK_END));long size=ftell(plan);assert(size>0);
 assert(!jgr_models_convert(disc,plan,0,(uint32_t)size,directory,output));assert(model_source_used);
 JgrScratchPool *pool=jgr_scratch_begin();assert(pool&&!pool->reclaim&&!pool->streams);assert(!jgr_scratch_end());
 /* A valid recipe with a nonexistent PLAY member fails after its source has
  * been admitted; converter cleanup must unregister the stack-owned scope. */
 unsigned char bad_member[4]={63,0,0,0};
 assert(!fseek(plan,32+PARAM_AT+8,SEEK_SET));assert(fwrite(bad_member,1,4,plan)==4&&!fflush(plan));
 assert(jgr_models_convert(disc,plan,0,(uint32_t)size,directory,output));assert(model_source_used);
 pool=jgr_scratch_begin();assert(pool&&!pool->reclaim&&!pool->streams);assert(!jgr_scratch_end());
 fclose(plan);fclose(output);jgr_disc_close(disc);return 0;
}
int main(int argc,char **argv){
 if(argc==4)return conversion_lifetime(argv[1],argv[2],argv[3]);
 assert(argc==5);JgrDisc *disc=NULL,*clone=NULL;char error[256];
 ModelSourceUsage usage={0};
 const unsigned char *single=(const unsigned char*)"decompile/afs/PLAYER/one.bin";
 const unsigned char *multi=(const unsigned char*)"decompile/afs/DEMO01/many.bin";
 model_source_count(&usage,single);model_source_count(&usage,multi);model_source_count(&usage,multi);
 assert(model_source_rows(&usage,single,0,0)==1);
 assert(!model_source_rows(&usage,single,1,0));
 assert(model_source_rows(&usage,single,1,2048)==1);
 assert(model_source_rows(&usage,multi,1,0)==2);
 assert(!model_source_rows(&usage,multi,1,1024));
 const char *excluded[]={"decompile/afs/STAGE1/geometry.bin","decompile/afs/U_GARAGE/geometry.bin","decompile/extracted/main.bin"};
 for(unsigned i=0;i<3;i++){model_source_count(&usage,(const unsigned char*)excluded[i]);assert(!model_source_rows(&usage,(const unsigned char*)excluded[i],0,0));}
 memset(&usage,0,sizeof usage);
 for(unsigned i=0;i<65;i++){unsigned char path[192];snprintf((char*)path,sizeof path,"decompile/afs/PLAYER/%u.bin",i);model_source_count(&usage,path);assert(model_source_rows(&usage,path,0,0)==(i<64));}
 assert(usage.count==64);
 assert(!jgr_disc_open_fixture(&disc,argv[1],error,sizeof error));
 JgrDiscFile file={(uint64_t)strtoull(argv[2],NULL,10),(uint32_t)strtoul(argv[3],NULL,10)};
 int corrupt=atoi(argv[4]);JgrScratchPool pool;jgr_scratch_init(&pool,32768);ModelSource source={0};JgrDiscReadStats start,loaded,read;
 jgr_disc_read_stats(disc,&start);
 int result=model_source_prepare(&source,&pool,disc,&file,2);
 if(corrupt){assert(result<0&&!source.data&&!source.view.disc);jgr_disc_close(disc);return 0;}
 assert(!result&&source.data&&source.view.disc==disc&&pool.reclaim==&source.reclaim);
 jgr_disc_read_stats(disc,&loaded);
 assert(loaded.sequential_reads-start.sequential_reads==2);
 assert(loaded.physical_reads-start.physical_reads==2);
 unsigned char bytes[128];
 for(unsigned i=0;i<100;i++){unsigned at=(i*7919u)%(file.size-sizeof bytes);
  assert(!jgr_disc_read(disc,&file,at,bytes,sizeof bytes));assert(!memcmp(bytes,source.data+at,sizeof bytes));}
 jgr_disc_read_stats(disc,&read);assert(read.physical_reads==loaded.physical_reads);
 JgrDiscViewStats hits;jgr_disc_view_stats(disc,&hits);assert(hits.reads==100&&hits.bytes==12800);
 assert(jgr_disc_read(disc,&file,file.size-1,bytes,2));
 assert(jgr_disc_read(disc,&file,0,NULL,1));
 assert(!jgr_disc_read(disc,&file,file.size,NULL,0));
 assert(jgr_disc_view_begin(disc,&source.view,&file,source.data,file.size));
 /* Inner extent shadows only itself. Outer extents remain independently live. */
 JgrDiscView inner={0};JgrDiscFile other={file.offset+7,file.size-7};
 assert(!jgr_disc_view_begin(disc,&inner,&other,source.data+7,other.size));
 assert(jgr_disc_view_end(&source.view));
 assert(!jgr_disc_read(disc,&other,0,bytes,sizeof bytes));assert(!memcmp(bytes,source.data+7,sizeof bytes));
 assert(!jgr_disc_read(disc,&file,0,bytes,sizeof bytes));assert(!memcmp(bytes,source.data,sizeof bytes));
 assert(!jgr_disc_view_end(&inner));
 assert(!jgr_disc_clone(&clone,disc,error,sizeof error));
 assert(!jgr_disc_read(clone,&file,0,bytes,sizeof bytes));
 jgr_disc_view_stats(clone,&hits);assert(!hits.reads);jgr_disc_close(clone);
 JgrDiscView invalid={0};assert(jgr_disc_view_begin(disc,&invalid,&file,source.data,file.size-1));
 assert(jgr_disc_view_begin(NULL,&invalid,&file,source.data,file.size));
 assert(!invalid.disc);
 /* A scratch allocation within a single geometry row must reclaim the actual
  * registered source before spilling, then continue to serve copied data. */
 optional_source=source.data;scratch_pressure=1;
 FILE *work=jgr_scratch_open(&pool,"must-not-spill.tmp");assert(work);
 unsigned char output[12000];memset(output,0x56,sizeof output);
 assert(fwrite(output,1,sizeof output,work)==sizeof output&&!fflush(work));
 assert(!source.data&&!source.view.disc&&!source.reclaim.pool&&!pool.reclaim);
 assert(!pool.spills&&!pool.opens&&!scratch_pressure);
 rewind(work);assert(fread(output,1,sizeof output,work)==sizeof output);
 for(unsigned i=0;i<sizeof output;i++)assert(output[i]==0x56);
 assert(!jgr_scratch_discard(&pool,work));
 assert(!model_source_prepare(&source,&pool,disc,&file,2)&&!source.data);
 memset(&source,0,sizeof source);
 assert(!model_source_prepare(&source,&pool,disc,&file,2)&&source.data);
 /* Between-row headroom checks also reclaim without reloading it. */
 free_heap=MODEL_SOURCE_HEADROOM-1;
 assert(!model_source_prepare(&source,&pool,disc,&file,2)&&!source.data&&!source.view.disc);
 free_heap=(size_t)-1;
 assert(!model_source_prepare(&source,&pool,disc,&file,2)&&!source.data);
 memset(&source,0,sizeof source);deny_allocation=1;errno=0;
 assert(!model_source_prepare(&source,&pool,disc,&file,2)&&!source.data&&errno==0);
 deny_allocation=0;memset(&source,0,sizeof source);free_heap=MODEL_SOURCE_HEADROOM+file.size;
 assert(!model_source_prepare(&source,&pool,disc,&file,2)&&!source.data);
 free_heap=(size_t)-1;memset(&source,0,sizeof source);
 model_source_suppressed=1;assert(!model_source_prepare(&source,&pool,disc,&file,2)&&!source.data);
 model_source_suppressed=0;memset(&source,0,sizeof source);
 assert(!model_source_prepare(&source,&pool,disc,&file,2)&&source.data);
 /* Closing never frees borrowed storage, but makes a later end harmless. */
 jgr_disc_close(disc);assert(!source.view.disc);model_source_release(&source);assert(!pool.reclaim);
 return 0;
}
'''


SCRATCH_DRIVER = r'''
#include <stdlib.h>
#include <errno.h>
extern int scratch_pressure;
static void *pressure_realloc(void *p,size_t n){if(scratch_pressure){errno=ENOMEM;return NULL;}return realloc(p,n);}
#define realloc pressure_realloc
#include "portable_scratch.c"
'''

class ModelSourceViewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-source-view-')
        cls.folder = Path(cls.temp.name)
        bridge = cls.folder / 'check.c'; bridge.write_text(DRIVER)
        cls.exe = cls.folder / 'check'
        scratch = cls.folder / 'scratch.c'
        scratch.write_text(SCRATCH_DRIVER)
        names = ('portable_models', 'portable_models_colors', 'portable_models_pack',
                 'portable_disc', 'portable_convert', 'portable_indexed')
        subprocess.run(['cc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-ffp-contract=off', '-fsanitize=address,undefined', '-DJGR_DISC_TESTING=1',
                        '-I' + str(ROOT / 'psp/src'), str(bridge), str(scratch),
                        *[str(ROOT / 'psp/src' / (name + '.c')) for name in names],
                        '-lm', '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, raw, corrupt=False):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            gdi, source, _ = native_disc.NativeDiscTests.large_sequential_fixture(folder, raw)
            if corrupt:
                track = folder / 'track data.bin'; data = bytearray(track.read_bytes())
                data[123 + 640 * 2352 + 15] = 2; track.write_bytes(data)
            subprocess.run([str(self.exe), str(gdi), str(source.offset), str(source.size),
                            str(int(corrupt))], check=True,
                           env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0' if sys.platform == 'darwin' else 'detect_leaks=1'})

    def test_raw_unaligned_two_chunk_source_and_lifetimes(self):
        self.run_case(True)

    def test_cooked_two_chunk_source_and_fallbacks(self):
        self.run_case(False)

    def test_converter_success_and_geometry_failure_unregister_source_scope(self):
        supplied = os.environ.get('JGR_TEST_GDI')
        if not supplied:
            self.skipTest('Set JGR_TEST_GDI for real converter cleanup paths')
        from model_recipes import recipes
        with tempfile.TemporaryDirectory(prefix='jgr-source-lifetime-') as tmp:
            folder = Path(tmp); plan = folder / 'source.recipe'
            plan.write_bytes(recipes()['chars.jgc']['block'])
            subprocess.run([self.exe, supplied, plan, folder], check=True,
                           env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0' if sys.platform == 'darwin' else 'detect_leaks=1'})

    def test_corrupt_raw_header_never_publishes_a_view(self):
        self.run_case(True, True)


if __name__ == '__main__':
    unittest.main()
