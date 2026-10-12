"""Authored mesh tests for allocation-independent material group sizing."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = r'''
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static unsigned poison, pending_hash, hash_allocations, group_allocations;
static size_t hash_bytes, allocation_limit;
static void *poison_malloc(size_t size);
#define malloc poison_malloc
#ifndef MODEL_SOURCE
#define MODEL_SOURCE "portable_models.c"
#endif
#include MODEL_SOURCE
#undef malloc
static void *poison_malloc(size_t size)
{
    if(size==MAX_GROUPS*sizeof(Group)){group_allocations++;pending_hash=1;}
    else if(pending_hash){hash_allocations++;hash_bytes=size;pending_hash=0;}
    if(size>allocation_limit){errno=ENOMEM;return NULL;}
    void *p=malloc(size);if(p)memset(p,(int)poison,size);return p;
}
typedef struct {unsigned char bytes[65536];unsigned size;} Input;
int jgr_disc_read(JgrDisc *disc,const JgrDiscFile *file,uint32_t at,void *out,size_t size)
{
    Input *in=(Input*)disc;
    if(at>file->size || size>file->size-at || file->offset>in->size ||
       at>in->size-file->offset || size>in->size-file->offset-at)return -1;
    memcpy(out,in->bytes+file->offset+at,size);return 0;
}
int jgr_disc_find(JgrDisc *disc,const char *name,JgrDiscFile *file)
{(void)disc;(void)name;(void)file;return -1;}
typedef struct {unsigned triangles[2],batches,next_texture,reverse;} Output;
static int emit(const JgrModelBatch *batch,void *opaque)
{
    Output *out=opaque;unsigned texture=out->next_texture++;
    unsigned triangles=out->triangles[2-texture];
    unsigned first=texture==2?0:out->triangles[0]*3;
    assert(batch->texture==texture && batch->flags==0 && batch->stride==24);
    assert(batch->vertices==triangles*3 && batch->indices==triangles*3);
    for(unsigned i=0;i<batch->vertices;i++){
        unsigned original=first+(out->reverse?(triangles-1-i/3)*3+i%3:i);
        unsigned char expected[24]={0};w32(expected+8,0xffffffffu);
        wf(expected+12,original);wf(expected+16,original%7);wf(expected+20,-1);
        assert(!memcmp(batch->vertex_bytes+i*24,expected,sizeof expected));
        assert(u16(batch->index_bytes+i*2)==i);
        assert(batch->positions[i*3]==original && batch->positions[i*3+1]==original%7 && batch->positions[i*3+2]==-1);
    }
    out->batches++;return 0;
}
static void fixture(Input *in,unsigned first,unsigned second)
{
    memset(in,0,sizeof *in);unsigned count=(first+second)*3,vl=64;
    unsigned end=vl+8+count*12,pl=end+4;
    w32(in->bytes+16,vl);w32(in->bytes+20,pl);
    w16(in->bytes+vl,34);w16(in->bytes+vl+2,1+count*3);
    w16(in->bytes+vl+6,count);
    for(unsigned i=0;i<count;i++){
        wf(in->bytes+vl+8+i*12,i);wf(in->bytes+vl+12+i*12,i%7);wf(in->bytes+vl+16+i*12,-1);
    }
    in->bytes[end]=255;unsigned index=0;
    for(unsigned group=0;group<2;group++){
        unsigned n=group?second:first;w16(in->bytes+pl,8);w16(in->bytes+pl+2,2-group);pl+=4;
        w16(in->bytes+pl,65);w16(in->bytes+pl+2,1+n*10);w16(in->bytes+pl+4,n);pl+=6;
        for(unsigned j=0;j<n;j++){
            w16(in->bytes+pl,3);pl+=2;
            for(unsigned k=0;k<3;k++){w16(in->bytes+pl,index++);pl+=6;}
        }
    }
    in->bytes[pl]=255;in->size=pl+2;
}
int main(int argc,char **argv)
{
    assert(argc==3);unsigned scenario=(unsigned)atoi(argv[1]);poison=(unsigned)strtoul(argv[2],NULL,0);
    unsigned first=scenario==0?1:43,second=scenario==0?1:2;
    Input in;fixture(&in,first,second);JgrDiscFile file={0,in.size};
    JgrModelCache *cache=jgr_model_cache_create();assert(cache);
    JgrModelOptions options;jgr_model_options_default(&options);options.texture_count=3;options.light=0;
    /* Small materials, including ones eligible for subdivision, must build
     * when a gratuitous 512 KiB table would exceed the allocation limit. */
    allocation_limit=65536u;
    if(scenario==2)options.max_edge=1000;
    Output out={{first,second},0,1,scenario==2};
    assert(!jgr_model_mesh(cache,(JgrDisc*)&in,&file,0,16,&options,emit,&out));
    assert(out.batches==2 && group_allocations==1 && hash_allocations==1);
    /* Texture 1 is the smaller material and is visited first after sorting.
     * A larger later material grows storage only when it is actually used. */
    assert(hash_bytes==512u);
    jgr_model_cache_free(cache);return 0;
}
'''


class ModelGroups(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('cc')
        if not compiler:
            raise unittest.SkipTest('Host C compiler unavailable')
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-model-groups-')
        work = Path(cls.temp.name)
        source = work/'groups.c'
        source.write_text(SOURCE)
        cls.exe = work/'groups'
        command = [compiler, '-std=c99', '-O1', '-g', '-ffp-contract=off',
                   '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                   '-fno-omit-frame-pointer', '-I'+str(ROOT/'psp/src')]
        if os.environ.get('JGR_TEST_MODEL_SOURCE'):
            command += ['-DMODEL_SOURCE="'+os.environ['JGR_TEST_MODEL_SOURCE']+'"']
        subprocess.run(command+[str(source), '-lm', '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, scenario):
        for poison in (0, 1, 0x5a, 0xa5, 0xff):
            with self.subTest(poison=poison):
                run = subprocess.run([str(self.exe), str(scenario), str(poison)], capture_output=True)
                self.assertEqual(run.returncode, 0, run.stderr.decode())

    def test_small_mesh_survives_bounded_allocation_with_dirty_memory(self):
        self.run_case(0)

    def test_first_triangle_counts_at_hash_growth_boundary(self):
        self.run_case(1)

    def test_subdivision_avoids_large_hash_and_keeps_exact_mesh(self):
        self.run_case(2)


if __name__ == '__main__':
    unittest.main()
