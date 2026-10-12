"""Subdivision hash growth, collisions, bounds and allocation-failure checks."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static size_t fail_size,clear_bytes;static unsigned clear_calls;
static void *checked_malloc(size_t n){if(n==fail_size&&fail_size){fail_size=0;return NULL;}void *p=malloc(n);if(p)memset(p,0xa5,n);return p;}
static void *counted_memset(void *p,int c,size_t n){if(!c){clear_bytes+=n;clear_calls++;}return memset(p,c,n);}
#define malloc checked_malloc
#undef memset
#define memset counted_memset
#ifndef MODEL_SOURCE
#define MODEL_SOURCE "portable_models.c"
#endif
#include MODEL_SOURCE
#undef malloc
#undef memset
int jgr_disc_read(JgrDisc *d,const JgrDiscFile *f,uint32_t at,void *p,size_t n)
{(void)d;(void)f;(void)at;(void)p;(void)n;return -1;}
int jgr_disc_find(JgrDisc *d,const char *name,JgrDiscFile *f)
{(void)d;(void)name;(void)f;return -1;}
#ifndef BASELINE
static unsigned lookup(const Assembly *a,const Midpoints *m,unsigned left,unsigned right){
 unsigned slot=midpoint_hash(left,right,m->hash_size-1),steps=0;
 while(a->hash[slot]){assert(++steps<=m->hash_size);unsigned i=a->hash[slot]-1;
  if(m->data[i].left==left&&m->data[i].right==right)return i;
  slot=(slot+1)&(m->hash_size-1);
 }return UINT_MAX;
}
static void check_keys(unsigned count,int collisions){
 Assembly a={0};Midpoints m={0};m.hash_size=128;
 assert(!assembly_hash_reserve(&a,128));memset(a.hash,0,128*sizeof *a.hash);
 m.data=calloc(count,sizeof *m.data);assert(m.data);
 for(unsigned i=0;i<count;i++){
  if(m.count==m.hash_size/2){assert(!midpoint_grow(&a,&m));
   for(unsigned j=0;j<i;j++)assert(lookup(&a,&m,m.data[j].left,m.data[j].right)==j);
  }
  unsigned left=collisions?i*128:i,right=left+1;
  assert(lookup(&a,&m,left,right)==UINT_MAX);
  unsigned slot=midpoint_hash(left,right,m.hash_size-1);
  while(a.hash[slot])slot=(slot+1)&(m.hash_size-1);
  m.data[i]=(Midpoint){left,right,65536-i};a.hash[slot]=++m.count;
 }
 for(unsigned i=0;i<count;i++){unsigned found=lookup(&a,&m,m.data[i].left,m.data[i].right);
  assert(found==i&&m.data[found].vertex==65536-i);}
 if(count==65536){assert(m.hash_size==HASH_SIZE);assert(midpoint_grow(&a,&m));
  assert(lookup(&a,&m,65535,65536)==65535);}
 free(m.data);free(a.hash);
}
static void check_failures(void){
 Assembly a={0};assert(!assembly_hash_reserve(&a,128));uint32_t *saved=a.hash;
 const unsigned invalid[]={0,127,129,HASH_SIZE+1,UINT_MAX};
 for(unsigned i=0;i<sizeof invalid/sizeof *invalid;i++){assert(assembly_hash_reserve(&a,invalid[i]));assert(a.hash==saved&&a.hash_capacity==128);}
 Midpoints m={0};m.hash_size=128;fail_size=1024;
 assert(midpoint_grow(&a,&m));assert(!fail_size&&!a.hash&&!a.hash_capacity&&m.hash_size==128);
 assert(!assembly_hash_reserve(&a,128));assert(a.hash_capacity==128);free(a.hash);
 check_keys(512,1);check_keys(65536,0);
}
#endif
static void fixture(Assembly *a,unsigned scenario){
 if(scenario==0){
  a->nv=65536;a->vertices=calloc(a->nv,sizeof *a->vertices);assert(a->vertices);a->vcap=a->nv;
  for(unsigned i=0;i<512;i++){unsigned left=i*128;a->vertices[left+1].p[0]=2;a->vertices[left+2].p[0]=1;
   assert(!index_append(a,left)&&!index_append(a,left+1)&&!index_append(a,left+2));}
 }else{
  a->nv=a->vcap=3;a->vertices=calloc(3,sizeof *a->vertices);assert(a->vertices);
  a->vertices[1].p[0]=scenario==1?64:256;a->vertices[2].p[1]=scenario==1?64:256;
  for(unsigned i=0;i<3;i++)assert(!index_append(a,i));
 }
}
int main(int argc,char **argv){
 assert(argc==2);
#ifndef BASELINE
 if(!strcmp(argv[1],"checks")){check_failures();return 0;}
#endif
 unsigned scenario=(unsigned)atoi(argv[1]);Assembly a={0};JgrModelOptions options;
 jgr_model_options_default(&options);options.max_edge=1;a.options=&options;fixture(&a,scenario);
#ifdef BASELINE
 a.hash=malloc(HASH_SIZE*sizeof *a.hash);assert(a.hash);a.hash_mask=HASH_SIZE-1;
#else
 assert(!assembly_hash_reserve(&a,128));a.hash_mask=127;
#endif
 clear_bytes=0;clear_calls=0;int result=split_group(&a);
 assert(result==(scenario==2?-1:0));
 fprintf(stderr,"{\"result\":%d,\"clear_bytes\":%zu,\"clear_calls\":%u,\"vertices\":%u,\"indices\":%u}\n",result,clear_bytes,clear_calls,a.nv,a.ni);
 if(!result){
  assert(fwrite(&a.nv,sizeof a.nv,1,stdout)==1&&fwrite(&a.ni,sizeof a.ni,1,stdout)==1);
  for(unsigned i=0;i<a.nv;i++)assert(fwrite(a.vertices[i].p,sizeof(double),3,stdout)==3);
  assert(fwrite(a.indices,sizeof *a.indices,a.ni,stdout)==a.ni);
 }
 free(a.vertices);free(a.indices);free(a.hash);return 0;
}
'''


class ModelHash(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-model-hash-')
        cls.work = Path(cls.temp.name)
        source = cls.work/'driver.c'; source.write_text(DRIVER)
        cls.executables = {}
        for label, original in (('candidate', None), ('baseline', os.environ.get('JGR_TEST_MODEL_BASELINE'))):
            if label == 'baseline' and not original:
                continue
            exe = cls.work/label
            command = [os.environ.get('CC', 'cc'), '-std=c99', '-O1', '-g', '-ffp-contract=off',
                       '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                       '-fno-omit-frame-pointer', '-I'+str(ROOT/'psp/src')]
            if original:
                command += ['-DBASELINE=1', '-DMODEL_SOURCE="'+str(Path(original).resolve())+'"']
            subprocess.run(command+[str(source), '-lm', '-o', str(exe)], check=True)
            cls.executables[label] = exe

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, label, case):
        run = subprocess.run([str(self.executables[label]), str(case)], capture_output=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stderr.decode())
        return run

    def test_collision_chains_all_growth_boundaries_maximum_keys_and_allocation_failure(self):
        self.run_case('candidate', 'checks')

    def test_subdivision_shared_edges_growth_and_bound_failure(self):
        for case in range(3):
            with self.subTest(case=case):
                candidate = self.run_case('candidate', case)
                result = json.loads(candidate.stderr)
                if case == 0:
                    self.assertLess(result['clear_bytes'], 100000)
                if 'baseline' in self.executables:
                    baseline = self.run_case('baseline', case)
                    self.assertEqual(candidate.stdout, baseline.stdout)
                    old = json.loads(baseline.stderr)
                    for key in ('result', 'vertices', 'indices'):
                        self.assertEqual(result[key], old[key])
                    if case == 0:
                        self.assertLess(result['clear_bytes'], old['clear_bytes']/4)


if __name__ == '__main__':
    unittest.main()
