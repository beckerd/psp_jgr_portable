"""Native observation-to-JSD proof using opt-in private completed actor captures.

This tests the observer and ABI aggregation, not disc-only replay execution.
No observations or converted timeline values are distributed in this test.
"""
import csv,ctypes as C,hashlib,json,os,subprocess,tempfile,unittest
from pathlib import Path
from replay_demo_table_recipes import ROOT,emit
BRIDGE=r'''
#include "portable_replay_demo_table.h"
#include "portable_replay_demo.h"
#include <stdlib.h>
int observe(const char *raw,const PortableReplayCue *cues,unsigned count,const char *target){
 FILE *f=fopen(raw,"rb"),*o=NULL;JgrReplayDemo *d=jgr_replay_demo_create();int result=-1;unsigned char row[4192],header[16];
 if(!f||!d||fread(header,1,16,f)!=16)goto done;
 for(;;){size_t n=fread(row,1,sizeof row,f);if(!n){if(ferror(f))goto done;break;}if(n!=sizeof row||jgr_replay_demo_sample(d,row))goto done;}
 o=fopen(target,"wb");if(o)result=jgr_replay_demo_finish(d,cues,count,o);
 done:if(f)fclose(f);if(o&&fclose(o))result=-1;jgr_replay_demo_destroy(d);return result;
}
int aggregate_files(const char *gdi,const char **names,const char *target){
 JgrDisc *disc=NULL;char error[192];unsigned char *rows[19]={0};unsigned sizes[19]={0};FILE *out=NULL;int result=-1;
 if(jgr_disc_open(&disc,gdi,error,sizeof error))goto done;
 for(unsigned i=0;i<19;i++){FILE *f=fopen(names[i],"rb");if(!f)goto done;if(fseek(f,0,SEEK_END)){fclose(f);goto done;}long n=ftell(f);if(n<16||n>65536||fseek(f,0,SEEK_SET)){fclose(f);goto done;}sizes[i]=(unsigned)n;rows[i]=malloc(n);if(!rows[i]||fread(rows[i],1,n,f)!=(unsigned)n){fclose(f);goto done;}fclose(f);}
 out=fopen(target,"wb");if(out)result=jgr_replay_demo_table(disc,(const unsigned char**)rows,sizes,out);
 done:if(out&&fclose(out))result=-1;for(unsigned i=0;i<19;i++)free(rows[i]);jgr_disc_close(disc);return result;
}
'''
class Cue(C.Structure):_fields_=[(n,C.c_uint32) for n in ('tick','kind','bank','program','parent','task')]
class DemoTableTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();cls.folder=Path(cls.tmp.name);p=cls.folder/'bridge.c';p.write_text(BRIDGE);lib=cls.folder/'demo.so'
  subprocess.run(['cc','-shared','-fPIC','-std=c99','-O2','-ffp-contract=off','-D_XOPEN_SOURCE=700','-Wall','-Wextra','-Werror','-I'+str(ROOT/'psp/src'),str(p),*[str(ROOT/'psp/src'/f'{n}.c') for n in ('portable_replay_demo_table','portable_replay_demo','portable_tables','portable_disc','portable_convert')],'-lm','-o',str(lib)],check=True)
  cls.lib=C.CDLL(str(lib));cls.lib.observe.argtypes=[C.c_char_p,C.POINTER(Cue),C.c_uint,C.c_char_p];cls.lib.aggregate_files.argtypes=[C.c_char_p,C.POINTER(C.c_char_p),C.c_char_p]
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_selectors_contain_no_observed_values(self):
  self.assertEqual(emit(),(ROOT/'psp/src/portable_replay_demo_table_data.inc').read_text())
 def test_native_observer_and_all_demo_aggregate_match_catalog(self):
  gdi=os.environ.get('JGR_TEST_GDI');manifest=os.environ.get('JGR_TEST_DEMO_MANIFEST')
  if not gdi or not manifest:self.skipTest('Set JGR_TEST_GDI and JGR_TEST_DEMO_MANIFEST for private raw-capture proof')
  reports=json.loads(Path(manifest).read_text());jobs=[r for r in json.loads((ROOT/'tools/portable/capture_jobs.json').read_text())['jobs'] if r['kind']=='demo'];self.assertEqual(len(reports),19)
  names=[]
  for i,(report,job) in enumerate(zip(reports,jobs)):
   self.assertEqual(report['path'],job['path']);source=ROOT/report['source']
   with (source/'audio.csv').open() as f:events=list(csv.DictReader(f))
   cues=(Cue*len(events))(*(Cue(*(int(r[n]) for n in ('tick','kind','bank','program','parent','task'))) for r in events))
   output=self.folder/f'demo{i}.rdc';self.assertEqual(self.lib.observe(os.fsencode(source/'title_actor_camera.bin'),cues,len(cues),os.fsencode(output)),0,job['key']);names.append(os.fsencode(output))
  paths=(C.c_char_p*19)(*names);output=self.folder/'aggregate.jsd'
  self.assertEqual(self.lib.aggregate_files(os.fsencode(gdi),paths,os.fsencode(output)),0)
  row=next(r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files'] if r['path']=='source_data_ptr_challenge_demo_data.jsd')
  self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(),row['sha256'])
  first=Path(os.fsdecode(names[0]));original=first.read_bytes()
  for bad in (original[:-1],original[:8]+b'\xff'*4+original[12:],original[:16]+b'\xff'*4+original[20:]):
   first.write_bytes(bad);self.assertNotEqual(self.lib.aggregate_files(os.fsencode(gdi),paths,os.fsencode(output)),0);self.assertEqual(output.read_bytes(),b'')
  first.write_bytes(original)
 def test_psp_compile(self):
  cc=os.environ.get('PSP_CC')
  if not cc:self.skipTest('Set PSP_CC')
  subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-DJGR_PORTABLE_ASSETS=1','-include',str(ROOT/'psp/src/portable_assets.h'),'-I'+str(ROOT/'psp/src'),'-c',str(ROOT/'psp/src/portable_replay_demo_table.c'),'-o',str(self.folder/'demo.o')],check=True)
if __name__=='__main__':unittest.main()
