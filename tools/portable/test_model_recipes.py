"""Metadata validation and full native-GDI parity for base model jobs."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import model_recipes

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include "portable_models_jobs.h"
#include "portable_scratch.h"
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){
 if(argc==3 && !strcmp(argv[1],"lights")){JgrDisc *d=NULL;char error[256];JgrModelLight light;if(jgr_disc_open(&d,argv[2],error,sizeof error))return 7;int r=jgr_model_baked_light(d,&light);jgr_disc_close(d);if(r)return 8;return fwrite(&light,1,sizeof light,stdout)==sizeof light?0:9;}
 if(argc<3)return 2;FILE *plan=fopen(argv[2],"rb");if(!plan)return 3;
 fseek(plan,0,SEEK_END);long n=ftell(plan);if(n<0||n>0x7fffffff)return 4;fseek(plan,7,SEEK_SET);
 if(!strcmp(argv[1],"validate")){int r=jgr_model_recipe_validate(plan,0,(uint32_t)n);if(ftell(plan)!=7)return 5;fclose(plan);return r?1:0;}
 if(argc!=6 && argc!=7)return 6;JgrDisc *disc=NULL;char error[256];if(jgr_disc_open(&disc,argv[3],error,sizeof error)){fprintf(stderr,"disc: %s\n",error);return 7;}
 jgr_scratch_setup_phase(0);FILE *out=fopen(argv[5],"wb"),*lighting=argc==7?fopen(argv[6],"wb"):NULL;int r=out && (argc==6 || lighting)?jgr_models_convert_with_lighting(disc,plan,0,(uint32_t)n,argv[4],out,lighting):-1;
 if(r)fprintf(stderr,"%s\n",jgr_models_error());
 if(lighting)fclose(lighting);if(out)fclose(out);jgr_disc_close(disc);fclose(plan);return r?8:0;
}
'''


class ModelRecipes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('cc'):
            raise unittest.SkipTest('Host C compiler unavailable')
        cls.tmp = tempfile.TemporaryDirectory(prefix='jgr-model-recipe-')
        cls.work = Path(cls.tmp.name)
        (cls.work/'driver.c').write_text(DRIVER)
        cls.driver = cls.work/'driver'
        sources = ['portable_models_jobs.c', 'portable_models_colors.c', 'portable_models_pack.c', 'portable_models.c', 'portable_disc.c',
                   'portable_convert.c', 'portable_indexed.c', 'portable_scratch.c']
        subprocess.run(['cc', '-std=gnu99', '-O2', '-ffp-contract=off', '-Wall', '-Wextra',
                        '-I', str(ROOT/'psp/src'), str(cls.work/'driver.c'),
                        *[str(ROOT/'psp/src'/n) for n in sources], '-lm', '-o', str(cls.driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def validate(self, block):
        plan = self.work/'test.recipe'
        plan.write_bytes(block)
        return subprocess.run([self.driver, 'validate', plan], capture_output=True).returncode

    def test_play_and_raw_metadata(self):
        for specs in (["Example:afs/PLAYER/000_dat.bin:afs/PLAYER/001_txr.bin:0,0,0,0:mot=4"],
                      ["Example:afs/ENEMY/001_dat.bin:afs/ENEMY/000_txr.bin:0,0,0,0:rawat:1000:1040:1080,1100:tex=2"]):
            recipe = model_recipes.recipe(specs)
            self.assertEqual(recipe['operation'], 30)
            self.assertEqual(recipe['args'], (0, 0, 0, 0))
            self.assertEqual(self.validate(recipe['block']), 0)

    def test_public_metadata_needs_no_private_inputs(self):
        jobs = model_recipes.recipes()
        self.assertGreaterEqual(len(jobs), 19)
        for name, row in jobs.items():
            with self.subTest(name=name):
                self.assertEqual(self.validate(row['block']), 0)

    def test_rejects_unread_payload_and_malformed_extents(self):
        recipe = model_recipes.recipe([
            "Example:afs/ENEMY/001_dat.bin:afs/ENEMY/000_txr.bin:0,0,0,0:rawat:1000:1040:1080,1100"])
        original = recipe['block']
        for field, value in ((8, 2), (12, 684), (16, 0xffffffff), (28, 1),
                             (32+608+4*4, 0), (32+608+5*4, 0xffffffff),
                             (32+608+8*4, 0x80000000), (32+608+12*4, 1),
                             (len(original)-4, 0)):
            with self.subTest(field=field):
                bad = bytearray(original)
                struct.pack_into('<I', bad, field, value)
                self.assertEqual(self.validate(bad), 1)
        self.assertEqual(self.validate(original+b'unreferenced payload'), 1)
        self.assertEqual(self.validate(original[:-1]), 1)
        bad = bytearray(original)
        bad[32:32+192] = b'decompile/afs/../secret\0'.ljust(192, b'\0')
        self.assertEqual(self.validate(bad), 1)
        bad = bytearray(original)
        struct.pack_into('<I', bad, 32+592, 0x7fc00000)
        self.assertEqual(self.validate(bad), 1)

    @unittest.skipUnless(os.environ.get('JGR_TEST_GDI'), 'Supported original disc not supplied')
    def test_scratch_failure_reports_stage_before_cleanup(self):
        plan = self.work/'scratch-failure.recipe'
        plan.write_bytes(model_recipes.recipes()['bantam_openings.jgc']['block'])
        missing = self.work/'blocked-cache-directory'
        missing.mkdir()
        (missing/'jgc-setup-00.tmp').mkdir()
        output = self.work/'scratch-failure.jgc'
        run = subprocess.run([self.driver, 'convert', plan, os.environ['JGR_TEST_GDI'], missing, output],
                             capture_output=True, text=True)
        self.assertEqual(run.returncode, 8)
        self.assertIn('Model conversion: inspect scratch stream (item 0, errno ', run.stderr)
        self.assertTrue((missing/'jgc-setup-00.tmp').is_dir())
        self.assertEqual(output.read_bytes(), b'')

    @unittest.skipUnless(os.environ.get('JGR_TEST_GDI'), 'Supported original disc not supplied')
    def test_cast_and_lighting_share_geometry_with_exact_output(self):
        plan = self.work/'cast-pair.recipe'
        recipes = model_recipes.recipes()
        plan.write_bytes(recipes['chars.jgc']['block'])
        outputs = [self.work/'paired-chars.jgc', self.work/'paired-playerlight.bin']
        run = subprocess.run([self.driver, 'convert', plan, os.environ['JGR_TEST_GDI'],
                              self.work, *outputs], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        catalog = {r['path']: r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']}
        for name, output in zip(('chars.jgc', 'playerlight.bin'), outputs):
            data = output.read_bytes()
            self.assertEqual(len(data), catalog[name]['bytes'], name)
            self.assertEqual(hashlib.sha256(data).hexdigest(), catalog[name]['sha256'], name)
        self.assertFalse(list(self.work.glob('jgc-setup-*.tmp')))

    def test_no_copied_texture_maps_or_unsupported_modifiers(self):
        row = 'Example:afs/ENEMY/001_dat.bin:afs/ENEMY/000_txr.bin:0,0,0,0:rawat:1000:1040:1080'
        for suffix in (':texmap=0,1,2', ':cull=front', ':alpha=5', ':lighting=3', ':blend=1,8'):
            with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                model_recipes.recipe([row+suffix])
        with self.assertRaises(ValueError):
            model_recipes.recipe([row], {'baked-light': 'profile'})

    @unittest.skipUnless(all(os.environ.get(k) for k in ('JGR_TEST_GDI', 'JGR_TEST_SOURCE_TREE', 'JGR_TEST_ASSETS')),
                         'Private GDI, fresh source tree and expected asset pack not supplied')
    def test_native_gdi_catalog_parity(self):
        recipes = model_recipes.recipes(Path(os.environ['JGR_TEST_SOURCE_TREE']))
        public = model_recipes.recipes()
        self.assertEqual({k:v['block'] for k,v in recipes.items()}, {k:v['block'] for k,v in public.items()})
        required = {f'people{stage}{suffix}.jgc' for stage in (1, 2, 3, 5) for suffix in ('', '_compact')} | {'record_book.jgc'}
        self.assertTrue(required <= set(recipes))
        results = []
        for name, recipe in recipes.items():
            with self.subTest(name=name):
                plan, output = self.work/(name+'.recipe'), self.work/name
                plan.write_bytes(recipe['block'])
                run = subprocess.run([self.driver, 'convert', plan, os.environ['JGR_TEST_GDI'], self.work, output],
                                     capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, run.stderr)
                data = output.read_bytes()
                expected = (Path(os.environ['JGR_TEST_ASSETS'])/name).read_bytes()
                self.assertEqual(data, expected)
                self.assertFalse(list(self.work.glob('jgc-setup-*.tmp')))
                results.append({'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                                'recipe_sha256': hashlib.sha256(recipe['block']).hexdigest(),
                                'recipe_bytes': len(recipe['block'])})
        report = os.environ.get('JGR_MODEL_CATALOG_REPORT')
        if report:
            output = Path(report)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps({'kind': 'native-gdi-final-catalog-parity', 'cases': results}, indent=2)+'\n')

    @unittest.skipUnless(all(os.environ.get(k) for k in ('JGR_TEST_GDI', 'JGR_TEST_SOURCE_TREE')),
                         'Private original input not supplied')
    def test_source_baked_light_profile(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location('light_reference', ROOT/'decompile/tools/baked_light.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        data = (Path(os.environ['JGR_TEST_SOURCE_TREE'])/'decompile/extracted/1ST_READ.BIN').read_bytes()
        profile = module.profile_from_executable(data)
        expected = struct.pack('<11d6I', *profile['direction'], profile['diffuse'], profile['ambient'],
                               *profile['rgb'], *profile['cel_direction'],
                               *profile['cel_colors'][0], *profile['cel_colors'][1])
        run = subprocess.run([self.driver, 'lights', os.environ['JGR_TEST_GDI']], capture_output=True)
        self.assertEqual(run.returncode, 0)
        self.assertEqual(run.stdout, expected)


if __name__ == '__main__':
    unittest.main()
