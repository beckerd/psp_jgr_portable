"""Native scene archive/text/staff-card parity against private original inputs."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import graffiti_recipes
ROOT=Path(__file__).resolve().parents[2]
DRIVER=r'''
#include "portable_graffiti_convert.h"
#include "portable_graffiti_convert.h"
#include <stdlib.h>
int main(int argc,char **argv){
 if(argc!=6)return 2;JgrDisc *d=NULL;char error[256];JgrDiscFile source;
 if(jgr_disc_open(&d,argv[1],error,sizeof error)){fprintf(stderr,"%s\n",error);return 3;}
 unsigned op=(unsigned)atoi(argv[2]);int r=jgr_disc_source(d,argv[3],&source);
 if(!r){FILE *out=fopen(argv[5],"wb");r=out?(jgr_graffiti_convert(d,&source,op,(unsigned)atoi(argv[4]),out)):-1;if(out)fclose(out);}
 jgr_disc_close(d);return r?4:0;
}
'''
class GraffitiConvert(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  if not shutil.which('cc'):raise unittest.SkipTest('C compiler unavailable')
  cls.tmp=tempfile.TemporaryDirectory(prefix='jgr-graffiti-convert-');cls.work=Path(cls.tmp.name);cls.driver=cls.work/'driver';source=cls.work/'driver.c';source.write_text(DRIVER)
  subprocess.run(['cc','-std=gnu99','-O2','-ffp-contract=off','-I',str(ROOT/'psp/src'),str(source),*[str(ROOT/'psp/src'/name) for name in ('portable_graffiti_convert.c','portable_disc.c')],'-lm','-o',str(cls.driver)],check=True)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_metadata(self):
  jobs=graffiti_recipes.recipes();self.assertEqual(len(jobs),23)
  for op,source,args in jobs.values():self.assertIn(op,(84,85,86,87));self.assertTrue(source.startswith('decompile/'));self.assertEqual(len(args),4)
 @unittest.skipUnless(os.environ.get('JGR_TEST_GDI') and os.environ.get('JGR_TEST_ASSETS'),'Private original input not supplied')
 def test_native_graffiti_catalog(self):
  rows=[]
  for name,(op,source,args) in graffiti_recipes.recipes().items():
   with self.subTest(name=name):
    output=self.work/name;run=subprocess.run([self.driver,os.environ['JGR_TEST_GDI'],str(op),source,str(args[0]),output],capture_output=True,text=True)
    self.assertEqual(run.returncode,0,run.stderr);data=output.read_bytes();self.assertEqual(data,(Path(os.environ['JGR_TEST_ASSETS'])/name).read_bytes())
    rows.append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
  if os.environ.get('JGR_GRAFFITI_REPORT'):Path(os.environ['JGR_GRAFFITI_REPORT']).write_text(json.dumps(dict(kind='native-disc-graffiti-parity',cases=rows),indent=2)+'\n')
if __name__=='__main__':unittest.main()
