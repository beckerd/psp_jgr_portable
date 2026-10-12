"""Speculative city-node discovery rejects non-finite source words without FP."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include <assert.h>
#include <fenv.h>
#include "portable_stage.c"

static int reference(uint32_t p,uint32_t s){
 float pf,sf;memcpy(&pf,&p,4);memcpy(&sf,&s,4);
 volatile double position=pf,scale=sf;
 return position> -1e6&&position<1e6&&scale>1e-4&&scale<1e3;
}
static void compare(uint32_t p,uint32_t s){
 assert(!feclearexcept(FE_ALL_EXCEPT));
 int actual=city_node_component_valid(p,s);
 assert(!fetestexcept(FE_ALL_EXCEPT));assert(actual==reference(p,s));
}
static void special(void){
 const uint32_t unusual[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,
  0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc00000,0xffc00000,
  0x7f800001,0xff800001,0xfffffffe,0xffff9873};
 for(unsigned i=0;i<sizeof unusual/sizeof *unusual;i++){
  compare(unusual[i],0x3f800000);compare(0,unusual[i]);
  for(unsigned j=0;j<sizeof unusual/sizeof *unusual;j++)compare(unusual[i],unusual[j]);
 }
 assert(city_node_component_valid(0,0x3f800000));
 assert(city_node_component_valid(0x80000000,0x3f800000));
 assert(!city_node_component_valid(0xfffffffe,0x3f800000));
 for(uint32_t p=0x497423fe;p<=0x49742402;p++){
  compare(p,0x3f800000);compare(p|0x80000000,0x3f800000);
  assert(city_node_component_valid(p,0x3f800000)==(p<0x49742400));
 }
 for(uint32_t s=0x38d1b715;s<=0x38d1b71a;s++){
  compare(0,s);assert(city_node_component_valid(0,s)==(s>0x38d1b717));
 }
 for(uint32_t s=0x4479fffe;s<=0x447a0002;s++){
  compare(0,s);assert(city_node_component_valid(0,s)==(s<0x447a0000));
 }
 puts("special values and strict boundaries passed");
}
static void random_pairs(void){
 uint32_t seed=1;for(unsigned i=0;i<1000000;i++){
  seed=1664525u*seed+1013904223u;uint32_t p=seed;
  seed=1664525u*seed+1013904223u;compare(p,seed);
 }
 puts("1000000 raw-word pairs match original comparisons with no FP flags");
}
static void source_scan(const char *gdi){
 JgrDisc *disc=NULL;char error[256];assert(!jgr_disc_open(&disc,gdi,error,sizeof error));
 const unsigned stages[]={1,2,3,5,6};puts("[");
 for(unsigned n=0;n<5;n++){
  Stage s={0};char path[96];unsigned stage=stages[n];s.disc=disc;
  snprintf(path,sizeof path,"decompile/afs/STAGE%u/000_st%udat.bin",stage,stage);
  assert(!jgr_disc_source(disc,path,&s.source));s.data=malloc(s.source.size);assert(s.data);
  assert(!jgr_disc_read(disc,&s.source,0,s.data,s.source.size));
  unsigned candidates=0,accepted=0,nonfinite=0;
  for(uint32_t at=SB;valid_data(&s,at,53);at+=4){
   uint32_t ev=dw(&s,at),model=dw(&s,at+4),child=dw(&s,at+44),sibling=dw(&s,at+48);
   if(ev>0xffff||(!model&&!child&&!sibling)||(model&&!dataptr(&s,model))||
      (child&&!dataptr(&s,child))||(sibling&&!dataptr(&s,sibling)))continue;
   int old=1,current=1,bad=0;candidates++;
   for(unsigned k=0;k<3;k++){
    uint32_t p=dw(&s,at+8+4*k),scale=dw(&s,at+32+4*k);
    compare(p,scale);old&=reference(p,scale);current&=city_node_component_valid(p,scale);
    bad|=(p&0x7f800000)==0x7f800000||(scale&0x7f800000)==0x7f800000;
   }
   assert(old==current);accepted+=current;nonfinite+=bad;
  }
  assert(nonfinite>0);
  printf("%s{\"stage\":%u,\"candidates\":%u,\"accepted\":%u,\"nonfinite\":%u}",n?",\n":"",stage,candidates,accepted,nonfinite);
  free(s.data);
 }
 puts("\n]");jgr_disc_close(disc);
}
int main(int argc,char **argv){
 assert(argc==2||argc==3);
 if(argc==3){source_scan(argv[2]);return 0;}
 if(!strcmp(argv[1],"special"))special();else random_pairs();return 0;
}
'''


class StageScanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-stage-scan-')
        folder = Path(cls.temp.name); source = folder / 'scan.c'
        source.write_text(DRIVER); cls.driver = folder / 'scan'
        names = ('portable_stage_pack', 'portable_scratch', 'portable_models',
                 'portable_disc', 'portable_convert', 'portable_indexed')
        subprocess.run(['cc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-ffp-contract=off', '-fsanitize=address,undefined',
                        '-I' + str(ROOT / 'psp/src'), str(source),
                        *[str(ROOT / 'psp/src' / (name + '.c')) for name in names],
                        '-lm', '-o', str(cls.driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_driver(self, *args):
        return subprocess.run([self.driver, *args], check=True, capture_output=True,
                              text=True, env={**os.environ, 'ASAN_OPTIONS':
                              'detect_leaks=0' if sys.platform == 'darwin' else 'detect_leaks=1'}).stdout

    def test_special_values_and_exact_boundaries(self):
        self.run_driver('special')

    def test_random_raw_bits_match_original_predicate_without_fp_exceptions(self):
        self.run_driver('random')

    @unittest.skipUnless(os.environ.get('JGR_TEST_GDI'), 'Private supported disc required')
    def test_every_candidate_on_all_five_stages_keeps_original_classification(self):
        report = json.loads(self.run_driver('source', os.environ['JGR_TEST_GDI']))
        self.assertEqual([row['stage'] for row in report], [1, 2, 3, 5, 6])
        self.assertTrue(all(row['nonfinite'] > 0 for row in report))
        if os.environ.get('JGR_STAGE_SCAN_REPORT'):
            Path(os.environ['JGR_STAGE_SCAN_REPORT']).write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
