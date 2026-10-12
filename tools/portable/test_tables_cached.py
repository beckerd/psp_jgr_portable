"""Final table bindings against generated, catalog-verified geometry fixtures."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
HARNESS=r'''
#include "portable_tables.h"
#include "portable_stage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned blocked,opens;
static FILE *dependency(void *user,const char *name){char path[2048];opens++;if(blocked==1||(blocked==2&&strcmp(name,"stage1_obj.jgs"))||(blocked==3&&!strcmp(name,"stage1_obj.jgs"))||strchr(name,'/')||strstr(name,".."))return NULL;if(snprintf(path,sizeof path,"%s/%s",(const char*)user,name)>=(int)sizeof path)return NULL;return fopen(path,"rb");}
int main(int argc,char **argv){
    JgrDisc *disc=NULL,*other=NULL;char error[192];if(argc!=5&&argc!=6)return 2;
    if(jgr_disc_open(&disc,argv[1],error,sizeof error)){fprintf(stderr,"%s",error);return 3;}
    if(argc==6){
        jgr_table_city_cache_begin(disc,argv[4],dependency,argv[3]);
        unsigned stage=strstr(argv[2],"tunnel")?2:1;
        FILE *geometry=tmpfile(),*owners=tmpfile();if(!geometry||!owners)return 5;
        /* A fresh setup has placements before city, but the object bank
         * does not exist yet. Never let capture read a prefilled fixture. */
        blocked=!strcmp(argv[5],"capture-failure")?1:3;
        if(jgr_stage_city_geometry(disc,stage,argv[4],geometry,owners))return 6;
        if(fclose(geometry)||fclose(owners))return 7;
        blocked=0;
        if(!strcmp(argv[5],"reset"))jgr_table_city_cache_begin(disc,argv[4],dependency,argv[3]);
        if(!strcmp(argv[5],"other-disc")&&jgr_disc_open(&other,argv[1],error,sizeof error))return 8;
        if(!strcmp(argv[5],"reuse"))blocked=2;
    }
    jgr_stage_stats_reset();opens=0;
    int status=jgr_convert_table_cached(other?other:disc,argv[2],argv[4],dependency,argv[3],stdout);
    JgrStageStats stats;jgr_stage_stats(&stats);
    if(argc==6)fprintf(stderr,"{\"builds\":%u,\"dependencies\":%u}\n",stats.builds,opens);
    jgr_table_city_cache_end();jgr_disc_close(other);jgr_disc_close(disc);
    return status?4:fflush(stdout)!=0;
}
'''

class CachedTables(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.folder=Path(cls.tmp.name)
        harness=cls.folder/'harness.c';harness.write_text(HARNESS);cls.exe=cls.folder/'cached'
        cls.sources=['portable_disc','portable_convert','portable_tables','portable_tables_cached','portable_mission_cast','portable_stage','portable_stage_pack','portable_scratch','portable_models','portable_indexed','portable_stage_derived']
        subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O2','-ffp-contract=off','-D_XOPEN_SOURCE=700','-DJGR_STAGE_STATS=1','-Wall','-Wextra','-Werror','-I',str(ROOT/'psp/src'),str(harness),*[str(ROOT/'psp/src'/f'{n}.c') for n in cls.sources],'-lm','-o',str(cls.exe)],check=True)
        # Fault injection only in an isolated source copy: withhold the tunnel
        # atlas from the optional observer while strict fallback still sees it.
        source=(ROOT/'psp/src/portable_stage.c').read_text()
        old='if(word(gbix+8)==20087)'
        if source.count(old)!=1:raise AssertionError('Tunnel capture fault site changed')
        failed=cls.folder/'failed_stage.c';failed.write_text(source.replace(old,'if(!tunnel->optional&&word(gbix+8)==20087)'))
        cls.fault_exe=cls.folder/'capture-fault'
        subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O2','-ffp-contract=off','-D_XOPEN_SOURCE=700','-DJGR_STAGE_STATS=1','-Wall','-Wextra','-Werror','-I',str(ROOT/'psp/src'),str(harness),*[str(failed if n=='portable_stage' else ROOT/'psp/src'/f'{n}.c') for n in cls.sources],'-lm','-o',str(cls.fault_exe)],check=True)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def test_generated_dependency_fixtures_match_catalog(self):
        gdi=os.environ.get('JGR_TEST_GDI')
        if not gdi:self.skipTest('Set JGR_TEST_GDI to verify supported-disc conversion')
        assets=Path(os.environ.get('JGR_TEST_ASSETS',ROOT/'psp'))
        catalog={r['path']:r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']}
        dependencies=['stage1.jgs','stage1.jrs','grind_lights.jgs','stage1_obj.jgs','set1_obj.bin','stage2.jgs']
        for name in dependencies:
            data=(assets/name).read_bytes();self.assertEqual(hashlib.sha256(data).hexdigest(),catalog[name]['sha256'])
        for group in ('ptr_grind_props_data','shibuya_gate_data','ptr_shibuya_door_data','stage_ped_proxy_data','ptr_kogane_tunnel_materials','ptr_mission_cast'):
            name=f'source_data_{group}.jsd'
            proc=subprocess.run([str(self.exe),gdi,name,str(assets),str(self.folder)],capture_output=True)
            self.assertEqual(proc.returncode,0,(name,proc.stderr.decode()))
            self.assertEqual(hashlib.sha256(proc.stdout).hexdigest(),catalog[name]['sha256'],name)
    def test_missing_generated_dependency_rejected(self):
        gdi=os.environ.get('JGR_TEST_GDI')
        if not gdi:self.skipTest('Set JGR_TEST_GDI to verify supported-disc conversion')
        proc=subprocess.run([str(self.exe),gdi,'source_data_shibuya_gate_data.jsd',str(self.folder),str(self.folder)],capture_output=True)
        self.assertNotEqual(proc.returncode,0);self.assertEqual(proc.stdout,b'')
    def test_city_capture_reuses_exact_tables_before_object_bank_exists(self):
        self.city_capture_modes(('reuse',))
    def test_city_capture_is_scoped_to_disc_and_session_and_failure_falls_back(self):
        self.city_capture_modes(('reset','other-disc','capture-failure'))
    def test_optional_tunnel_capture_failure_does_not_fail_city_and_deferred_fallback_is_exact(self):
        gdi=os.environ.get('JGR_TEST_GDI')
        if not gdi:self.skipTest('Set JGR_TEST_GDI to verify supported-disc conversion')
        assets=Path(os.environ.get('JGR_TEST_ASSETS',ROOT/'psp'))
        name='source_data_ptr_kogane_tunnel_materials.jsd'
        proc=subprocess.run([str(self.fault_exe),gdi,name,str(assets),str(self.folder),'fallback'],capture_output=True)
        self.assertEqual(proc.returncode,0,proc.stderr.decode())
        expected=next(r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files'] if r['path']==name)
        self.assertEqual(hashlib.sha256(proc.stdout).hexdigest(),expected['sha256'])
        self.assertEqual(json.loads(proc.stderr),{'builds':1,'dependencies':1})
    def city_capture_modes(self,modes):
        gdi=os.environ.get('JGR_TEST_GDI')
        if not gdi:self.skipTest('Set JGR_TEST_GDI to verify supported-disc conversion')
        assets=Path(os.environ.get('JGR_TEST_ASSETS',ROOT/'psp'))
        catalog={r['path']:r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']}
        for group in ('stage_ped_proxy_data','ptr_kogane_tunnel_materials'):
            name=f'source_data_{group}.jsd'
            for mode in modes:
                if mode=='capture-failure' and 'tunnel' in group:continue
                with self.subTest(group=group,mode=mode):
                    proc=subprocess.run([str(self.exe),gdi,name,str(assets),str(self.folder),mode],capture_output=True)
                    self.assertEqual(proc.returncode,0,proc.stderr.decode())
                    self.assertEqual(hashlib.sha256(proc.stdout).hexdigest(),catalog[name]['sha256'])
                    stats=json.loads(proc.stderr)
                    self.assertEqual(stats['builds'],0 if mode=='reuse' else 1)
                    self.assertEqual(stats['dependencies'],(0 if 'tunnel' in group else 1) if mode=='reuse' else (1 if 'tunnel' in group else 2))
                    self.assertFalse(list(self.folder.glob('*.tmp')))
    def test_psp_compile(self):
        cc=os.environ.get('PSP_CC')
        if not cc:self.skipTest('Set PSP_CC for strict target compilation')
        subprocess.run([cc,'-std=c99','-Wall','-Wextra','-Werror','-DJGR_PORTABLE_ASSETS=1','-include',str(ROOT/'psp/src/portable_assets.h'),'-I',str(ROOT/'psp/src'),'-c',str(ROOT/'psp/src/portable_tables_cached.c'),'-o',str(self.folder/'cached.o')],check=True)

if __name__=='__main__':unittest.main()
